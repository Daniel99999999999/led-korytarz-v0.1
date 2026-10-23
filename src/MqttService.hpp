#pragma once

#include "FeatureConfig.hpp"
#include "LocalControlConfig.hpp"
#include "MqttDiscoveryPayload.hpp"
#include "MqttPublishTracker.hpp"
#include "MqttSyncMachine.hpp"
#include "OtaService.hpp"
#if LED_CONTROLLER_AT24C08_ENABLED
#include "PersistentSettings.hpp"
#endif
#if LED_CONTROLLER_INA228_ENABLED
#include "Ina228Telemetry.hpp"
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
#include "TemperatureTelemetry.hpp"
#endif
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class LedController;
class WifiService;

class MqttService final {
  public:
    static constexpr std::size_t kChannelCount = 5;
    static constexpr std::size_t kPirInputCount = 2;
    static constexpr std::size_t kMaxUriLength = 127;
    static constexpr std::size_t kMaxUsernameLength = 63;
    static constexpr std::size_t kMaxPasswordLength = 127;
    static constexpr std::size_t kMaxTopicPrefixLength = 63;
    static constexpr std::size_t kMaxDiscoveryPrefixLength = 31;

    enum class State : std::uint8_t {
        unavailable,
        unconfigured,
        ready,
        connecting,
        connected,
        disconnected,
        authentication_error,
    };

    enum class SyncError : std::uint8_t {
        none,
        subscribe_send,
        subscribe_rejected,
        subscribe_timeout,
        discovery_puback_timeout,
        birth_enqueue,
        discovery_root,
        discovery_local,
        discovery_debug,
        discovery_component,
        discovery_buffer,
        discovery_enqueue,
    };

    [[nodiscard]] static const char* syncErrorName(SyncError error) noexcept;

    struct ChannelState {
        std::uint8_t brightness_percent{100};
        bool on{false};
    };

    using ChannelSnapshot = std::array<ChannelState, kChannelCount>;

    struct PirInputState {
        bool active{false};
    };

    using PirInputSnapshot = std::array<PirInputState, kPirInputCount>;

    struct Snapshot {
        ChannelSnapshot channels{};
        PirInputSnapshot pir_inputs{};
        local_control::Config local_config{};
        bool lcd_night_active{false};
#if LED_CONTROLLER_INA228_ENABLED
        Ina228Telemetry ina228{};
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
        TemperatureTelemetry temperatures{};
#endif
    };

    struct Status {
        State state{State::unavailable};
        std::array<char, kMaxUriLength + 1U> uri{};
        std::array<char, kMaxUsernameLength + 1U> username{};
        std::array<char, kMaxTopicPrefixLength + 1U> topic_prefix{};
        std::array<char, kMaxDiscoveryPrefixLength + 1U> discovery_prefix{};
        std::array<char, 32> device_id{};
        std::uint32_t connect_count{0};
        std::uint32_t accepted_commands{0};
        std::uint32_t rejected_commands{0};
        std::int32_t last_error_type{0};
        std::int32_t last_connect_return_code{0};
        bool initialized{false};
        bool started{false};
        bool configuration_ready{false};
        bool configuration_applied{false};
        bool password_set{false};
        bool pending_changes{false};
        bool publish_backlog{false};
        bool birth_queued{false};
        bool subscriptions_ready{false};
        bool discovery_queued{false};
        bool discovery_confirmed{false};
        std::uint8_t subscriptions_done{0};
        std::uint8_t subscriptions_total{
            static_cast<std::uint8_t>(MqttSyncMachine::kSubscriptionCount)};
        std::uint32_t discovery_payload_bytes{0};
        std::uint32_t outbox_bytes{0};
        SyncError last_sync_error{SyncError::none};
    };

    MqttService(LedController& controller, OtaService& ota_service,
                WifiService& wifi_service) noexcept;

    esp_err_t initialize() noexcept;
    esp_err_t start() noexcept;

    esp_err_t setUri(const char* uri) noexcept;
    esp_err_t setUsername(const char* username) noexcept;
    esp_err_t setPassword(const char* password) noexcept;
    esp_err_t setTopicPrefix(const char* prefix) noexcept;
    esp_err_t setDiscoveryPrefix(const char* prefix) noexcept;

    esp_err_t apply() noexcept;
    esp_err_t disconnect() noexcept;
    esp_err_t clear() noexcept;
    esp_err_t submitSnapshot(const Snapshot& snapshot) noexcept;
    void recordPwmCommandResult(bool applied) noexcept;

    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] bool tryGetStatus(Status& output) const noexcept;
    [[nodiscard]] bool tryGetSnapshot(Snapshot& output) const noexcept;
#if LED_CONTROLLER_AT24C08_ENABLED
    esp_err_t exportPersistentConfig(persistent_settings::MqttConfig& output) const noexcept;
    esp_err_t restorePersistentConfig(const persistent_settings::MqttConfig& input) noexcept;
#endif

  private:
    static constexpr std::uint32_t kReconnectIntervalMs = 5'000;
    static constexpr std::uint32_t kNetworkTimeoutMs = 3'000;
    static constexpr std::uint32_t kMinimumStatePublishIntervalMs = 200;
    static constexpr std::uint32_t kPublishRetryIntervalMs = 500;
    static constexpr std::uint32_t kServiceTaskStackSize = 6'144;
    static constexpr UBaseType_t kServiceTaskPriority = 3;
    static constexpr std::size_t kTopicLength = 160;
    static constexpr std::size_t kDiscoveryPayloadLength = mqtt_discovery::kPayloadCapacity;
    static constexpr std::size_t kMqttBufferLength = 8'192;
    static constexpr std::uint64_t kOutboxLimit = 32'768;

    struct RuntimeConfig {
        std::array<char, kMaxUriLength + 1U> uri{};
        std::array<char, kMaxUsernameLength + 1U> username{};
        std::array<char, kMaxPasswordLength + 1U> password{};
        std::array<char, kMaxTopicPrefixLength + 1U> topic_prefix{};
        std::array<char, kMaxDiscoveryPrefixLength + 1U> discovery_prefix{};
    };

    struct TopicLayout {
        std::array<char, kTopicLength> base{};
        std::array<std::array<char, kTopicLength>, 10> pwm_command{}, pwm_state{};
        std::array<char, kTopicLength> availability{};
        std::array<char, kTopicLength> discovery{};
        std::array<char, kTopicLength> home_assistant_status{};
        std::array<char, kTopicLength> ota_command{};
        std::array<char, kTopicLength> ota_status{};
        std::array<char, kTopicLength> debug_command{};
        std::array<char, kTopicLength> debug_status{};
        std::array<char, kTopicLength> debug_log{};
        std::array<char, kTopicLength> lcd_mode_command{};
        std::array<char, kTopicLength> lcd_mode_state{};
        std::array<char, kTopicLength> lcd_override_command{};
        std::array<char, kTopicLength> lcd_override_state{};
        std::array<char, kTopicLength> lcd_night_command{};
        std::array<char, kTopicLength> lcd_night_state{};
        std::array<std::array<char, kTopicLength>, kChannelCount> command{};
        std::array<std::array<char, kTopicLength>, kChannelCount> state{};
        std::array<std::array<char, kTopicLength>, kPirInputCount> pir_state{};
        std::array<std::array<char, kTopicLength>, kChannelCount> mapping_command{};
        std::array<std::array<char, kTopicLength>, kChannelCount> mapping_state{};
        std::array<std::array<char, kTopicLength>, kPirInputCount> delay_command{};
        std::array<std::array<char, kTopicLength>, kPirInputCount> delay_state{};
#if LED_CONTROLLER_INA228_ENABLED
        std::array<char, kTopicLength> ina228_availability{};
        std::array<char, kTopicLength> ina228_current{};
        std::array<char, kTopicLength> ina228_bus_voltage{};
        std::array<char, kTopicLength> ina228_power{};
        std::array<char, kTopicLength> ina228_energy{};
        std::array<char, kTopicLength> ina228_die_temperature{};
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
        std::array<std::array<char, kTopicLength>, kTemperatureRoleCount>
            temperature_availability{};
        std::array<std::array<char, kTopicLength>, kTemperatureRoleCount> temperature_state{};
#endif
    };

    static void taskEntry(void* context) noexcept;
    [[noreturn]] void taskLoop() noexcept;
    static void eventHandler(void* context, esp_event_base_t event_base, std::int32_t event_id,
                             void* event_data) noexcept;
    static void otaStatusCallback(void* context, const OtaService::Status& status) noexcept;

    void handleEvent(esp_mqtt_event_handle_t event) noexcept;
    void handleData(const esp_mqtt_event_t& event) noexcept;
    void requestSynchronization() noexcept;
    void advanceSynchronization(std::uint32_t now_ms) noexcept;
    [[nodiscard]] const char* subscriptionTopic(std::size_t index) const noexcept;
    bool publishSnapshot(const Snapshot& snapshot) noexcept;
    bool publishOtaStatus() noexcept;
    void serviceDebug() noexcept;
    void startClientWhenNetworkReady() noexcept;
    int publishDiscovery(esp_mqtt_client_handle_t client) noexcept;
    int enqueueMessageId(esp_mqtt_client_handle_t client, const char* topic, const char* payload,
                         bool retain) noexcept;
    bool enqueueMessage(esp_mqtt_client_handle_t client, const char* topic, const char* payload,
                        bool retain) noexcept;
    esp_err_t stopClient(bool publish_offline) noexcept;
    esp_err_t createClient() noexcept;
    bool buildTopics(const RuntimeConfig& config) noexcept;
    bool buildDiscoveryPayload(SyncError& failure, mqtt_discovery::StageObserver observer,
                               void* observer_context) noexcept;
    void updateStatusFromConfigurationLocked() noexcept;
    void noteRejectedCommand() noexcept;
    void noteAcceptedCommand() noexcept;

    [[nodiscard]] bool configurationIsValid(const RuntimeConfig& config) const noexcept;
    [[nodiscard]] static bool topicPrefixIsValid(const char* prefix,
                                                 std::size_t maximum_length) noexcept;
    [[nodiscard]] static bool runtimeConfigsEqual(const RuntimeConfig& left,
                                                  const RuntimeConfig& right) noexcept;
    [[nodiscard]] static bool topicEquals(const char* topic, int topic_length,
                                          const char* expected) noexcept;
    static void copyText(char* destination, std::size_t destination_size, const char* source,
                         std::size_t source_limit) noexcept;
    static void secureZero(void* data, std::size_t size) noexcept;

    WifiService& wifi_service_;
    LedController& controller_;
    OtaService& ota_service_;
    mutable StaticSemaphore_t mutex_storage_{};
    mutable SemaphoreHandle_t mutex_{nullptr};
    StaticSemaphore_t client_mutex_storage_{};
    SemaphoreHandle_t client_mutex_{nullptr};
    StaticQueue_t snapshot_queue_storage_{};
    std::array<std::uint8_t, sizeof(Snapshot)> snapshot_queue_buffer_{};
    QueueHandle_t snapshot_queue_{nullptr};
    TaskHandle_t task_handle_{nullptr};
    RuntimeConfig staged_config_{};
    RuntimeConfig applied_config_{};
    TopicLayout topics_{};
    std::array<char, 32> device_id_{};
    MqttPublishTracker publish_tracker_{};
    MqttSyncMachine sync_machine_{};
    std::uint32_t ota_status_revision_published_{0};
    bool ota_status_published_{false};
    bool debug_status_published_{false};
    bool debug_active_published_{false};
    std::uint32_t debug_status_seconds_{0};
    TickType_t debug_log_tick_{0};
    bool enqueue_error_reported_{false};
    std::array<char, kDiscoveryPayloadLength> discovery_payload_{};
    bool discovery_payload_valid_{false};
    std::size_t discovery_payload_bytes_{0};
    esp_mqtt_client_handle_t client_{nullptr};
    bool client_started_{false};
    bool start_retry_pending_{false};
    TickType_t start_retry_tick_{0};
    std::atomic<esp_mqtt_client_handle_t> active_client_{nullptr};
    std::atomic<bool> connected_{false};
    std::atomic<bool> synchronization_requested_{false};
    std::atomic<std::uint32_t> connection_generation_{0};
    std::atomic<int> last_suback_id_{0};
    std::atomic<bool> last_suback_accepted_{false};
    std::atomic<int> last_puback_id_{0};
    std::atomic<int> discovery_message_id_{0};
    std::atomic<bool> discovery_acknowledged_{false};
    std::atomic<bool> publish_backlog_{false};
    std::atomic<std::uint32_t> accepted_commands_{0};
    std::atomic<std::uint32_t> rejected_commands_{0};
    Status status_{};
    bool initialized_{false};
    bool started_{false};
};
