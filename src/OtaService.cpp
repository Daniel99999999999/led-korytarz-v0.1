#include "OtaService.hpp"

#include "DeviceIdentity.hpp"
#include "StackMonitor.hpp"
#include "WifiService.hpp"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "psa/crypto.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <strings.h>
#include <type_traits>

namespace {
constexpr char kTag[] = "ota_service";

static_assert(std::is_trivially_copyable_v<OtaService::Status>);

class MutexGuard final {
  public:
    explicit MutexGuard(const SemaphoreHandle_t mutex,
                        const TickType_t timeout = portMAX_DELAY) noexcept
        : mutex_(mutex), locked_((mutex != nullptr) && (xSemaphoreTake(mutex, timeout) == pdTRUE)) {
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
    bool locked_;
};

void copyText(char* const destination, const std::size_t destination_size,
              const char* const source) noexcept {
    if ((destination == nullptr) || (destination_size == 0U)) {
        return;
    }
    destination[0] = '\0';
    if (source == nullptr) {
        return;
    }
    const std::size_t length = std::min(std::strlen(source), destination_size - 1U);
    std::memcpy(destination, source, length);
    destination[length] = '\0';
}
} // namespace

esp_err_t OtaService::initialize() noexcept {
    if (initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    request_queue_ = xQueueCreateStatic(1, sizeof(Request), request_queue_buffer_.data(),
                                        &request_queue_storage_);
    if ((mutex_ == nullptr) || (request_queue_ == nullptr)) {
        return ESP_ERR_NO_MEM;
    }

    copyText(status_.current_version.data(), status_.current_version.size(),
             esp_app_get_description()->version);
    status_.revision = 1U;

    const esp_partition_t* const running_partition = esp_ota_get_running_partition();
    const esp_partition_t* const update_partition = esp_ota_get_next_update_partition(nullptr);
    if ((running_partition == nullptr) || (update_partition == nullptr) ||
        (running_partition == update_partition)) {
        status_.state = State::unavailable;
        status_.last_error = ESP_ERR_NOT_SUPPORTED;
        ESP_LOGE(kTag, "Brak dwoch partycji aplikacji OTA");
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_ota_img_states_t image_state{};
    const esp_err_t state_result = esp_ota_get_state_partition(running_partition, &image_state);
    validation_pending_ = (state_result == ESP_OK) && (image_state == ESP_OTA_IMG_PENDING_VERIFY);
    status_.state = validation_pending_ ? State::validating : State::idle;
    status_.last_error = ESP_OK;
    initialized_ = true;

    ESP_LOGI(kTag, "OTA HTTPS gotowe; aktywna=%s, nastepna=%s%s", running_partition->label,
             update_partition->label,
             validation_pending_ ? "; nowy obraz czeka na weryfikacje" : "");
    return ESP_OK;
}

esp_err_t OtaService::start() noexcept {
    if (!initialized_ || started_ || (task_handle_ != nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    const BaseType_t result =
        xTaskCreate(taskEntry, "ota_service", kTaskStackSize, this, kTaskPriority, &task_handle_);
    if (result != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    started_ = true;
    publishCurrentStatus();
    return ESP_OK;
}

esp_err_t OtaService::requestUpdate(const char* const url, const std::size_t length) noexcept {
    const ota_request::ValidationResult validation = ota_request::validate(url, length);
    if (validation != ota_request::ValidationResult::ok) {
        ESP_LOGW(kTag, "Odrzucono adres OTA: %s", ota_request::resultName(validation));
        return ESP_ERR_INVALID_ARG;
    }
    if (!initialized_ || !started_) {
        return ESP_ERR_INVALID_STATE;
    }

    {
        MutexGuard lock(mutex_);
        if (!lock.locked() ||
            ((status_.state != State::idle) && (status_.state != State::failed))) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    bool expected = false;
    if (!request_in_flight_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return ESP_ERR_INVALID_STATE;
    }

    Request request{};
    std::memcpy(request.url.data(), url, length);
    request.url[length] = '\0';
    updateStatus(State::queued, 0U, ESP_OK);
    if (xQueueSend(request_queue_, &request, 0) != pdPASS) {
        request_in_flight_.store(false, std::memory_order_release);
        updateStatus(State::failed, 0U, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(kTag, "Przyjeto bezpieczna komende OTA HTTPS");
    return ESP_OK;
}

esp_err_t OtaService::beginLocalUpdate() noexcept {
    if (!initialized_ || !started_)
        return ESP_ERR_INVALID_STATE;
    {
        MutexGuard lock(mutex_);
        if (!lock.locked() || (status_.state != State::idle && status_.state != State::failed))
            return ESP_ERR_INVALID_STATE;
    }
    bool expected = false;
    if (!request_in_flight_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return ESP_ERR_INVALID_STATE;
    updateStatus(State::downloading, 0, ESP_OK);
    return ESP_OK;
}
void OtaService::finishLocalUpdate(const esp_err_t result, const char* version) noexcept {
    updateStatus(result == ESP_OK ? State::rebooting : State::failed, result == ESP_OK ? 100 : 0,
                 result, version);
    if (result != ESP_OK)
        request_in_flight_.store(false, std::memory_order_release);
}

esp_err_t OtaService::setStatusCallback(const StatusCallback callback,
                                        void* const context) noexcept {
    if (mutex_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    {
        MutexGuard lock(mutex_);
        if (!lock.locked()) {
            return ESP_FAIL;
        }
        status_callback_ = callback;
        status_callback_context_ = context;
    }
    publishCurrentStatus();
    return ESP_OK;
}

OtaService::Status OtaService::status() const noexcept {
    Status snapshot{};
    MutexGuard lock(mutex_);
    if (lock.locked()) {
        snapshot = status_;
    }
    return snapshot;
}

const char* OtaService::stateName(const State state) noexcept {
    switch (state) {
    case State::unavailable:
        return "unavailable";
    case State::validating:
        return "validating";
    case State::idle:
        return "idle";
    case State::queued:
        return "queued";
    case State::downloading:
        return "downloading";
    case State::verifying:
        return "verifying";
    case State::rebooting:
        return "rebooting";
    case State::failed:
        return "failed";
    }
    return "unknown";
}

void OtaService::taskEntry(void* const context) noexcept {
    static_cast<OtaService*>(context)->taskLoop();
}

[[noreturn]] void OtaService::taskLoop() noexcept {
    if (validation_pending_) {
        const TickType_t validation_started = xTaskGetTickCount();
        const TickType_t validation_delay = pdMS_TO_TICKS(kValidationDelayMs);
        for (;;) {
            stack_monitor::sample(stack_monitor::Task::ota, kTaskStackSize);
            const TickType_t elapsed = xTaskGetTickCount() - validation_started;
            if (elapsed >= validation_delay) {
                break;
            }
            static_cast<void>(ulTaskNotifyTake(pdTRUE, validation_delay - elapsed));
        }

        const esp_err_t validation_result = validatePendingImage();
        if (validation_result != ESP_OK) {
            updateStatus(State::unavailable, 0U, validation_result);
            ESP_LOGE(kTag, "Nie udalo sie zatwierdzic nowego obrazu: %s",
                     esp_err_to_name(validation_result));
        } else {
            validation_pending_ = false;
            updateStatus(State::idle, 0U, ESP_OK);
            ESP_LOGI(kTag, "Nowy obraz pracuje stabilnie; rollback anulowany");
        }
    }

    next_auto_check_s_ =
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000000) + 120U + esp_random() % 181U;
    {
        MutexGuard lock(mutex_);
        status_.auto_next_check_s = static_cast<std::uint32_t>(next_auto_check_s_);
    }
    for (;;) {
        stack_monitor::sample(stack_monitor::Task::ota, kTaskStackSize);
        Request request{};
        if (xQueueReceive(request_queue_, &request, pdMS_TO_TICKS(10000)) != pdPASS) {
            checkAutomaticUpdate();
            continue;
        }
        const esp_err_t result = performUpdate(request);
        request_in_flight_.store(false, std::memory_order_release);
        if (result != ESP_OK) {
            updateStatus(State::failed, 0U, result);
            ESP_LOGE(kTag, "Aktualizacja OTA nie powiodla sie: %s", esp_err_to_name(result));
        }
    }
}

esp_err_t OtaService::validatePendingImage() noexcept {
    return esp_ota_mark_app_valid_cancel_rollback();
}

esp_err_t OtaService::performUpdate(const Request& request) noexcept {
    stack_monitor::Scope stack_sample(stack_monitor::Task::ota, kTaskStackSize);
    updateStatus(State::downloading, 0U, ESP_OK);

    esp_http_client_config_t http_config{};
    http_config.url = request.url.data();
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
    http_config.timeout_ms = static_cast<int>(kHttpTimeoutMs);
    http_config.keep_alive_enable = true;
    http_config.buffer_size = 4'096;
    http_config.buffer_size_tx = 1'024;

    esp_https_ota_config_t ota_config{};
    ota_config.http_config = &http_config;
    ota_config.bulk_flash_erase = false;

    esp_https_ota_handle_t ota_handle = nullptr;
    esp_err_t result = esp_https_ota_begin(&ota_config, &ota_handle);
    if (result != ESP_OK) {
        return result;
    }

    esp_app_desc_t candidate{};
    result = esp_https_ota_get_img_desc(ota_handle, &candidate);
    if (result != ESP_OK) {
        static_cast<void>(esp_https_ota_abort(ota_handle));
        return result;
    }

    const esp_app_desc_t* const current = esp_app_get_description();
    if (std::strncmp(candidate.project_name, current->project_name,
                     sizeof(candidate.project_name)) != 0) {
        ESP_LOGE(kTag, "Obraz ma inny projekt: %.32s", candidate.project_name);
        static_cast<void>(esp_https_ota_abort(ota_handle));
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (std::strncmp(candidate.version, current->version, sizeof(candidate.version)) == 0) {
        ESP_LOGW(kTag, "Odrzucono ponowna instalacje tej samej wersji %.32s", candidate.version);
        static_cast<void>(esp_https_ota_abort(ota_handle));
        return ESP_ERR_INVALID_VERSION;
    }

    if (request.automatic && std::strncmp(candidate.version, request.manifest.version.data(),
                                          sizeof(candidate.version)) != 0) {
        static_cast<void>(esp_https_ota_abort(ota_handle));
        return ESP_ERR_INVALID_VERSION;
    }
    updateStatus(State::downloading, 0U, ESP_OK, candidate.version);
    ESP_LOGI(kTag, "Pobieranie firmware %.32s dla projektu %.32s", candidate.version,
             candidate.project_name);

    std::uint8_t last_progress = 0U;
    do {
        result = esp_https_ota_perform(ota_handle);
        if (result == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            const int downloaded = esp_https_ota_get_image_len_read(ota_handle);
            const int total = esp_https_ota_get_image_size(ota_handle);
            if ((downloaded >= 0) && (total > 0)) {
                const int calculated = std::min(99, (downloaded * 100) / total);
                const auto progress = static_cast<std::uint8_t>(calculated);
                if (progress >= static_cast<std::uint8_t>(last_progress + kProgressStepPercent)) {
                    last_progress = progress;
                    updateStatus(State::downloading, progress, ESP_OK, candidate.version);
                }
            }
        }
    } while (result == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    if (result != ESP_OK) {
        static_cast<void>(esp_https_ota_abort(ota_handle));
        return result;
    }
    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        static_cast<void>(esp_https_ota_abort(ota_handle));
        return ESP_ERR_INVALID_SIZE;
    }

    if (request.automatic) {
        result = verifyAutomaticImage(request, esp_https_ota_get_image_len_read(ota_handle));
        if (result != ESP_OK) {
            static_cast<void>(esp_https_ota_abort(ota_handle));
            return result;
        }
    }
    updateStatus(State::verifying, 100U, ESP_OK, candidate.version);
    result = esp_https_ota_finish(ota_handle);
    if (result != ESP_OK) {
        return result;
    }

    updateStatus(State::rebooting, 100U, ESP_OK, candidate.version);
    ESP_LOGI(kTag, "Obraz zweryfikowany; restart do wersji %.32s", candidate.version);
    static_cast<void>(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kRebootStatusDelayMs)));
    stack_monitor::sample(stack_monitor::Task::ota, kTaskStackSize, true);
    esp_restart();
    return ESP_OK;
}

void OtaService::updateStatus(const State state, const std::uint8_t progress_percent,
                              const esp_err_t error, const char* const candidate_version) noexcept {
    Status snapshot{};
    StatusCallback callback = nullptr;
    void* callback_context = nullptr;
    {
        MutexGuard lock(mutex_);
        if (!lock.locked()) {
            return;
        }
        status_.state = state;
        status_.progress_percent = progress_percent;
        status_.last_error = error;
        if (candidate_version != nullptr) {
            copyText(status_.candidate_version.data(), status_.candidate_version.size(),
                     candidate_version);
        } else if ((state == State::idle) || (state == State::queued) ||
                   (state == State::unavailable)) {
            status_.candidate_version.fill('\0');
        }
        ++status_.revision;
        if (status_.revision == 0U) {
            status_.revision = 1U;
        }
        snapshot = status_;
        callback = status_callback_;
        callback_context = status_callback_context_;
    }
    if (callback != nullptr) {
        callback(callback_context, snapshot);
    }
}

void OtaService::publishCurrentStatus() noexcept {
    Status snapshot{};
    StatusCallback callback = nullptr;
    void* callback_context = nullptr;
    {
        MutexGuard lock(mutex_);
        if (!lock.locked()) {
            return;
        }
        snapshot = status_;
        callback = status_callback_;
        callback_context = status_callback_context_;
    }
    if (callback != nullptr) {
        callback(callback_context, snapshot);
    }
}

esp_err_t OtaService::manifestEvent(esp_http_client_event_t* event) noexcept {
    auto& self = *static_cast<OtaService*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        strcasecmp(event->header_key, "Location") == 0) {
        self.manifest_redirect_secure_ = std::strncmp(event->header_value, "https://", 8) == 0 &&
                                         std::strlen(event->header_value) <= 4096;
    }
    return ESP_OK;
}

esp_err_t OtaService::fetchManifest(auto_ota::Manifest& manifest) noexcept {
    std::array<char, 192> url{};
    std::snprintf(url.data(), url.size(), "%s/latest/download/release.json",
                  auto_ota::kReleaseBase);
    esp_http_client_config_t config{};
    config.url = url.data();
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = static_cast<int>(kHttpTimeoutMs);
    config.buffer_size = 1024;
    config.buffer_size_tx = 1024;
    config.disable_auto_redirect = true;
    config.event_handler = manifestEvent;
    config.user_data = this;
    config.user_agent = "led-controller-auto-ota";
    auto client = esp_http_client_init(&config);
    if (!client)
        return ESP_ERR_NO_MEM;
    esp_err_t result = ESP_ERR_INVALID_RESPONSE;
    std::size_t length = 0;
    const auto deadline = esp_timer_get_time() + 60000000;
    for (unsigned redirect = 0; redirect <= 5; ++redirect) {
        manifest_redirect_secure_ = false;
        result = esp_http_client_open(client, 0);
        if (result != ESP_OK)
            break;
        const auto content_length = esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (content_length < 0 || esp_timer_get_time() >= deadline) {
            result = ESP_ERR_TIMEOUT;
            break;
        }
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (!manifest_redirect_secure_ || redirect == 5) {
                result = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            result = esp_http_client_set_redirection(client);
            static_cast<void>(esp_http_client_close(client));
            if (result != ESP_OK)
                break;
            continue;
        }
        if (status != 200 || content_length > static_cast<std::int64_t>(auto_ota::kManifestLimit)) {
            result = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        while (length < manifest_buffer_.size()) {
            const int count =
                esp_http_client_read(client, manifest_buffer_.data() + length,
                                     static_cast<int>(manifest_buffer_.size() - length));
            if (count < 0) {
                result = ESP_FAIL;
                break;
            }
            length += static_cast<std::size_t>(count);
            if (length > auto_ota::kManifestLimit) {
                result = ESP_ERR_INVALID_SIZE;
                break;
            }
            if (esp_timer_get_time() >= deadline) {
                result = ESP_ERR_TIMEOUT;
                break;
            }
            if (count == 0) {
                result = esp_http_client_is_complete_data_received(client) ? ESP_OK
                                                                           : ESP_ERR_INVALID_SIZE;
                break;
            }
        }
        break;
    }
    static_cast<void>(esp_http_client_close(client));
    static_cast<void>(esp_http_client_cleanup(client));
    if (result == ESP_OK && !auto_ota::parseManifest(manifest_buffer_.data(), length, manifest))
        result = ESP_ERR_INVALID_RESPONSE;
    return result;
}

esp_err_t OtaService::verifyAutomaticImage(const Request& request, int received) noexcept {
    if (received < 0 || static_cast<std::uint32_t>(received) != request.manifest.size)
        return ESP_ERR_INVALID_SIZE;
    const auto* partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition || request.manifest.size > partition->size)
        return ESP_ERR_INVALID_SIZE;
    if (psa_crypto_init() != PSA_SUCCESS)
        return ESP_FAIL;
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&operation, PSA_ALG_SHA_256) != PSA_SUCCESS)
        return ESP_FAIL;
    std::array<std::uint8_t, 512> block{};
    esp_err_t result = ESP_OK;
    for (std::uint32_t offset = 0; offset < request.manifest.size;) {
        const auto count =
            std::min(block.size(), static_cast<std::size_t>(request.manifest.size - offset));
        result = esp_partition_read(partition, offset, block.data(), count);
        if (result != ESP_OK)
            break;
        if (psa_hash_update(&operation, block.data(), count) != PSA_SUCCESS) {
            result = ESP_FAIL;
            break;
        }
        offset += static_cast<std::uint32_t>(count);
        if ((offset % 16384U) == 0)
            static_cast<void>(ulTaskNotifyTake(pdTRUE, 1));
    }
    std::array<std::uint8_t, 32> digest{};
    std::size_t length = 0;
    if (result == ESP_OK &&
        psa_hash_finish(&operation, digest.data(), digest.size(), &length) != PSA_SUCCESS)
        result = ESP_FAIL;
    static_cast<void>(psa_hash_abort(&operation));
    if (result == ESP_OK && (length != digest.size() || digest != request.manifest.sha256))
        result = ESP_ERR_INVALID_CRC;
    return result;
}

void OtaService::checkAutomaticUpdate() noexcept {
    const auto now = static_cast<std::uint64_t>(esp_timer_get_time() / 1000000);
    // Offline / unsynchronized clock does not create TLS attempts or writes to NVS.
    if (validation_pending_ || now < next_auto_check_s_ || !wifi_.networkReady() ||
        std::time(nullptr) < 1704067200)
        return;
    bool expected = false;
    if (!request_in_flight_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;
    {
        MutexGuard lock(mutex_);
        status_.auto_checking = true;
        status_.auto_last_check_s = static_cast<std::uint32_t>(now);
        ++status_.auto_checks;
    }
    ESP_LOGI(kTag, "Auto OTA: sprawdzanie stabilnego wydania GitHub");
    Request request{};
    request.automatic = true;
    esp_err_t result = fetchManifest(request.manifest);
    if (result == ESP_OK &&
        auto_ota::newer(request.manifest.version.data(), esp_app_get_description()->version)) {
        esp_app_desc_t invalid{};
        const auto* invalid_partition = esp_ota_get_last_invalid_partition();
        const bool rejected =
            invalid_partition &&
            esp_ota_get_partition_description(invalid_partition, &invalid) == ESP_OK &&
            std::strncmp(invalid.version, request.manifest.version.data(),
                         sizeof(invalid.version)) == 0;
        if (rejected) {
            ESP_LOGW(kTag, "Auto OTA: pominieto wersje odrzucona przez rollback %.32s",
                     invalid.version);
        } else {
            std::snprintf(request.url.data(), request.url.size(), "%s/download/v%s/firmware.bin",
                          auto_ota::kReleaseBase, request.manifest.version.data());
            ESP_LOGI(kTag, "Auto OTA: instalacja %s", request.manifest.version.data());
            result = performUpdate(request);
            if (result != ESP_OK)
                updateStatus(State::failed, 0U, result);
        }
    }
    if (result == ESP_OK)
        auto_failures_ = 0;
    else if (auto_failures_ < 6)
        ++auto_failures_;
    const auto interval =
        result == ESP_OK ? auto_ota::kIntervalSeconds : auto_ota::retrySeconds(auto_failures_);
    next_auto_check_s_ =
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000000) + interval + esp_random() % 901U;
    {
        MutexGuard lock(mutex_);
        status_.auto_checking = false;
        status_.auto_error = result;
        status_.auto_next_check_s = static_cast<std::uint32_t>(next_auto_check_s_);
        ++status_.revision;
    }
    request_in_flight_.store(false, std::memory_order_release);
    publishCurrentStatus();
    ESP_LOGI(kTag, "Auto OTA: wynik=%s; nastepna proba za %lu s (+ jitter)",
             esp_err_to_name(result), static_cast<unsigned long>(interval));
}
