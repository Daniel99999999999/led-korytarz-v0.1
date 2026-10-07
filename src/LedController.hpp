#pragma once

#include "BacklightPolicy.hpp"
#include "BoardConfig.hpp"
#include "Button.hpp"
#include "ControlCommand.hpp"
#if LED_CONTROLLER_INA228_ENABLED
#include "Ina228Telemetry.hpp"
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
#include "TemperatureTelemetry.hpp"
#endif
#include "LcdService.hpp"
#include "LedChannel.hpp"
#include "PirAutomation.hpp"
#include "PirInput.hpp"
#include "StatusLed.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <array>
#include <cstdint>

class WifiService;
class LocalOtaService;
class MqttService;
#if LED_CONTROLLER_AT24C08_ENABLED
class SettingsService;
#endif

class LedController final {
  public:
    explicit LedController(WifiService* wifi_service = nullptr) noexcept;

    void attachLocalOta(LocalOtaService& service) noexcept {
        local_ota_ = &service;
    }
    esp_err_t initialize() noexcept;
    esp_err_t start() noexcept;
    esp_err_t attachMqttService(MqttService* mqtt_service) noexcept;
#if LED_CONTROLLER_AT24C08_ENABLED
    esp_err_t attachSettingsService(SettingsService* settings_service) noexcept;
#endif
    esp_err_t enqueueCommand(const ControlCommand& command) noexcept;
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
    static constexpr UBaseType_t kControlCommandQueueLength = 10;

    static void taskEntry(void* context) noexcept;
    [[noreturn]] void taskLoop() noexcept;
    void handleButton(std::size_t index, Button::Event event, std::uint32_t now) noexcept;
    void handleChannelResult(std::size_t index, esp_err_t result) noexcept;
    void manualOverride(std::size_t index) noexcept;
    void servicePir(std::uint32_t now) noexcept;
    void processControlCommands(std::uint32_t now) noexcept;
    esp_err_t applyControlCommand(const ControlCommand& command, std::uint32_t now) noexcept;
#if LED_CONTROLLER_IR_NEC_ENABLED
    void processIrAction(const IrRemoteAction& action, std::uint32_t now) noexcept;
    void serviceIrTimers(std::uint32_t now) noexcept;
    void applyIrSelectedPower(const IrRemoteAction& action, std::uint32_t now) noexcept;
    void applyIrTargetToggle(const IrRemoteAction& action, std::uint32_t now) noexcept;
    void applyIrNightToggle(const IrRemoteAction& action, std::uint32_t now) noexcept;
    void applyIrBrightness(const IrRemoteAction& action, std::uint32_t now, bool increase) noexcept;
    void applyIrTimerToggle(const IrRemoteAction& action, std::uint32_t now) noexcept;
    void handleIrChannelResult(std::size_t index, const char* operation, esp_err_t result) noexcept;
    void clearIrNightForChannel(std::size_t index) noexcept;
    void clearIrNightForTarget(std::uint8_t target) noexcept;
    void showIrTargetNotice(std::uint8_t target, std::uint32_t now) noexcept;
    void showIrNotice(const char* text, std::uint32_t now) noexcept;
    [[nodiscard]] bool irTargetIncludes(std::uint8_t target,
                                        std::size_t channel_index) const noexcept;
    [[nodiscard]] bool irTargetHasManualDimming(std::uint8_t target) const noexcept;
    [[nodiscard]] bool irActionNeedsLedAccess(const IrRemoteAction& action) const noexcept;
#endif
    void serviceMqtt() noexcept;
    void serviceLcd(TickType_t now) noexcept;
    void handleLcdFailure(const char* operation, esp_err_t error) noexcept;
    void handleLcdRecovery(TickType_t now) noexcept;
    void handleLcdInternalFailure(const char* operation, esp_err_t error) noexcept;
    [[nodiscard]] LcdService::Snapshot makeLcdSnapshot() noexcept;
    void latchStatusFault() noexcept;

    StatusLed status_led_;
    std::array<LedChannel, board_config::kLeds.size()> leds_;
    std::array<Button, board_config::kButtons.size()> buttons_;
    std::array<PirInput, board_config::kPirInputs.size()> pir_inputs_;
    LcdService lcd_service_;
    Button lcd_backlight_button_;
    LocalOtaService* local_ota_{nullptr};
    WifiService* wifi_service_{nullptr};
    MqttService* mqtt_service_{nullptr};
    BacklightPolicy backlight_policy_{};
    bool lcd_night_active_{false};
    bool mqtt_last_lcd_night_{false};
    local_control::Config local_config_{};
    local_control::Config mqtt_last_local_config_{};
    std::uint32_t local_config_revision_{0};
    std::array<PirAutomation, local_control::kPirCount> pir_automation_{};
    std::array<std::uint8_t, local_control::kChannelCount> gesture_masks_{};
    std::array<std::size_t, local_control::kChannelCount> gesture_leaders_{};
    std::array<std::array<std::uint8_t, local_control::kChannelCount>, local_control::kChannelCount>
        gesture_levels_{};
    LcdDashboard::NetworkSnapshot network_snapshot_cache_{};
    LcdDashboard::MqttSnapshot mqtt_snapshot_cache_{};
    StaticQueue_t command_queue_storage_{};
    std::array<std::uint8_t, kControlCommandQueueLength * sizeof(ControlCommand)>
        command_queue_buffer_{};
    QueueHandle_t command_queue_{nullptr};
    TaskHandle_t task_handle_{nullptr};
    TickType_t last_lcd_refresh_at_{0};
    TickType_t last_uptime_tick_{0};
    std::uint64_t uptime_ticks_{0};
    bool initialized_{false};
    bool status_led_initialized_{false};
    bool watchdog_initialized_{false};
    bool watchdog_reset_detected_{false};
    bool system_fault_latched_{false};
    bool lcd_fault_active_{false};
    bool lcd_service_initialized_{false};
    bool lcd_service_started_{false};
    bool lcd_backlight_button_initialized_{false};
    bool lcd_snapshot_published_{false};
    std::array<bool, board_config::kLeds.size()> manual_dimming_active_{};
    std::array<bool, board_config::kLeds.size()> led_error_reported_{};
    std::array<bool, board_config::kPirInputs.size()> pir_input_initialized_{};
    std::array<bool, board_config::kLeds.size()> mqtt_last_on_{};
    std::array<std::uint8_t, board_config::kLeds.size()> mqtt_last_brightness_{};
    std::array<bool, board_config::kPirInputs.size()> mqtt_last_pir_active_{};
    bool mqtt_snapshot_published_{false};
    bool mqtt_snapshot_error_reported_{false};
#if LED_CONTROLLER_INA228_ENABLED
    Ina228Telemetry ina228_telemetry_cache_{};
    std::uint32_t mqtt_last_ina228_revision_{0};
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    TemperatureTelemetry temperature_telemetry_cache_{};
    std::uint32_t mqtt_last_temperature_revision_{0};
#endif
#if LED_CONTROLLER_IR_NEC_ENABLED
    std::array<bool, board_config::kLeds.size()> ir_night_active_{};
    std::array<bool, board_config::kLeds.size()> ir_night_saved_on_{};
    std::array<std::uint8_t, board_config::kLeds.size()> ir_night_saved_brightness_{};
    std::array<bool, board_config::kLeds.size()> ir_timer_active_{};
    std::array<std::uint32_t, board_config::kLeds.size()> ir_timer_deadline_{};
    LcdDashboard::Line ir_notice_text_{};
    std::uint32_t ir_notice_expires_at_{0};
    bool ir_notice_active_{false};
#endif
};
