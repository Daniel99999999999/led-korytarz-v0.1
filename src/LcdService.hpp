#pragma once

#include "FeatureConfig.hpp"
#include "HardwareDiagnostics.hpp"
#if LED_CONTROLLER_DS1307_NVRAM_ENABLED
#include "Ds1307Nvram.hpp"
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
#include "Ds18b20Service.hpp"
#include "TemperatureTelemetry.hpp"
#endif
#include "I2cBus.hpp"
#if LED_CONTROLLER_INA228_ENABLED
#include "Ina228.hpp"
#include "Ina228Telemetry.hpp"
#endif
#include "LcdDashboard.hpp"
#include "LcdDisplay.hpp"
#include "LocalControlConfig.hpp"
#if LED_CONTROLLER_AT24C08_ENABLED
#include "SettingsService.hpp"
#endif
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <cstdint>

class LedController;

class LcdService final {
  public:
    struct Snapshot {
        std::array<LcdDashboard::ChannelSnapshot, LcdDashboard::kChannelCount> channels;
        LcdDashboard::Status status;
        std::uint64_t uptime_seconds;
        LcdDashboard::NetworkSnapshot network;
        LcdDashboard::MqttSnapshot mqtt;
        LcdDashboard::NoticeSnapshot notice;
        LcdDashboard::MeasurementSnapshot measurement;
        bool service_active{false};
        LcdDashboard::Lines service_lines{};
        local_control::Config local_config{};
        std::uint32_t local_config_revision{0};
    };

    bool tryGetHardwareDiagnostics(HardwareDiagnostics& output) const noexcept;
    esp_err_t initialize() noexcept;
#if LED_CONTROLLER_DS1307_NVRAM_ENABLED
    void attachLocalController(LedController& controller) noexcept;
#endif
    esp_err_t start() noexcept;
    esp_err_t publish(const Snapshot& snapshot) noexcept;
    esp_err_t requestBacklight(bool enabled) noexcept;
#if LED_CONTROLLER_AT24C08_ENABLED
    esp_err_t attachSettingsService(SettingsService* settings_service) noexcept;
#endif
    [[nodiscard]] esp_err_t currentFault() const noexcept;
#if LED_CONTROLLER_INA228_ENABLED
    [[nodiscard]] bool tryGetIna228Telemetry(Ina228Telemetry& output) const noexcept;
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    void requestTemperatureScan() noexcept;
    esp_err_t assignTemperatureRole(TemperatureRole role,
                                    std::size_t one_based_sensor_index) noexcept;
    esp_err_t clearTemperatureRole(TemperatureRole role) noexcept;
    esp_err_t clearTemperatureAssignments() noexcept;
    [[nodiscard]] bool tryGetTemperatureTelemetry(TemperatureTelemetry& output) const noexcept;
#endif

  private:
#if LED_CONTROLLER_AT24C08_ENABLED || LED_CONTROLLER_DS18B20_ENABLED
    static constexpr std::uint32_t kTaskStackSize = 6'144;
#else
    static constexpr std::uint32_t kTaskStackSize = 4'096;
#endif
    static constexpr UBaseType_t kTaskPriority = 3;
    static constexpr std::uint32_t kTaskPeriodMs = 5;

    static void taskEntry(void* context) noexcept;
    [[noreturn]] void taskLoop() noexcept;
    void reportFault(const char* operation, esp_err_t error,
                     bool shared_bus_fault = false) noexcept;
    void recoverSharedBusAfterTimeout(const char* operation, esp_err_t error) noexcept;
    void reportBusRecovery() noexcept;
    void reportRecovery() noexcept;
#if LED_CONTROLLER_INA228_ENABLED
    esp_err_t initializeIna228(TickType_t now) noexcept;
    esp_err_t resetIna228() noexcept;
    void serviceIna228(TickType_t now) noexcept;
    void publishIna228Telemetry(const Ina228Telemetry& telemetry) noexcept;
    void markIna228Unavailable(esp_err_t error, bool present) noexcept;
#endif

    void publishHardwareDiagnostics() noexcept;
    HardwareDiagnostics hardware_{};
    HardwareDiagnostics published_hardware_{};
    mutable std::atomic_flag hardware_busy_ = ATOMIC_FLAG_INIT;
    I2cBus bus_;
#if LED_CONTROLLER_DS1307_NVRAM_ENABLED
    Ds1307Nvram nvram_;
    LedController* local_controller_{nullptr};
#endif
    LcdDisplay display_;
    StaticQueue_t queue_storage_{};
    std::array<std::uint8_t, sizeof(Snapshot)> queue_buffer_{};
    QueueHandle_t snapshot_queue_{nullptr};
    TaskHandle_t task_handle_{nullptr};
    std::atomic<std::int32_t> fault_code_{ESP_OK};
    bool fault_is_shared_bus_{false};
    std::atomic<bool> desired_backlight_{true};
#if LED_CONTROLLER_AT24C08_ENABLED
    SettingsService* settings_service_{nullptr};
#endif
#if LED_CONTROLLER_INA228_ENABLED
    Ina228 ina228_;
    mutable StaticSemaphore_t ina228_mutex_storage_{};
    mutable SemaphoreHandle_t ina228_mutex_{nullptr};
    Ina228Telemetry ina228_telemetry_{};
    TickType_t ina228_next_action_at_{0};
    std::uint16_t ina228_device_id_{0};
    bool ina228_configured_{false};
    bool ina228_absence_reported_{false};
    bool ina228_fault_reported_{false};
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    Ds18b20Service temperature_service_;
#endif
    bool initialized_{false};
};
