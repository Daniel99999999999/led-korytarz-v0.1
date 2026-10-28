#include "MqttService.hpp"

#include "DebugLog.hpp"
#include "DeviceIdentity.hpp"
#include "LedController.hpp"
#include "MqttCommandParser.hpp"
#include "MqttDiscoveryPayload.hpp"
#include "StackMonitor.hpp"
#include "WifiService.hpp"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace {
constexpr char kTag[] = "mqtt_service";
constexpr char kDefaultDiscoveryPrefix[] = "homeassistant";

#if LED_CONTROLLER_AT24C08_ENABLED
static_assert(MqttService::kMaxUriLength == persistent_settings::kMaxMqttUriLength);
static_assert(MqttService::kMaxUsernameLength == persistent_settings::kMaxMqttUsernameLength);
static_assert(MqttService::kMaxPasswordLength == persistent_settings::kMaxMqttPasswordLength);
static_assert(MqttService::kMaxTopicPrefixLength == persistent_settings::kMaxTopicPrefixLength);
static_assert(MqttService::kMaxDiscoveryPrefixLength ==
              persistent_settings::kMaxDiscoveryPrefixLength);
#endif
constexpr char kOnline[] = "online";
constexpr char kOffline[] = "offline";
constexpr std::uint32_t kInternalHeapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
std::atomic<bool> discovery_build_active{false};
std::atomic<std::size_t> discovery_failed_allocation_bytes{0};

void failedAllocationHook(const std::size_t size, const std::uint32_t caps,
                          const char* const function_name) noexcept {
    static_cast<void>(caps);
    static_cast<void>(function_name);
    if (discovery_build_active.load(std::memory_order_relaxed)) {
        discovery_failed_allocation_bytes.store(size, std::memory_order_relaxed);
    }
}

static_assert(device_identity::kHomeAssistantChannelNames.size() == MqttService::kChannelCount);
static_assert(device_identity::kHomeAssistantPirInputNames.size() == MqttService::kPirInputCount);
static_assert(MqttSyncMachine::kSubscriptionCount == 1 + 10 + 5 + 5 + 2 + 3 + 1 + 1);
static_assert(std::is_trivially_copyable_v<MqttService::Snapshot>,
              "Migawka MQTT musi nadawac sie do kopiowania przez kolejke FreeRTOS");

class MutexGuard final {
  public:
    explicit MutexGuard(const SemaphoreHandle_t mutex,
                        const TickType_t timeout = portMAX_DELAY) noexcept
        : mutex_(mutex) {
        locked_ = (mutex_ != nullptr) && (xSemaphoreTake(mutex_, timeout) == pdTRUE);
    }
    ~MutexGuard() {
        if (locked_) {
            static_cast<void>(xSemaphoreGive(mutex_));
        }
    }

    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

    [[nodiscard]] bool locked() const noexcept {
        return locked_;
    }

  private:
    SemaphoreHandle_t mutex_;
    bool locked_{false};
};

struct HeapSample final {
    std::size_t free_bytes{0};
    std::size_t largest_block{0};
    std::size_t minimum_free{0};
    bool observed{false};
};

using DiscoveryHeapSamples = std::array<HeapSample, 4>;

void sampleDiscoveryHeap(const mqtt_discovery::Stage stage, void* const context) noexcept {
    auto& samples = *static_cast<DiscoveryHeapSamples*>(context);
    auto& sample = samples[static_cast<std::size_t>(stage)];
    sample.free_bytes = heap_caps_get_free_size(kInternalHeapCaps);
    sample.largest_block = heap_caps_get_largest_free_block(kInternalHeapCaps);
    sample.minimum_free = heap_caps_get_minimum_free_size(kInternalHeapCaps);
    sample.observed = true;
}

void logDiscoveryHeap(const char* const stage, const HeapSample& sample) noexcept {
    if (sample.observed) {
        ESP_LOGI(kTag, "Discovery RAM %s: free=%u B, largest=%u B, minimum=%u B", stage,
                 static_cast<unsigned>(sample.free_bytes),
                 static_cast<unsigned>(sample.largest_block),
                 static_cast<unsigned>(sample.minimum_free));
    }
}

[[nodiscard]] bool formatDeviceId(char* const destination, const std::size_t destination_size,
                                  const char* const prefix,
                                  const std::array<std::uint8_t, 6>& mac) noexcept {
    const int written =
        std::snprintf(destination, destination_size, "%s_%02x%02x%02x%02x%02x%02x", prefix,
                      static_cast<unsigned>(mac[0]), static_cast<unsigned>(mac[1]),
                      static_cast<unsigned>(mac[2]), static_cast<unsigned>(mac[3]),
                      static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
    return (written >= 0) && (static_cast<std::size_t>(written) < destination_size);
}
} // namespace

MqttService::MqttService(LedController& controller, OtaService& ota_service,
                         WifiService& wifi_service) noexcept
    : wifi_service_(wifi_service), controller_(controller), ota_service_(ota_service) {}

const char* MqttService::syncErrorName(const SyncError error) noexcept {
    switch (error) {
    case SyncError::none:
        return "none";
    case SyncError::subscribe_send:
        return "subscribe_send";
    case SyncError::subscribe_rejected:
        return "subscribe_rejected";
    case SyncError::subscribe_timeout:
        return "subscribe_timeout";
    case SyncError::discovery_puback_timeout:
        return "discovery_puback_timeout";
    case SyncError::birth_enqueue:
        return "birth_enqueue";
    case SyncError::discovery_root:
        return "discovery_root";
    case SyncError::discovery_local:
        return "discovery_local";
    case SyncError::discovery_debug:
        return "discovery_debug";
    case SyncError::discovery_component:
        return "discovery_component";
    case SyncError::discovery_buffer:
        return "discovery_buffer";
    case SyncError::discovery_enqueue:
        return "discovery_enqueue";
    }
    return "unknown";
}

esp_err_t MqttService::initialize() noexcept {
    if (initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    client_mutex_ = xSemaphoreCreateMutexStatic(&client_mutex_storage_);
    snapshot_queue_ = xQueueCreateStatic(1, sizeof(Snapshot), snapshot_queue_buffer_.data(),
                                         &snapshot_queue_storage_);
    if ((mutex_ == nullptr) || (client_mutex_ == nullptr) || (snapshot_queue_ == nullptr)) {
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t hook_result = heap_caps_register_failed_alloc_callback(failedAllocationHook);
    if (hook_result != ESP_OK) {
        ESP_LOGW(kTag, "Diagnostyka nieudanych alokacji: %s", esp_err_to_name(hook_result));
    }

    std::array<std::uint8_t, 6> mac{};
    const esp_err_t mac_result = esp_read_mac(mac.data(), ESP_MAC_WIFI_STA);
    if (mac_result != ESP_OK) {
        return mac_result;
    }

    if (!formatDeviceId(device_id_.data(), device_id_.size(), device_identity::kDeviceIdPrefix,
                        mac)) {
        return ESP_ERR_INVALID_SIZE;
    }

    copyText(staged_config_.topic_prefix.data(), staged_config_.topic_prefix.size(),
             device_identity::kTechnicalSlug, kMaxTopicPrefixLength);
    copyText(staged_config_.discovery_prefix.data(), staged_config_.discovery_prefix.size(),
             kDefaultDiscoveryPrefix, kMaxDiscoveryPrefixLength);

    {
        MutexGuard lock(mutex_);
        if (!lock.locked()) {
            return ESP_FAIL;
        }
        status_.initialized = true;
        status_.state = State::unconfigured;
        copyText(status_.device_id.data(), status_.device_id.size(), device_id_.data(),
                 device_id_.size() - 1U);
        updateStatusFromConfigurationLocked();
    }

    initialized_ = true;
    const esp_err_t ota_callback_result = ota_service_.setStatusCallback(otaStatusCallback, this);
    if (ota_callback_result != ESP_OK) {
        ESP_LOGW(kTag, "Status OTA nie bedzie publikowany: %s",
                 esp_err_to_name(ota_callback_result));
    }
#if LED_CONTROLLER_AT24C08_ENABLED
    ESP_LOGI(kTag, "MQTT przygotowane; device id=%s, zapis przez AT24C08N", device_id_.data());
#else
    ESP_LOGI(kTag, "MQTT przygotowane; device id=%s, storage=RAM only", device_id_.data());
#endif
    return ESP_OK;
}

esp_err_t MqttService::start() noexcept {
    if (!initialized_ || started_ || (task_handle_ != nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    const BaseType_t result = xTaskCreate(taskEntry, "mqtt_service", kServiceTaskStackSize, this,
                                          kServiceTaskPriority, &task_handle_);
    if (result != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    started_ = true;
    {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.started = true;
        }
    }
    return ESP_OK;
}

esp_err_t MqttService::setUri(const char* const uri) noexcept {
    if ((uri == nullptr) || (std::strncmp(uri, "mqtt://", 7) != 0) || (uri[7] == '\0') ||
        (std::strchr(uri, '@') != nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strlen(uri) > kMaxUriLength) {
        return ESP_ERR_INVALID_SIZE;
    }

    MutexGuard lock(mutex_);
    if (!lock.locked()) {
        return ESP_ERR_INVALID_STATE;
    }
    copyText(staged_config_.uri.data(), staged_config_.uri.size(), uri, kMaxUriLength);
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}

esp_err_t MqttService::setUsername(const char* const username) noexcept {
    if ((username == nullptr) || (username[0] == '\0')) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strlen(username) > kMaxUsernameLength) {
        return ESP_ERR_INVALID_SIZE;
    }

    MutexGuard lock(mutex_);
    if (!lock.locked()) {
        return ESP_ERR_INVALID_STATE;
    }
    copyText(staged_config_.username.data(), staged_config_.username.size(), username,
             kMaxUsernameLength);
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}

esp_err_t MqttService::setPassword(const char* const password) noexcept {
    if (password == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strlen(password) > kMaxPasswordLength) {
        return ESP_ERR_INVALID_SIZE;
    }

    MutexGuard lock(mutex_);
    if (!lock.locked()) {
        return ESP_ERR_INVALID_STATE;
    }
    secureZero(staged_config_.password.data(), staged_config_.password.size());
    copyText(staged_config_.password.data(), staged_config_.password.size(), password,
             kMaxPasswordLength);
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}

esp_err_t MqttService::setTopicPrefix(const char* const prefix) noexcept {
    if (!topicPrefixIsValid(prefix, kMaxTopicPrefixLength)) {
        return ESP_ERR_INVALID_ARG;
    }

    MutexGuard lock(mutex_);
    if (!lock.locked()) {
        return ESP_ERR_INVALID_STATE;
    }
    copyText(staged_config_.topic_prefix.data(), staged_config_.topic_prefix.size(), prefix,
             kMaxTopicPrefixLength);
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}

esp_err_t MqttService::setDiscoveryPrefix(const char* const prefix) noexcept {
    if (!topicPrefixIsValid(prefix, kMaxDiscoveryPrefixLength)) {
        return ESP_ERR_INVALID_ARG;
    }

    MutexGuard lock(mutex_);
    if (!lock.locked()) {
        return ESP_ERR_INVALID_STATE;
    }
    copyText(staged_config_.discovery_prefix.data(), staged_config_.discovery_prefix.size(), prefix,
             kMaxDiscoveryPrefixLength);
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}

esp_err_t MqttService::apply() noexcept {
    if (!started_) {
        return ESP_ERR_INVALID_STATE;
    }

    RuntimeConfig requested{};
    {
        MutexGuard lock(mutex_);
        if (!lock.locked()) {
            return ESP_FAIL;
        }
        if (!configurationIsValid(staged_config_)) {
            return ESP_ERR_INVALID_ARG;
        }
        requested = staged_config_;
    }

    esp_err_t result = stopClient(true);
    if (result != ESP_OK) {
        secureZero(&requested, sizeof(requested));
        return result;
    }

    {
        MutexGuard lock(mutex_);
        if (!lock.locked()) {
            secureZero(&requested, sizeof(requested));
            return ESP_FAIL;
        }
        secureZero(applied_config_.password.data(), applied_config_.password.size());
        applied_config_ = requested;
        discovery_payload_valid_ = false;
        discovery_payload_bytes_ = 0;
        status_.discovery_payload_bytes = 0;
        status_.configuration_applied = false;
        if (!buildTopics(applied_config_)) {
            secureZero(&requested, sizeof(requested));
            updateStatusFromConfigurationLocked();
            return ESP_ERR_INVALID_SIZE;
        }
        status_.configuration_applied = true;
        status_.state = State::connecting;
        updateStatusFromConfigurationLocked();
    }

    ESP_LOGI(kTag, "=== MQTT CONFIG === Keepalive=30 s, Protocol=MQTT 3.1.1 / v4");
    // Log only the authority, never URI userinfo, path, query or the password.
    const char* const authority = requested.uri.data() + 7U; // validated mqtt:// prefix
    const std::size_t authority_length = std::strcspn(authority, "/?#");
    const bool has_userinfo = std::memchr(authority, '@', authority_length) != nullptr;
    ESP_LOGI(kTag, "Zastosowano konfiguracje: broker=mqtt://%.*s, user=%s, haslo=%s",
             has_userinfo ? 8 : static_cast<int>(authority_length),
             has_userinfo ? "[ukryte]" : authority, requested.username.data(),
             requested.password[0] != '\0' ? "ustawione" : "brak");
    ESP_LOGI(kTag, "MQTT: topic=%s, discovery=%s, client_id=%s; reconnect=%lu ms, timeout=%lu ms",
             requested.topic_prefix.data(), requested.discovery_prefix.data(), device_id_.data(),
             static_cast<unsigned long>(kReconnectIntervalMs),
             static_cast<unsigned long>(kNetworkTimeoutMs));
    secureZero(&requested, sizeof(requested));
    result = createClient();
    ESP_LOGI(kTag, "Przygotowanie klienta MQTT: %s (start odroczony do GOT_IP)",
             esp_err_to_name(result));
    if (result != ESP_OK) {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.state = State::disconnected;
        }
    }
    return result;
}

esp_err_t MqttService::disconnect() noexcept {
    const esp_err_t result = stopClient(true);
    MutexGuard lock(mutex_);
    if (lock.locked()) {
        status_.state = status_.configuration_ready ? State::ready : State::unconfigured;
    }
    return result;
}

esp_err_t MqttService::clear() noexcept {
    const esp_err_t stop_result = stopClient(true);

    MutexGuard lock(mutex_);
    if (!lock.locked()) {
        return ESP_FAIL;
    }
    secureZero(&staged_config_, sizeof(staged_config_));
    secureZero(&applied_config_, sizeof(applied_config_));
    topics_ = {};
    discovery_payload_valid_ = false;
    discovery_payload_bytes_ = 0;
    status_.discovery_payload_bytes = 0;
    copyText(staged_config_.topic_prefix.data(), staged_config_.topic_prefix.size(),
             device_identity::kTechnicalSlug, kMaxTopicPrefixLength);
    copyText(staged_config_.discovery_prefix.data(), staged_config_.discovery_prefix.size(),
             kDefaultDiscoveryPrefix, kMaxDiscoveryPrefixLength);
    status_.configuration_applied = false;
    status_.state = State::unconfigured;
    updateStatusFromConfigurationLocked();
    return stop_result;
}

esp_err_t MqttService::submitSnapshot(const Snapshot& snapshot) noexcept {
    if (snapshot_queue_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueOverwrite(snapshot_queue_, &snapshot) != pdPASS) {
        return ESP_FAIL;
    }
    if (task_handle_ != nullptr) {
        static_cast<void>(xTaskNotifyGive(task_handle_));
    }
    return ESP_OK;
}

MqttService::Status MqttService::status() const noexcept {
    Status copy{};
    MutexGuard lock(mutex_);
    if (lock.locked()) {
        copy = status_;
    }
    copy.publish_backlog = publish_backlog_.load(std::memory_order_acquire);
    copy.accepted_commands = accepted_commands_.load(std::memory_order_relaxed);
    copy.rejected_commands = rejected_commands_.load(std::memory_order_relaxed);
    return copy;
}

bool MqttService::tryGetStatus(Status& output) const noexcept {
    Status copy{};
    MutexGuard lock(mutex_, 0);
    if (!lock.locked()) {
        return false;
    }
    copy = status_;
    copy.publish_backlog = publish_backlog_.load(std::memory_order_acquire);
    copy.accepted_commands = accepted_commands_.load(std::memory_order_relaxed);
    copy.rejected_commands = rejected_commands_.load(std::memory_order_relaxed);
    output = copy;
    return true;
}
bool MqttService::tryGetSnapshot(Snapshot& output) const noexcept {
    return snapshot_queue_ != nullptr && xQueuePeek(snapshot_queue_, &output, 0) == pdPASS;
}

#if LED_CONTROLLER_AT24C08_ENABLED
esp_err_t
MqttService::exportPersistentConfig(persistent_settings::MqttConfig& output) const noexcept {
    output = {};
    MutexGuard lock(mutex_, 0);
    if (!lock.locked()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!status_.configuration_applied) {
        return ESP_OK;
    }

    output.configured = true;
    copyText(output.uri.data(), output.uri.size(), applied_config_.uri.data(), kMaxUriLength);
    copyText(output.username.data(), output.username.size(), applied_config_.username.data(),
             kMaxUsernameLength);
    copyText(output.password.data(), output.password.size(), applied_config_.password.data(),
             kMaxPasswordLength);
    copyText(output.topic_prefix.data(), output.topic_prefix.size(),
             applied_config_.topic_prefix.data(), kMaxTopicPrefixLength);
    copyText(output.discovery_prefix.data(), output.discovery_prefix.size(),
             applied_config_.discovery_prefix.data(), kMaxDiscoveryPrefixLength);
    return ESP_OK;
}

esp_err_t
MqttService::restorePersistentConfig(const persistent_settings::MqttConfig& input) noexcept {
    if (!initialized_ || !input.configured) {
        return ESP_ERR_INVALID_ARG;
    }

    RuntimeConfig candidate{};
    copyText(candidate.uri.data(), candidate.uri.size(), input.uri.data(), kMaxUriLength);
    copyText(candidate.username.data(), candidate.username.size(), input.username.data(),
             kMaxUsernameLength);
    copyText(candidate.password.data(), candidate.password.size(), input.password.data(),
             kMaxPasswordLength);
    copyText(candidate.topic_prefix.data(), candidate.topic_prefix.size(),
             input.topic_prefix.data(), kMaxTopicPrefixLength);
    copyText(candidate.discovery_prefix.data(), candidate.discovery_prefix.size(),
             input.discovery_prefix.data(), kMaxDiscoveryPrefixLength);
    if (!configurationIsValid(candidate)) {
        secureZero(&candidate, sizeof(candidate));
        return ESP_ERR_INVALID_ARG;
    }

    MutexGuard lock(mutex_);
    if (!lock.locked()) {
        secureZero(&candidate, sizeof(candidate));
        return ESP_FAIL;
    }
    secureZero(&staged_config_, sizeof(staged_config_));
    staged_config_ = candidate;
    secureZero(&candidate, sizeof(candidate));
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}
#endif

void MqttService::taskEntry(void* const context) noexcept {
    static_cast<MqttService*>(context)->taskLoop();
}

[[noreturn]] void MqttService::taskLoop() noexcept {
    DebugLog::instance().excludePublisherTask(xTaskGetCurrentTaskHandle());
    TickType_t last_state_publish = 0;
    TickType_t last_ota_attempt = 0;
    bool ota_retry_pending = false;
    std::uint32_t observed_generation = 0;
    for (;;) {
        stack_monitor::sample(stack_monitor::Task::mqtt_service, kServiceTaskStackSize);
        static_cast<void>(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200)));
        startClientWhenNetworkReady();
        if (!connected_.load(std::memory_order_acquire)) {
            continue;
        }

        const std::uint32_t generation = connection_generation_.load(std::memory_order_acquire);
        if (generation != observed_generation) {
            observed_generation = generation;
            sync_machine_.onConnected();
            publish_tracker_.invalidateAll();
            ota_status_published_ = false;
            debug_status_published_ = false;
            stack_status_published_ = false;
            last_state_publish = 0;
            ota_retry_pending = false;
        }
        if (synchronization_requested_.exchange(false, std::memory_order_acq_rel)) {
            sync_machine_.onBirth();
            discovery_message_id_.store(0, std::memory_order_release);
            discovery_acknowledged_.store(false, std::memory_order_release);
            publish_tracker_.invalidateAll();
            ota_status_published_ = false;
            debug_status_published_ = false;
            stack_status_published_ = false;
            MutexGuard lock(mutex_);
            if (lock.locked()) {
                status_.birth_queued = false;
                status_.discovery_queued = false;
                status_.discovery_confirmed = false;
            }
        }

        serviceDebug();
        const TickType_t now = xTaskGetTickCount();
        if (!ota_retry_pending ||
            now - last_ota_attempt >= pdMS_TO_TICKS(kPublishRetryIntervalMs)) {
            ota_retry_pending = !publishOtaStatus();
            last_ota_attempt = now;
        }

        Snapshot snapshot{};
        if (xQueuePeek(snapshot_queue_, &snapshot, 0) == pdPASS &&
            (last_state_publish == 0 ||
             now - last_state_publish >= pdMS_TO_TICKS(kMinimumStatePublishIntervalMs))) {
            static_cast<void>(publishSnapshot(snapshot));
            last_state_publish = now;
        }
        advanceSynchronization(esp_log_timestamp());
    }
}

void MqttService::eventHandler(void* const context, const esp_event_base_t event_base,
                               const std::int32_t event_id, void* const event_data) noexcept {
    static_cast<void>(event_base);
    static_cast<void>(event_id);
    auto* const service = static_cast<MqttService*>(context);

    stack_monitor::Scope stack_sample(stack_monitor::Task::mqtt_client, kServiceTaskStackSize,
                                      false);
    service->handleEvent(static_cast<esp_mqtt_event_handle_t>(event_data));
}

void MqttService::otaStatusCallback(void* const context,
                                    const OtaService::Status& status) noexcept {
    static_cast<void>(status);
    auto* const service = static_cast<MqttService*>(context);
    if ((service != nullptr) && (service->task_handle_ != nullptr)) {
        static_cast<void>(xTaskNotifyGive(service->task_handle_));
    }
}

void MqttService::handleEvent(const esp_mqtt_event_handle_t event) noexcept {
    if ((event == nullptr) || (event->client != active_client_.load(std::memory_order_acquire))) {
        return;
    }

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED: {
        last_suback_id_.store(0, std::memory_order_release);
        last_puback_id_.store(0, std::memory_order_release);
        discovery_message_id_.store(0, std::memory_order_release);
        discovery_acknowledged_.store(false, std::memory_order_release);
        connection_generation_.fetch_add(1, std::memory_order_acq_rel);
        connected_.store(true, std::memory_order_release);
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.state = State::connected;
            ++status_.connect_count;
            status_.last_error_type = 0;
            status_.last_connect_return_code = 0;
            status_.birth_queued = false;
            status_.subscriptions_ready = false;
            status_.subscriptions_done = 0;
            status_.discovery_queued = false;
            status_.discovery_confirmed = false;
            status_.outbox_bytes = 0;
        }
        ESP_LOGI(kTag, "Polaczono z brokerem MQTT");
        if (task_handle_ != nullptr) {
            static_cast<void>(xTaskNotifyGive(task_handle_));
        }
        break;
    }
    case MQTT_EVENT_DISCONNECTED: {
        connected_.store(false, std::memory_order_release);
        connection_generation_.fetch_add(1, std::memory_order_acq_rel);
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            if (status_.state != State::authentication_error) {
                status_.state = State::disconnected;
            }
            status_.birth_queued = false;
            status_.subscriptions_ready = false;
            status_.subscriptions_done = 0;
            status_.discovery_queued = false;
            status_.discovery_confirmed = false;
            status_.outbox_bytes = 0;
        }
        ESP_LOGW(kTag, "Rozlaczono z brokerem; Wi-Fi ready=%s, klient ponowi polaczenie",
                 wifi_service_.networkReady() ? "tak" : "nie");
        break;
    }
    case MQTT_EVENT_SUBSCRIBED:
        last_suback_accepted_.store(event->data != nullptr && event->data_len == 1 &&
                                        static_cast<std::uint8_t>(event->data[0]) < 0x80,
                                    std::memory_order_release);
        last_suback_id_.store(event->msg_id, std::memory_order_release);
        if (task_handle_ != nullptr) {
            static_cast<void>(xTaskNotifyGive(task_handle_));
        }
        break;
    case MQTT_EVENT_PUBLISHED:
        last_puback_id_.store(event->msg_id, std::memory_order_release);
        if (event->msg_id > 0 &&
            event->msg_id == discovery_message_id_.load(std::memory_order_acquire)) {
            discovery_acknowledged_.store(true, std::memory_order_release);
            if (task_handle_ != nullptr) {
                static_cast<void>(xTaskNotifyGive(task_handle_));
            }
        }
        break;
    case MQTT_EVENT_DATA:
        handleData(*event);
        break;
    case MQTT_EVENT_ERROR: {
        MutexGuard lock(mutex_);
        if (lock.locked() && (event->error_handle != nullptr)) {
            status_.last_error_type = static_cast<std::int32_t>(event->error_handle->error_type);
            status_.last_connect_return_code =
                static_cast<std::int32_t>(event->error_handle->connect_return_code);
            if ((event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) &&
                ((event->error_handle->connect_return_code ==
                  MQTT_CONNECTION_REFUSE_BAD_USERNAME) ||
                 (event->error_handle->connect_return_code ==
                  MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED))) {
                status_.state = State::authentication_error;
            }
        }
        if (event->error_handle != nullptr) {
            const auto& error = *event->error_handle;
            const char* authority = applied_config_.uri.data() + 7U;
            const std::size_t length = std::strcspn(authority, "/?#");
            const bool hidden = std::memchr(authority, '@', length) != nullptr;
            ESP_LOGE(kTag, "MQTT ERROR target URI=mqtt://%.*s",
                     hidden ? 8 : static_cast<int>(length), hidden ? "[ukryte]" : authority);
            ESP_LOGE(kTag, "sock_errno=%d, sock_errno_description=%s",
                     error.esp_transport_sock_errno, std::strerror(error.esp_transport_sock_errno));
            ESP_LOGE(kTag,
                     "MQTT error_type=%d, esp_tls_last_esp_err=0x%x, esp_tls_stack_err=0x%x, "
                     "esp_tls_cert_verify_flags=0x%x, connect_return_code=%d, socket_errno=%d",
                     static_cast<int>(error.error_type),
                     static_cast<unsigned>(error.esp_tls_last_esp_err),
                     static_cast<unsigned>(error.esp_tls_stack_err),
                     static_cast<unsigned>(error.esp_tls_cert_verify_flags),
                     static_cast<int>(error.connect_return_code), error.esp_transport_sock_errno);
        }
        ESP_LOGE(kTag, "Blad polaczenia MQTT; sterowanie lokalne pozostaje aktywne");
        break;
    }
    default:
        break;
    }
}

void MqttService::handleData(const esp_mqtt_event_t& event) noexcept {
    if ((event.topic == nullptr) || (event.topic_len <= 0) || (event.data == nullptr) ||
        (event.data_len < 0) || (event.current_data_offset != 0) ||
        (event.data_len != event.total_data_len)) {
        noteRejectedCommand();
        ESP_LOGW(kTag, "Odrzucono fragmentowana albo niepelna wiadomosc MQTT");
        return;
    }

    if (topicEquals(event.topic, event.topic_len, topics_.debug_command.data())) {
        const bool on = event.data_len == 2 && std::memcmp(event.data, "ON", 2) == 0;
        const bool off = event.data_len == 3 && std::memcmp(event.data, "OFF", 3) == 0;
        if (event.retain || (!on && !off) || !DebugLog::instance().setEnabled(on)) {
            noteRejectedCommand();
        } else {
            noteAcceptedCommand();
            if (task_handle_ != nullptr) {
                static_cast<void>(xTaskNotifyGive(task_handle_));
            }
            ESP_LOGI(kTag, "Debug MQTT %s (limit sesji 600 s)", on ? "ON" : "OFF");
        }
        return;
    }

    if (topicEquals(event.topic, event.topic_len, topics_.home_assistant_status.data())) {
        if ((event.data_len == static_cast<int>(std::strlen(kOnline))) &&
            (std::memcmp(event.data, kOnline, std::strlen(kOnline)) == 0)) {
            requestSynchronization();
        }
        return;
    }

    if (topicEquals(event.topic, event.topic_len, topics_.ota_command.data())) {
        if (event.retain) {
            noteRejectedCommand();
            ESP_LOGW(kTag, "Odrzucono retained command OTA");
            return;
        }
        const esp_err_t ota_result =
            ota_service_.requestUpdate(event.data, static_cast<std::size_t>(event.data_len));
        if (ota_result != ESP_OK) {
            noteRejectedCommand();
            ESP_LOGW(kTag, "Odrzucono komende OTA: %s", esp_err_to_name(ota_result));
            return;
        }
        noteAcceptedCommand();
        return;
    }

    for (std::size_t i = 0; i < 10; ++i) {
        if (!topicEquals(event.topic, event.topic_len, topics_.pwm_command[i].data()))
            continue;
        ControlCommand command{};
        command.kind = ControlCommand::Kind::pwm_setting;
        command.channel_index = i / 5;
        command.setting_index = static_cast<std::uint8_t>(i % 5);
        if (event.retain ||
            !pwm_config::parse(static_cast<unsigned>(i % 5), event.data,
                               static_cast<std::size_t>(event.data_len), command.setting_value) ||
            controller_.enqueueCommand(command) != ESP_OK) {
            noteRejectedCommand();
            return;
        }
        return;
    }
    for (std::size_t index = 0; index < kChannelCount + kPirInputCount; ++index) {
        const bool mapping = index < kChannelCount;
        const std::size_t setting_index = mapping ? index : index - kChannelCount;
        const char* const topic = mapping ? topics_.mapping_command[setting_index].data()
                                          : topics_.delay_command[setting_index].data();
        if (!topicEquals(event.topic, event.topic_len, topic)) {
            continue;
        }
        ControlCommand command{};
        command.kind =
            mapping ? ControlCommand::Kind::button_mapping : ControlCommand::Kind::pir_delay;
        command.channel_index = setting_index;
        std::uint8_t mask = 0;
        const auto length = static_cast<std::size_t>(event.data_len);
        const bool valid =
            mapping ? local_control::parseMask(event.data, length, mask)
                    : local_control::parseDelay(event.data, length, command.setting_value);
        if (mapping) {
            command.setting_value = mask;
        }
        if (event.retain || !valid || controller_.enqueueCommand(command) != ESP_OK) {
            noteRejectedCommand();
            ESP_LOGW(kTag, "Odrzucono ustawienie sterowania lokalnego");
        } else {
            noteAcceptedCommand();
        }
        return;
    }

    if (topicEquals(event.topic, event.topic_len, topics_.lcd_mode_command.data()) ||
        topicEquals(event.topic, event.topic_len, topics_.lcd_override_command.data()) ||
        topicEquals(event.topic, event.topic_len, topics_.lcd_night_command.data())) {
        ControlCommand command{};
        bool valid = false;
        const auto size = static_cast<std::size_t>(event.data_len);
        if (topicEquals(event.topic, event.topic_len, topics_.lcd_mode_command.data())) {
            command.kind = ControlCommand::Kind::lcd_mode;
            valid = local_control::parseBacklightMode(event.data, size, command.setting_value);
        } else if (topicEquals(event.topic, event.topic_len, topics_.lcd_override_command.data())) {
            command.kind = ControlCommand::Kind::lcd_override;
            valid = local_control::parseDelay(event.data, size, command.setting_value) &&
                    command.setting_value > 0;
        } else {
            command.kind = ControlCommand::Kind::lcd_night;
            const bool on = size == 2U && std::memcmp(event.data, "ON", 2) == 0;
            valid = on || (size == 3U && std::memcmp(event.data, "OFF", 3) == 0);
            command.setting_value = on ? 1 : 0;
        }
        if (event.retain || !valid || controller_.enqueueCommand(command) != ESP_OK) {
            noteRejectedCommand();
        } else {
            noteAcceptedCommand();
        }
        return;
    }

    std::size_t channel_index = kChannelCount;
    for (std::size_t index = 0; index < kChannelCount; ++index) {
        if (topicEquals(event.topic, event.topic_len, topics_.command[index].data())) {
            channel_index = index;
            break;
        }
    }
    if (channel_index >= kChannelCount) {
        return;
    }

    if (event.retain) {
        noteRejectedCommand();
        ESP_LOGW(kTag, "Odrzucono retained command kanalu %u",
                 static_cast<unsigned>(channel_index + 1U));
        return;
    }

    ControlCommand command{};
    const mqtt_protocol::ParseResult parse_result =
        mqtt_protocol::parseCommand(event.data, static_cast<std::size_t>(event.data_len), command);
    if (parse_result != mqtt_protocol::ParseResult::ok) {
        noteRejectedCommand();
        ESP_LOGW(kTag, "Odrzucono komende kanalu %u: %s", static_cast<unsigned>(channel_index + 1U),
                 mqtt_protocol::parseResultName(parse_result));
        return;
    }

    command.channel_index = channel_index;
    const esp_err_t queue_result = controller_.enqueueCommand(command);
    if (queue_result != ESP_OK) {
        noteRejectedCommand();
        ESP_LOGW(kTag, "Odrzucono komende kanalu %u: kolejka kontrolera pelna",
                 static_cast<unsigned>(channel_index + 1U));
        return;
    }
    noteAcceptedCommand();
}

void MqttService::requestSynchronization() noexcept {
    synchronization_requested_.store(true, std::memory_order_release);
    if (task_handle_ != nullptr) {
        static_cast<void>(xTaskNotifyGive(task_handle_));
    }
}

const char* MqttService::subscriptionTopic(const std::size_t index) const noexcept {
    if (index == 0) {
        return topics_.debug_command.data();
    }
    if (index <= 10) {
        return topics_.pwm_command[index - 1].data();
    }
    if (index <= 15) {
        return topics_.command[index - 11].data();
    }
    if (index <= 20) {
        return topics_.mapping_command[index - 16].data();
    }
    if (index <= 22) {
        return topics_.delay_command[index - 21].data();
    }
    switch (index) {
    case 23:
        return topics_.lcd_mode_command.data();
    case 24:
        return topics_.lcd_override_command.data();
    case 25:
        return topics_.lcd_night_command.data();
    case 26:
        return topics_.home_assistant_status.data();
    case 27:
        return topics_.ota_command.data();
    default:
        return nullptr;
    }
}

void MqttService::advanceSynchronization(const std::uint32_t now_ms) noexcept {
    const int suback_id = last_suback_id_.load(std::memory_order_acquire);
    const auto suback = sync_machine_.onSuback(
        suback_id, last_suback_accepted_.load(std::memory_order_acquire), now_ms);
    if (suback != MqttSyncMachine::SubackResult::ignored) {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.subscriptions_done =
                static_cast<std::uint8_t>(sync_machine_.subscriptionIndex());
            status_.subscriptions_ready = sync_machine_.subscriptionsReady();
            if (suback == MqttSyncMachine::SubackResult::rejected) {
                status_.last_sync_error = SyncError::subscribe_rejected;
            }
        }
        if (suback == MqttSyncMachine::SubackResult::rejected) {
            ESP_LOGW(kTag, "SUBACK odrzucony; ponowienie z backoff (topic %u)",
                     static_cast<unsigned>(sync_machine_.subscriptionIndex() + 1));
        } else if (sync_machine_.subscriptionsReady()) {
            ESP_LOGI(kTag, "Subskrypcje MQTT potwierdzone: %u/%u",
                     static_cast<unsigned>(MqttSyncMachine::kSubscriptionCount),
                     static_cast<unsigned>(MqttSyncMachine::kSubscriptionCount));
        }
    }
    if (sync_machine_.pollSubackTimeout(now_ms)) {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.last_sync_error = SyncError::subscribe_timeout;
        }
        ESP_LOGW(kTag, "Brak SUBACK; ponowienie z backoff (topic %u)",
                 static_cast<unsigned>(sync_machine_.subscriptionIndex() + 1));
    }
    if (discovery_acknowledged_.load(std::memory_order_acquire) &&
        sync_machine_.confirmDiscovery(discovery_message_id_.load(std::memory_order_acquire))) {
        {
            MutexGuard lock(mutex_);
            if (lock.locked()) {
                status_.discovery_confirmed = true;
                status_.discovery_queued = true;
                status_.last_sync_error = SyncError::none;
            }
        }
        ESP_LOGI(kTag, "Discovery potwierdzone przez broker (PUBACK)");
    }
    if (sync_machine_.pollPubackTimeout(now_ms)) {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.discovery_queued = false;
            status_.last_sync_error = SyncError::discovery_puback_timeout;
        }
        ESP_LOGW(kTag, "Brak PUBACK Discovery; ponowienie po backoff i zwolnieniu outboxu");
    }

    MutexGuard client_lock(client_mutex_, 0);
    if (!client_lock.locked() || !connected_.load(std::memory_order_acquire) ||
        client_ == nullptr) {
        return;
    }
    if (sync_machine_.canQueueBirth(now_ms)) {
        if (enqueueMessage(client_, topics_.availability.data(), kOnline, true)) {
            sync_machine_.birthQueued();
            MutexGuard lock(mutex_);
            if (lock.locked()) {
                status_.birth_queued = true;
            }
        } else {
            sync_machine_.birthFailed(now_ms);
            MutexGuard lock(mutex_);
            if (lock.locked()) {
                status_.last_sync_error = SyncError::birth_enqueue;
            }
        }
    } else if (sync_machine_.canSubscribe(now_ms)) {
        const char* const topic = subscriptionTopic(sync_machine_.subscriptionIndex());
        const int id = topic == nullptr ? -1 : esp_mqtt_client_subscribe(client_, topic, 1);
        if (id > 0) {
            sync_machine_.subscribeSent(id, now_ms);
        } else {
            sync_machine_.subscribeFailed(now_ms);
            MutexGuard lock(mutex_);
            if (lock.locked()) {
                status_.last_sync_error = SyncError::subscribe_send;
            }
            ESP_LOGW(kTag, "SUBSCRIBE nieudany; ponowienie z backoff (topic %u)",
                     static_cast<unsigned>(sync_machine_.subscriptionIndex() + 1));
        }
    } else if (sync_machine_.canQueueDiscovery(now_ms) &&
               esp_mqtt_client_get_outbox_size(client_) <= static_cast<int>(kMqttBufferLength)) {
        last_puback_id_.store(0, std::memory_order_release);
        discovery_acknowledged_.store(false, std::memory_order_release);
        const int id = publishDiscovery(client_);
        if (id > 0) {
            sync_machine_.discoveryQueued(id, now_ms);
            discovery_message_id_.store(id, std::memory_order_release);
            if (last_puback_id_.load(std::memory_order_acquire) == id) {
                discovery_acknowledged_.store(true, std::memory_order_release);
            }
            MutexGuard lock(mutex_);
            if (lock.locked()) {
                status_.discovery_queued = true;
                status_.discovery_confirmed = false;
            }
        } else {
            sync_machine_.discoveryFailed(now_ms);
        }
    }
    const int outbox_bytes = esp_mqtt_client_get_outbox_size(client_);
    MutexGuard lock(mutex_);
    if (lock.locked()) {
        status_.outbox_bytes = outbox_bytes > 0 ? static_cast<std::uint32_t>(outbox_bytes) : 0;
    }
}

bool MqttService::publishSnapshot(const Snapshot& snapshot) noexcept {
    MutexGuard client_lock(client_mutex_);
    if (!client_lock.locked() || !connected_.load(std::memory_order_acquire) ||
        (client_ == nullptr)) {
        return false;
    }

    const auto enqueue_tracked = [this](const MqttPublishTracker::Field field,
                                        const std::uint64_t fingerprint, const char* const topic,
                                        const char* const payload) noexcept {
        if (!publish_tracker_.needsPublish(field, fingerprint)) {
            return true;
        }
        if (!enqueueMessage(client_, topic, payload, true)) {
            return false;
        }
        publish_tracker_.markPublished(field, fingerprint);
        return true;
    };

    for (std::size_t i = 0; i < 10; ++i) {
        const auto value =
            pwm_config::value(snapshot.local_config.pwm[i / 5], static_cast<unsigned>(i % 5));
        std::array<char, 12> payload{};
        if (i % 5 == 4)
            std::snprintf(payload.data(), payload.size(), "%s", value ? "ON/OFF" : "PWM");
        else
            std::snprintf(payload.data(), payload.size(), "%u", static_cast<unsigned>(value));
        const auto field = static_cast<MqttPublishTracker::Field>(
            static_cast<unsigned>(MqttPublishTracker::Field::pwm_4_frequency) + i);
        if (!enqueue_tracked(field, value, topics_.pwm_state[i].data(), payload.data()))
            return false;
    }
    for (std::size_t index = 0; index < kChannelCount; ++index) {
        const auto mask = snapshot.local_config.button_masks[index];
        if (!enqueue_tracked(MqttPublishTracker::mappingField(index), mask,
                             topics_.mapping_state[index].data(),
                             local_control::optionForMask(mask))) {
            return false;
        }
    }
    for (std::size_t index = 0; index < kPirInputCount; ++index) {
        std::array<char, 8> value{};
        const auto seconds = snapshot.local_config.off_delay_seconds[index];
        std::snprintf(value.data(), value.size(), "%u", static_cast<unsigned>(seconds));
        if (!enqueue_tracked(MqttPublishTracker::delayField(index), seconds,
                             topics_.delay_state[index].data(), value.data())) {
            return false;
        }
    }

    std::array<char, 8> override_seconds{};
    std::snprintf(override_seconds.data(), override_seconds.size(), "%u",
                  static_cast<unsigned>(snapshot.local_config.lcd_override_seconds));
    const auto mode = static_cast<std::size_t>(snapshot.local_config.backlight_mode);
    if (mode >= local_control::kBacklightOptions.size() ||
        !enqueue_tracked(MqttPublishTracker::Field::lcd_mode, mode, topics_.lcd_mode_state.data(),
                         local_control::kBacklightOptions[mode]) ||
        !enqueue_tracked(MqttPublishTracker::Field::lcd_override,
                         snapshot.local_config.lcd_override_seconds,
                         topics_.lcd_override_state.data(), override_seconds.data()) ||
        !enqueue_tracked(MqttPublishTracker::Field::lcd_night, snapshot.lcd_night_active ? 1U : 0U,
                         topics_.lcd_night_state.data(),
                         snapshot.lcd_night_active ? "ON" : "OFF")) {
        return false;
    }

    std::array<char, 96> payload{};
    for (std::size_t index = 0; index < snapshot.channels.size(); ++index) {
        const MqttPublishTracker::Field field = MqttPublishTracker::channelField(index);
        const std::uint64_t fingerprint =
            static_cast<std::uint64_t>(snapshot.channels[index].brightness_percent) |
            (snapshot.channels[index].on ? (std::uint64_t{1} << 8U) : 0U);
        if (!publish_tracker_.needsPublish(field, fingerprint)) {
            continue;
        }
        const std::uint8_t brightness =
            mqtt_protocol::percentToBrightness(snapshot.channels[index].brightness_percent);
        const int written = std::snprintf(payload.data(), payload.size(),
                                          "{\"state\":\"%s\",\"brightness\":%u,"
                                          "\"color_mode\":\"brightness\"}",
                                          snapshot.channels[index].on ? "ON" : "OFF",
                                          static_cast<unsigned>(brightness));
        if ((written < 0) || (static_cast<std::size_t>(written) >= payload.size())) {
            continue;
        }
        if (!enqueue_tracked(field, fingerprint, topics_.state[index].data(), payload.data())) {
            return false;
        }
    }

    for (std::size_t index = 0; index < snapshot.pir_inputs.size(); ++index) {
        const std::uint64_t fingerprint = snapshot.pir_inputs[index].active ? 1U : 0U;
        if (!enqueue_tracked(MqttPublishTracker::pirField(index), fingerprint,
                             topics_.pir_state[index].data(),
                             snapshot.pir_inputs[index].active ? "ON" : "OFF")) {
            return false;
        }
    }

#if LED_CONTROLLER_INA228_ENABLED
    const Ina228Telemetry& ina = snapshot.ina228;
    const bool ina_available = ina.present && ina.valid;
    if (!enqueue_tracked(MqttPublishTracker::Field::ina_availability, ina_available ? 1U : 0U,
                         topics_.ina228_availability.data(), ina_available ? kOnline : kOffline)) {
        return false;
    }
    if (!ina_available) {
        publish_tracker_.invalidate(MqttPublishTracker::Field::ina_current);
        publish_tracker_.invalidate(MqttPublishTracker::Field::ina_bus_voltage);
        publish_tracker_.invalidate(MqttPublishTracker::Field::ina_power);
        publish_tracker_.invalidate(MqttPublishTracker::Field::ina_energy);
        publish_tracker_.invalidate(MqttPublishTracker::Field::ina_die_temperature);
    } else {
        std::array<char, 48> sensor_payload{};
        static_cast<void>(std::snprintf(sensor_payload.data(), sensor_payload.size(), "%" PRId32,
                                        ina.current_milliamps));
        if (!enqueue_tracked(
                MqttPublishTracker::Field::ina_current,
                static_cast<std::uint64_t>(static_cast<std::int64_t>(ina.current_milliamps)),
                topics_.ina228_current.data(), sensor_payload.data())) {
            return false;
        }

        static_cast<void>(std::snprintf(sensor_payload.data(), sensor_payload.size(), "%" PRIu32,
                                        ina.bus_voltage_millivolts));
        if (!enqueue_tracked(MqttPublishTracker::Field::ina_bus_voltage, ina.bus_voltage_millivolts,
                             topics_.ina228_bus_voltage.data(), sensor_payload.data())) {
            return false;
        }

        static_cast<void>(std::snprintf(sensor_payload.data(), sensor_payload.size(), "%" PRIu32,
                                        ina.power_milliwatts));
        if (!enqueue_tracked(MqttPublishTracker::Field::ina_power, ina.power_milliwatts,
                             topics_.ina228_power.data(), sensor_payload.data())) {
            return false;
        }

        static_cast<void>(std::snprintf(
            sensor_payload.data(), sensor_payload.size(), "%" PRIu32 ".%03" PRIu32,
            ina.energy_milliwatt_hours / 1'000U, ina.energy_milliwatt_hours % 1'000U));
        if (!enqueue_tracked(MqttPublishTracker::Field::ina_energy, ina.energy_milliwatt_hours,
                             topics_.ina228_energy.data(), sensor_payload.data())) {
            return false;
        }

        const std::int64_t temperature = ina.die_temperature_millicelsius;
        const std::uint64_t magnitude =
            static_cast<std::uint64_t>((temperature < 0) ? -temperature : temperature);
        static_cast<void>(std::snprintf(sensor_payload.data(), sensor_payload.size(),
                                        "%s%" PRIu64 ".%03" PRIu64, (temperature < 0) ? "-" : "",
                                        magnitude / 1'000U, magnitude % 1'000U));
        if (!enqueue_tracked(MqttPublishTracker::Field::ina_die_temperature,
                             static_cast<std::uint64_t>(temperature),
                             topics_.ina228_die_temperature.data(), sensor_payload.data())) {
            return false;
        }
    }
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    for (std::size_t index = 0; index < snapshot.temperatures.roles.size(); ++index) {
        const TemperatureRoleTelemetry& temperature = snapshot.temperatures.roles[index];
        const bool available = temperature.configured && temperature.present && temperature.valid;
        if (!enqueue_tracked(MqttPublishTracker::temperatureAvailabilityField(index),
                             available ? 1U : 0U, topics_.temperature_availability[index].data(),
                             available ? kOnline : kOffline)) {
            return false;
        }
        if (!available) {
            publish_tracker_.invalidate(MqttPublishTracker::temperatureValueField(index));
            continue;
        }
        std::array<char, 32> temperature_payload{};
        const std::int64_t value = temperature.temperature_millicelsius;
        const std::uint64_t magnitude = static_cast<std::uint64_t>((value < 0) ? -value : value);
        static_cast<void>(std::snprintf(temperature_payload.data(), temperature_payload.size(),
                                        "%s%" PRIu64 ".%03" PRIu64, (value < 0) ? "-" : "",
                                        magnitude / 1'000U, magnitude % 1'000U));
        if (!enqueue_tracked(MqttPublishTracker::temperatureValueField(index),
                             static_cast<std::uint64_t>(value),
                             topics_.temperature_state[index].data(), temperature_payload.data())) {
            return false;
        }
    }
#endif

    if (enqueue_error_reported_) {
        ESP_LOGI(kTag, "Kolejka publikacji MQTT ponownie gotowa");
        enqueue_error_reported_ = false;
    }
    publish_backlog_.store(false, std::memory_order_release);
    return true;
}

bool MqttService::publishOtaStatus() noexcept {
    const OtaService::Status ota = ota_service_.status();
    if (ota_status_published_ && (ota_status_revision_published_ == ota.revision)) {
        return true;
    }

    MutexGuard client_lock(client_mutex_);
    if (!client_lock.locked() || !connected_.load(std::memory_order_acquire) ||
        (client_ == nullptr)) {
        return false;
    }

    std::array<char, 224> payload{};
    const int written = std::snprintf(
        payload.data(), payload.size(),
        "{\"state\":\"%s\",\"version\":\"%s\",\"candidate\":\"%s\","
        "\"progress\":%u,\"error\":\"%s\"}",
        OtaService::stateName(ota.state), ota.current_version.data(), ota.candidate_version.data(),
        static_cast<unsigned>(ota.progress_percent), esp_err_to_name(ota.last_error));
    if ((written < 0) || (static_cast<std::size_t>(written) >= payload.size())) {
        return false;
    }
    if (!enqueueMessage(client_, topics_.ota_status.data(), payload.data(), true)) {
        return false;
    }
    ota_status_revision_published_ = ota.revision;
    ota_status_published_ = true;
    return true;
}

int MqttService::publishDiscovery(const esp_mqtt_client_handle_t client) noexcept {
    if (!discovery_payload_valid_) {
        DiscoveryHeapSamples heap_samples{};
        discovery_failed_allocation_bytes.store(0, std::memory_order_relaxed);
        discovery_build_active.store(true, std::memory_order_release);
        SyncError failure = SyncError::none;
        const bool built = buildDiscoveryPayload(failure, sampleDiscoveryHeap, &heap_samples);
        discovery_build_active.store(false, std::memory_order_release);
        logDiscoveryHeap("before_tree", heap_samples[0]);
        logDiscoveryHeap("after_tree", heap_samples[1]);
        logDiscoveryHeap("after_serialization", heap_samples[2]);
        logDiscoveryHeap("after_free", heap_samples[3]);
        ESP_LOGI(kTag,
                 "Discovery build: %s, stage=%s, JSON=%u/%u B, "
                 "failed_alloc_during_build=%u B, task stack minimum=%u",
                 built ? "OK" : "FAIL", syncErrorName(failure),
                 static_cast<unsigned>(discovery_payload_bytes_),
                 static_cast<unsigned>(discovery_payload_.size()),
                 static_cast<unsigned>(
                     discovery_failed_allocation_bytes.load(std::memory_order_relaxed)),
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        if (!built) {
            MutexGuard lock(mutex_);
            if (lock.locked()) {
                status_.last_sync_error = failure;
                status_.discovery_payload_bytes = 0;
            }
            return -1;
        }
        discovery_payload_valid_ = true;
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.discovery_payload_bytes = static_cast<std::uint32_t>(discovery_payload_bytes_);
        }
    }
    const std::size_t free_before_enqueue = heap_caps_get_free_size(kInternalHeapCaps);
    const std::size_t largest_before_enqueue = heap_caps_get_largest_free_block(kInternalHeapCaps);
    const int id =
        enqueueMessageId(client, topics_.discovery.data(), discovery_payload_.data(), true);
    ESP_LOGI(kTag,
             "Discovery enqueue: %s, JSON=%u B, heap free=%u->%u, largest=%u->%u, "
             "outbox=%d B",
             id > 0 ? "OK" : "FAIL", static_cast<unsigned>(discovery_payload_bytes_),
             static_cast<unsigned>(free_before_enqueue),
             static_cast<unsigned>(heap_caps_get_free_size(kInternalHeapCaps)),
             static_cast<unsigned>(largest_before_enqueue),
             static_cast<unsigned>(heap_caps_get_largest_free_block(kInternalHeapCaps)),
             esp_mqtt_client_get_outbox_size(client));
    if (id > 0) {
        unsigned sensor_count = 0U;
#if LED_CONTROLLER_INA228_ENABLED
        sensor_count += 5U;
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
        sensor_count += 2U;
#endif
        ESP_LOGI(kTag, "Home Assistant Discovery: %u encji (5 LED, 2 PIR, %u pomiarow)",
                 32U + sensor_count, sensor_count);
    } else {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.last_sync_error = SyncError::discovery_enqueue;
        }
    }
    return id;
}

bool MqttService::enqueueMessage(const esp_mqtt_client_handle_t client, const char* const topic,
                                 const char* const payload, const bool retain) noexcept {
    return enqueueMessageId(client, topic, payload, retain) > 0;
}

int MqttService::enqueueMessageId(const esp_mqtt_client_handle_t client, const char* const topic,
                                  const char* const payload, const bool retain) noexcept {
    if ((client == nullptr) || (topic == nullptr) || (payload == nullptr)) {
        return -1;
    }
    const int message_id = esp_mqtt_client_enqueue(client, topic, payload, 0, 1, retain, true);
    if (message_id < 0) {
        publish_backlog_.store(true, std::memory_order_release);
        if (!enqueue_error_reported_) {
            if (message_id == -2) {
                ESP_LOGW(kTag, "Outbox MQTT pelny; publikacja zostanie ponowiona: %s", topic);
            } else {
                ESP_LOGW(kTag, "Nie udalo sie zakolejkowac publikacji MQTT (kod %d): %s",
                         message_id, topic);
            }
            enqueue_error_reported_ = true;
        }
        return message_id;
    }
    return message_id;
}

esp_err_t MqttService::stopClient(const bool publish_offline) noexcept {
    MutexGuard client_lock(client_mutex_);
    if (!client_lock.locked()) {
        return ESP_FAIL;
    }
    if (client_ == nullptr) {
        connected_.store(false, std::memory_order_release);
        return ESP_OK;
    }

    if (publish_offline && connected_.load(std::memory_order_acquire)) {
        static_cast<void>(
            esp_mqtt_client_publish(client_, topics_.availability.data(), kOffline, 0, 1, true));
    }

    ESP_LOGI(kTag, "MQTT explicit stop/destroy; publish_offline=%s",
             publish_offline ? "tak" : "nie");
    esp_mqtt_client_handle_t old_client = client_;
    active_client_.store(nullptr, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    connection_generation_.fetch_add(1, std::memory_order_acq_rel);
    synchronization_requested_.store(false, std::memory_order_release);
    discovery_message_id_.store(0, std::memory_order_release);
    discovery_acknowledged_.store(false, std::memory_order_release);
    publish_backlog_.store(false, std::memory_order_release);
    {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.birth_queued = false;
            status_.subscriptions_ready = false;
            status_.subscriptions_done = 0;
            status_.discovery_queued = false;
            status_.discovery_confirmed = false;
            status_.outbox_bytes = 0;
        }
    }
    client_ = nullptr;

    esp_err_t result = client_started_ ? esp_mqtt_client_stop(old_client) : ESP_OK;
    client_started_ = false;
    start_retry_pending_ = false;
    const esp_err_t destroy_result = esp_mqtt_client_destroy(old_client);
    if (result == ESP_OK) {
        result = destroy_result;
    }
    return result;
}

esp_err_t MqttService::createClient() noexcept {
    MutexGuard client_lock(client_mutex_);
    if (!client_lock.locked() || (client_ != nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_mqtt_client_config_t config{};
    config.broker.address.uri = applied_config_.uri.data();
    config.credentials.username = applied_config_.username.data();
    config.credentials.client_id = device_id_.data();
    config.credentials.authentication.password = applied_config_.password.data();
    config.session.last_will.topic = topics_.availability.data();
    config.session.last_will.msg = kOffline;
    config.session.last_will.msg_len = 0;
    config.session.last_will.qos = 1;
    config.session.last_will.retain = true;
    config.session.keepalive = 30;
    config.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    config.network.reconnect_timeout_ms = static_cast<int>(kReconnectIntervalMs);
    config.network.timeout_ms = static_cast<int>(kNetworkTimeoutMs);
    config.task.priority = static_cast<int>(kServiceTaskPriority);
    config.task.stack_size = static_cast<int>(kServiceTaskStackSize);
    config.buffer.size = static_cast<int>(kMqttBufferLength);
    config.buffer.out_size = static_cast<int>(kMqttBufferLength);
    config.outbox.limit = kOutboxLimit;

    esp_mqtt_client_handle_t new_client = esp_mqtt_client_init(&config);
    if (new_client == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result =
        esp_mqtt_client_register_event(new_client, MQTT_EVENT_ANY, eventHandler, this);
    if (result != ESP_OK) {
        static_cast<void>(esp_mqtt_client_destroy(new_client));
        return result;
    }

    client_ = new_client;
    active_client_.store(new_client, std::memory_order_release);
    ESP_LOGI(kTag, "MQTT client created: YES; client started: NO; connected: NO");
    client_started_ = false;
    start_retry_pending_ = false;
    return ESP_OK;
}

void MqttService::startClientWhenNetworkReady() noexcept {
    // Never start/stop MQTT from the shared IP event loop. Only this service task starts it.
    if (!wifi_service_.networkReady()) {
        return;
    }
    MutexGuard client_lock(client_mutex_, 0);
    if (!client_lock.locked() || client_ == nullptr || client_started_) {
        return;
    }
    const TickType_t now = xTaskGetTickCount();
    if (start_retry_pending_ && (now - start_retry_tick_ < pdMS_TO_TICKS(kReconnectIntervalMs))) {
        return;
    }
    const char* authority = applied_config_.uri.data() + 7U;
    const std::size_t length = std::strcspn(authority, "/?#");
    const bool hidden = std::memchr(authority, '@', length) != nullptr;
    ESP_LOGI(kTag, "MQTT target=%.*s; Starting MQTT after GOT_IP",
             hidden ? 8 : static_cast<int>(length), hidden ? "[ukryte]" : authority);
    const esp_err_t result = esp_mqtt_client_start(client_);
    ESP_LOGI(kTag, "esp_mqtt_client_start() = %s", esp_err_to_name(result));
    client_started_ = result == ESP_OK;
    ESP_LOGI(kTag, "MQTT client created: YES; client started: %s; connected: %s",
             client_started_ ? "YES" : "NO",
             connected_.load(std::memory_order_acquire) ? "YES" : "NO");
    start_retry_pending_ = !client_started_;
    start_retry_tick_ = now;
}

bool MqttService::buildTopics(const RuntimeConfig& config) noexcept {
    const auto format = [](char* const destination, const std::size_t size,
                           const char* const pattern, const char* const first,
                           const char* const second) noexcept {
        const int written = std::snprintf(destination, size, pattern, first, second);
        return (written >= 0) && (static_cast<std::size_t>(written) < size);
    };
    const auto formatChannel = [](char* const destination, const std::size_t size,
                                  const char* const pattern, const char* const first,
                                  const char* const second, const unsigned channel) noexcept {
        const int written = std::snprintf(destination, size, pattern, first, second, channel);
        return (written >= 0) && (static_cast<std::size_t>(written) < size);
    };

    for (std::size_t i = 0; i < 10; ++i) {
        const int a =
            std::snprintf(topics_.pwm_command[i].data(), kTopicLength, "%s/%s/config/pwm/%u/%s/set",
                          config.topic_prefix.data(), device_id_.data(),
                          static_cast<unsigned>(4 + i / 5), pwm_config::kFields[i % 5]);
        const int b =
            std::snprintf(topics_.pwm_state[i].data(), kTopicLength, "%s/%s/config/pwm/%u/%s/state",
                          config.topic_prefix.data(), device_id_.data(),
                          static_cast<unsigned>(4 + i / 5), pwm_config::kFields[i % 5]);
        if (a < 0 || b < 0 || static_cast<std::size_t>(a) >= kTopicLength ||
            static_cast<std::size_t>(b) >= kTopicLength)
            return false;
    }
    if (!format(topics_.base.data(), topics_.base.size(), "%s/%s", config.topic_prefix.data(),
                device_id_.data()) ||
        !format(topics_.availability.data(), topics_.availability.size(), "%s/%s/availability",
                config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.discovery.data(), topics_.discovery.size(), "%s/device/%s/config",
                config.discovery_prefix.data(), device_id_.data()) ||
        !format(topics_.ota_command.data(), topics_.ota_command.size(), "%s/%s/ota/set",
                config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.ota_status.data(), topics_.ota_status.size(), "%s/%s/ota/status",
                config.topic_prefix.data(), device_id_.data())) {
        return false;
    }
    if (!format(topics_.debug_command.data(), kTopicLength, "%s/%s/debug/set",
                config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.debug_status.data(), kTopicLength, "%s/%s/debug/status",
                config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.debug_log.data(), kTopicLength, "%s/%s/debug/log",
                config.topic_prefix.data(), device_id_.data())) {
        return false;
    }
    if (!format(topics_.lcd_mode_command.data(), kTopicLength, "%s/%s/config/lcd_mode/set",
                config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.lcd_mode_state.data(), kTopicLength, "%s/%s/config/lcd_mode/state",
                config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.lcd_override_command.data(), kTopicLength, "%s/%s/config/lcd_override/set",
                config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.lcd_override_state.data(), kTopicLength, "%s/%s/config/lcd_override/state",
                config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.lcd_night_command.data(), kTopicLength, "%s/%s/lcd/night/set",
                config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.lcd_night_state.data(), kTopicLength, "%s/%s/lcd/night/state",
                config.topic_prefix.data(), device_id_.data())) {
        return false;
    }
    const int status_written =
        std::snprintf(topics_.home_assistant_status.data(), topics_.home_assistant_status.size(),
                      "%s/status", config.discovery_prefix.data());
    if ((status_written < 0) ||
        (static_cast<std::size_t>(status_written) >= topics_.home_assistant_status.size())) {
        return false;
    }

    for (std::size_t index = 0; index < kChannelCount + kPirInputCount; ++index) {
        const bool mapping = index < kChannelCount;
        const std::size_t setting_index = mapping ? index : index - kChannelCount;
        auto& command =
            mapping ? topics_.mapping_command[setting_index] : topics_.delay_command[setting_index];
        auto& state =
            mapping ? topics_.mapping_state[setting_index] : topics_.delay_state[setting_index];
        const char* const type = mapping ? "key" : "pir_delay";
        if (!formatChannel(command.data(), command.size(), "%s/config/%s/%u/set",
                           topics_.base.data(), type, static_cast<unsigned>(setting_index + 1U)) ||
            !formatChannel(state.data(), state.size(), "%s/config/%s/%u/state", topics_.base.data(),
                           type, static_cast<unsigned>(setting_index + 1U))) {
            return false;
        }
    }

    for (std::size_t index = 0; index < kChannelCount; ++index) {
        const unsigned channel = static_cast<unsigned>(index + 1U);
        if (!formatChannel(topics_.command[index].data(), topics_.command[index].size(),
                           "%s/%s/channel/%u/set", config.topic_prefix.data(), device_id_.data(),
                           channel) ||
            !formatChannel(topics_.state[index].data(), topics_.state[index].size(),
                           "%s/%s/channel/%u/state", config.topic_prefix.data(), device_id_.data(),
                           channel)) {
            return false;
        }
    }

    for (std::size_t index = 0; index < kPirInputCount; ++index) {
        const unsigned input = static_cast<unsigned>(index + 1U);
        if (!formatChannel(topics_.pir_state[index].data(), topics_.pir_state[index].size(),
                           "%s/%s/pir/%u/state", config.topic_prefix.data(), device_id_.data(),
                           input)) {
            return false;
        }
    }
#if LED_CONTROLLER_INA228_ENABLED
    if (!format(topics_.ina228_availability.data(), topics_.ina228_availability.size(),
                "%s/%s/ina228/availability", config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.ina228_current.data(), topics_.ina228_current.size(),
                "%s/%s/ina228/current_ma", config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.ina228_bus_voltage.data(), topics_.ina228_bus_voltage.size(),
                "%s/%s/ina228/bus_voltage_mv", config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.ina228_power.data(), topics_.ina228_power.size(), "%s/%s/ina228/power_mw",
                config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.ina228_energy.data(), topics_.ina228_energy.size(),
                "%s/%s/ina228/energy_wh", config.topic_prefix.data(), device_id_.data()) ||
        !format(topics_.ina228_die_temperature.data(), topics_.ina228_die_temperature.size(),
                "%s/%s/ina228/die_temperature_c", config.topic_prefix.data(), device_id_.data())) {
        return false;
    }
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    constexpr std::array<const char*, kTemperatureRoleCount> kRoleTopics{{
        "radiator",
        "enclosure",
    }};
    for (std::size_t index = 0; index < kRoleTopics.size(); ++index) {
        const int availability_written = std::snprintf(
            topics_.temperature_availability[index].data(),
            topics_.temperature_availability[index].size(), "%s/%s/temperature/%s/availability",
            config.topic_prefix.data(), device_id_.data(), kRoleTopics[index]);
        const int state_written =
            std::snprintf(topics_.temperature_state[index].data(),
                          topics_.temperature_state[index].size(), "%s/%s/temperature/%s/state_c",
                          config.topic_prefix.data(), device_id_.data(), kRoleTopics[index]);
        if ((availability_written < 0) ||
            (static_cast<std::size_t>(availability_written) >=
             topics_.temperature_availability[index].size()) ||
            (state_written < 0) ||
            (static_cast<std::size_t>(state_written) >= topics_.temperature_state[index].size())) {
            return false;
        }
    }
#endif
    return true;
}

bool MqttService::buildDiscoveryPayload(SyncError& failure, mqtt_discovery::StageObserver observer,
                                        void* observer_context) noexcept {
    mqtt_discovery::Topics topics{};
    topics.base = topics_.base.data();
    topics.availability = topics_.availability.data();
    topics.ota_status = topics_.ota_status.data();
    for (std::size_t index = 0; index < kChannelCount; ++index) {
        topics.command[index] = topics_.command[index].data();
        topics.state[index] = topics_.state[index].data();
    }
    for (std::size_t index = 0; index < kPirInputCount; ++index) {
        topics.pir_state[index] = topics_.pir_state[index].data();
    }
#if LED_CONTROLLER_INA228_ENABLED
    topics.ina228_availability = topics_.ina228_availability.data();
    topics.ina228_state = {{topics_.ina228_current.data(), topics_.ina228_bus_voltage.data(),
                            topics_.ina228_power.data(), topics_.ina228_energy.data(),
                            topics_.ina228_die_temperature.data()}};
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    for (std::size_t index = 0; index < kTemperatureRoleCount; ++index) {
        topics.temperature_availability[index] = topics_.temperature_availability[index].data();
        topics.temperature_state[index] = topics_.temperature_state[index].data();
    }
#endif
    discovery_payload_bytes_ = 0;
    const auto result = mqtt_discovery::build(
        topics, device_id_.data(), esp_app_get_description()->version, discovery_payload_.data(),
        discovery_payload_.size(), observer, observer_context);
    discovery_payload_bytes_ = result.bytes;
    switch (result.failure) {
    case mqtt_discovery::Failure::none:
        failure = SyncError::none;
        break;
    case mqtt_discovery::Failure::root:
        failure = SyncError::discovery_root;
        break;
    case mqtt_discovery::Failure::local:
        failure = SyncError::discovery_local;
        break;
    case mqtt_discovery::Failure::debug:
        failure = SyncError::discovery_debug;
        break;
    case mqtt_discovery::Failure::component:
        failure = SyncError::discovery_component;
        break;
    case mqtt_discovery::Failure::buffer:
        failure = SyncError::discovery_buffer;
        break;
    }
    return result.ok();
}

void MqttService::updateStatusFromConfigurationLocked() noexcept {
    status_.configuration_ready = configurationIsValid(staged_config_);
    status_.password_set = staged_config_.password[0] != '\0';
    status_.pending_changes =
        status_.configuration_applied && !runtimeConfigsEqual(staged_config_, applied_config_);
    copyText(status_.uri.data(), status_.uri.size(), staged_config_.uri.data(), kMaxUriLength);
    copyText(status_.username.data(), status_.username.size(), staged_config_.username.data(),
             kMaxUsernameLength);
    copyText(status_.topic_prefix.data(), status_.topic_prefix.size(),
             staged_config_.topic_prefix.data(), kMaxTopicPrefixLength);
    copyText(status_.discovery_prefix.data(), status_.discovery_prefix.size(),
             staged_config_.discovery_prefix.data(), kMaxDiscoveryPrefixLength);
    if (!status_.configuration_applied) {
        status_.state = status_.configuration_ready ? State::ready : State::unconfigured;
    }
}

void MqttService::noteRejectedCommand() noexcept {
    rejected_commands_.fetch_add(1, std::memory_order_relaxed);
}

void MqttService::noteAcceptedCommand() noexcept {
    accepted_commands_.fetch_add(1, std::memory_order_relaxed);
}

void MqttService::recordPwmCommandResult(const bool applied) noexcept {
    if (applied) {
        noteAcceptedCommand();
    } else {
        noteRejectedCommand();
    }
}

bool MqttService::configurationIsValid(const RuntimeConfig& config) const noexcept {
    return (std::strncmp(config.uri.data(), "mqtt://", 7) == 0) && (config.username[0] != '\0') &&
           topicPrefixIsValid(config.topic_prefix.data(), kMaxTopicPrefixLength) &&
           topicPrefixIsValid(config.discovery_prefix.data(), kMaxDiscoveryPrefixLength);
}

bool MqttService::topicPrefixIsValid(const char* const prefix,
                                     const std::size_t maximum_length) noexcept {
    if (prefix == nullptr) {
        return false;
    }
    const std::size_t length = std::strlen(prefix);
    if ((length == 0U) || (length > maximum_length) || (prefix[0] == '/') ||
        (prefix[length - 1U] == '/')) {
        return false;
    }

    char previous = '\0';
    for (std::size_t index = 0; index < length; ++index) {
        const unsigned char character = static_cast<unsigned char>(prefix[index]);
        if ((character < 0x21U) || (character > 0x7EU) || (character == '#') ||
            (character == '+') || ((character == '/') && (previous == '/'))) {
            return false;
        }
        previous = static_cast<char>(character);
    }
    return true;
}

bool MqttService::runtimeConfigsEqual(const RuntimeConfig& left,
                                      const RuntimeConfig& right) noexcept {
    return (std::strcmp(left.uri.data(), right.uri.data()) == 0) &&
           (std::strcmp(left.username.data(), right.username.data()) == 0) &&
           (std::strcmp(left.password.data(), right.password.data()) == 0) &&
           (std::strcmp(left.topic_prefix.data(), right.topic_prefix.data()) == 0) &&
           (std::strcmp(left.discovery_prefix.data(), right.discovery_prefix.data()) == 0);
}

bool MqttService::topicEquals(const char* const topic, const int topic_length,
                              const char* const expected) noexcept {
    if ((topic == nullptr) || (expected == nullptr) || (topic_length < 0)) {
        return false;
    }
    const std::size_t expected_length = std::strlen(expected);
    return (static_cast<std::size_t>(topic_length) == expected_length) &&
           (std::memcmp(topic, expected, expected_length) == 0);
}

void MqttService::copyText(char* const destination, const std::size_t destination_size,
                           const char* const source, const std::size_t source_limit) noexcept {
    if ((destination == nullptr) || (destination_size == 0U)) {
        return;
    }
    destination[0] = '\0';
    if (source == nullptr) {
        return;
    }
    const std::size_t length = std::min(std::strlen(source), source_limit);
    const std::size_t copy_length = std::min(length, destination_size - 1U);
    std::memcpy(destination, source, copy_length);
    destination[copy_length] = '\0';
}

void MqttService::secureZero(void* const data, const std::size_t size) noexcept {
    auto* output = static_cast<volatile std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        output[index] = 0;
    }
}

void MqttService::serviceDebug() noexcept {
    DebugLog::Status status{};
    if (!DebugLog::instance().status(status)) {
        return;
    }
    MutexGuard lock(client_mutex_, 0);
    if (!lock.locked() || client_ == nullptr || !connected_.load(std::memory_order_acquire)) {
        return;
    }
    const auto now_seconds = DebugLog::seconds();
    const auto stack = stack_monitor::summary();
    const auto level = static_cast<unsigned>(stack.record.severity());
    const bool escalated = stack_status_published_ && level > stack_level_published_;
    if (stack.complete && (!stack_status_published_ || escalated ||
                           ((stack.record.minimum != stack_minimum_published_ ||
                             stack.measured != stack_measured_published_) &&
                            now_seconds - stack_publish_seconds_ >= 60U))) {
        if (stack_boot_id_ == 0) {
            stack_boot_id_ = esp_random();
            if (stack_boot_id_ == 0)
                stack_boot_id_ = 1;
        }
        const int topic_length = std::snprintf(stack_topic_.data(), stack_topic_.size(),
                                               "%s/stack/status", topics_.base.data());
        const int length = std::snprintf(
            stack_payload_.data(), stack_payload_.size(),
            "{\"level\":\"%s\",\"task\":\"%s\",\"minimum\":%lu,\"budget\":%lu,"
            "\"used_peak\":%lu,\"sampled_at\":%lu,\"minimum_at\":%lu,\"uptime_s\":%lu,"
            "\"boot_id\":%lu,\"measured_tasks\":%u,\"version\":\"%s\",\"reset_reason\":\"%s\","
            "\"heap_free\":%u,\"heap_minimum\":%u,\"heap_largest\":%u}",
            stack_model::name(stack.record.severity()), stack_monitor::name(stack.task),
            static_cast<unsigned long>(stack.record.minimum),
            static_cast<unsigned long>(stack.record.budget),
            static_cast<unsigned long>(stack.record.budget - stack.record.minimum),
            static_cast<unsigned long>(stack.record.sampled_at),
            static_cast<unsigned long>(stack.record.minimum_at),
            static_cast<unsigned long>(now_seconds), static_cast<unsigned long>(stack_boot_id_),
            stack.measured, esp_app_get_description()->version, DebugLog::resetReason(),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(
                heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        if (topic_length > 0 && static_cast<std::size_t>(topic_length) < stack_topic_.size() &&
            length > 0 && static_cast<std::size_t>(length) < stack_payload_.size() &&
            enqueueMessage(client_, stack_topic_.data(), stack_payload_.data(), true)) {
            if (level > stack_logged_level_ && level >= 2U) {
                stack_logged_level_ = level;
                ESP_LOGW(kTag, "Stack %s: %s minimum=%lu B budget=%lu B",
                         stack_model::name(stack.record.severity()),
                         stack_monitor::name(stack.task),
                         static_cast<unsigned long>(stack.record.minimum),
                         static_cast<unsigned long>(stack.record.budget));
            }
            stack_measured_published_ = stack.measured;
            stack_status_published_ = true;
            stack_level_published_ = level;
            stack_minimum_published_ = stack.record.minimum;
            stack_publish_seconds_ = now_seconds;
        }
    }

    if (!debug_status_published_ || status.active != debug_active_published_ ||
        static_cast<std::uint32_t>(now_seconds - debug_status_seconds_) >= 10U) {
        std::array<char, 256> payload{};
        const int length = std::snprintf(payload.data(), payload.size(),
                                         "{\"debug\":\"%s\",\"reset_reason\":\"%s\",\"uptime_s\":%"
                                         "lu,\"dropped\":%lu,\"version\":\"%s\"}",
                                         status.active ? "ON" : "OFF", DebugLog::resetReason(),
                                         static_cast<unsigned long>(now_seconds),
                                         static_cast<unsigned long>(status.dropped),
                                         esp_app_get_description()->version);
        if (length > 0 && static_cast<std::size_t>(length) < payload.size() &&
            enqueueMessage(client_, topics_.debug_status.data(), payload.data(), true)) {
            debug_status_published_ = true;
            debug_active_published_ = status.active;
            debug_status_seconds_ = now_seconds;
        }
    }
    const TickType_t now = xTaskGetTickCount();
    if (!status.active || (now - debug_log_tick_) < pdMS_TO_TICKS(200) ||
        esp_mqtt_client_get_outbox_size(client_) > 8192) {
        return;
    }
    DebugBuffer::Message message{};
    if (DebugLog::instance().pop(message)) {
        debug_log_tick_ = now;
        // Low-priority best-effort stream; never retain or retry a log forever.
        if (esp_mqtt_client_enqueue(client_, topics_.debug_log.data(), message.data(), 0, 0, false,
                                    true) < 0) {
            DebugLog::instance().noteTransportDrop();
        }
    }
}
