#include "LedController.hpp"

#include "LocalOtaService.hpp"
#include "MqttCommandParser.hpp"
#include "MqttService.hpp"
#include "TaskWatchdog.hpp"
#include "WifiService.hpp"
#include "esp_log.h"
#include "esp_system.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {
constexpr char kTag[] = "led_control";
constexpr std::uint32_t kTaskStackSize = 4096;
constexpr UBaseType_t kTaskPriority = 5;
#if LED_CONTROLLER_IR_NEC_ENABLED
constexpr std::uint8_t kIrBrightnessStepPercent = 5;
constexpr std::uint8_t kIrNightBrightnessPercent = 10;
constexpr std::uint32_t kIrOffTimerMs = 30U * 60U * 1'000U;
constexpr std::uint32_t kIrNoticeDurationMs = 2'000;

[[nodiscard]] bool deadlineReached(const std::uint32_t now, const std::uint32_t deadline) noexcept {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}
#endif

static_assert(std::is_trivially_copyable_v<ControlCommand>,
              "ControlCommand musi nadawac sie do kopiowania przez kolejke FreeRTOS");
static_assert(MqttService::kChannelCount == board_config::kLeds.size());
static_assert(MqttService::kPirInputCount == board_config::kPirInputs.size());

[[nodiscard]] bool isWatchdogReset(const esp_reset_reason_t reason) noexcept {
    return (reason == ESP_RST_INT_WDT) || (reason == ESP_RST_TASK_WDT) || (reason == ESP_RST_WDT);
}

template <std::size_t... Indices>
std::array<LedChannel, sizeof...(Indices)>
makeLedChannels(std::index_sequence<Indices...>) noexcept {
    return {{LedChannel(board_config::kLeds[Indices].name, board_config::kLeds[Indices].pwm_pin,
                        board_config::kLeds[Indices].ledc_channel)...}};
}

template <std::size_t... Indices>
std::array<Button, sizeof...(Indices)> makeButtons(std::index_sequence<Indices...>) noexcept {
    return {{Button(board_config::kButtons[Indices].name, board_config::kButtons[Indices].pin,
                    board_config::kButtons[Indices].active_low, board_config::kButtonDebounceMs,
                    board_config::kButtonLongPressMs, board_config::kBrightnessStepIntervalMs,
                    board_config::kButtonInternalPullUpsEnabled)...}};
}

template <std::size_t... Indices>
std::array<PirInput, sizeof...(Indices)> makePirInputs(std::index_sequence<Indices...>) noexcept {
    return {{PirInput(board_config::kPirInputs[Indices].name, board_config::kPirInputs[Indices].pin,
                      board_config::kPirActiveHigh, board_config::kPirContactDebounceMs)...}};
}
} // namespace

LedController::LedController(WifiService* const wifi_service) noexcept
    : status_led_(
          board_config::kStatusLedPin, board_config::kStatusLedActiveHigh,
          static_cast<std::uint32_t>(pdMS_TO_TICKS(board_config::kStatusLedToggleIntervalMs))),
      leds_(makeLedChannels(std::make_index_sequence<board_config::kLeds.size()>{})),
      buttons_(makeButtons(std::make_index_sequence<board_config::kButtons.size()>{})),
      pir_inputs_(makePirInputs(std::make_index_sequence<board_config::kPirInputs.size()>{})),
      lcd_service_(),
      lcd_backlight_button_(
          "Przycisk podswietlenia LCD", board_config::kLcdBacklightButtonPin,
          board_config::kLcdBacklightButtonActiveLow, board_config::kButtonDebounceMs, 10'000U,
          board_config::kBrightnessStepIntervalMs, board_config::kButtonInternalPullUpsEnabled),
      wifi_service_(wifi_service) {}

esp_err_t LedController::initialize() noexcept {
    esp_err_t result = ESP_OK;
    for (LedChannel& led : leds_) {
        result = led.prepareOutputLow();
        if (result != ESP_OK) {
            return result;
        }
    }

    const esp_reset_reason_t reset_reason = esp_reset_reason();
    watchdog_reset_detected_ = isWatchdogReset(reset_reason);
    result = status_led_.initialize(watchdog_reset_detected_,
                                    static_cast<std::uint32_t>(xTaskGetTickCount()));
    if (result == ESP_OK) {
        status_led_initialized_ = true;
        if (watchdog_reset_detected_) {
            ESP_LOGE(kTag, "Wykryto reset watchdog; LED statusu pozostaje wlaczona");
        }
    } else {
        ESP_LOGE(kTag, "Nie udalo sie uruchomic LED statusu: %s", esp_err_to_name(result));
    }

    result = TaskWatchdog::initialize(board_config::kTaskWatchdogTimeoutMs);
    if (result == ESP_OK) {
        watchdog_initialized_ = true;
    } else {
        ESP_LOGE(kTag, "Nie udalo sie skonfigurowac watchdog: %s", esp_err_to_name(result));
        latchStatusFault();
    }

    result = LedChannel::initializePeripheral();
    if (result != ESP_OK) {
        latchStatusFault();
        return result;
    }

    for (LedChannel& led : leds_) {
        result = led.initialize();
        if (result != ESP_OK) {
            latchStatusFault();
            return result;
        }
    }

    for (Button& button : buttons_) {
        result = button.initialize();
        if (result != ESP_OK) {
            latchStatusFault();
            return result;
        }
    }

    for (std::size_t index = 0; index < pir_inputs_.size(); ++index) {
        const esp_err_t pir_result = pir_inputs_[index].initialize();
        if (pir_result == ESP_OK) {
            pir_input_initialized_[index] = true;
        } else {
            ESP_LOGE(kTag, "%s: nie udalo sie uruchomic wejscia: %s", pir_inputs_[index].name(),
                     esp_err_to_name(pir_result));
            latchStatusFault();
        }
    }

    command_queue_ = xQueueCreateStatic(kControlCommandQueueLength, sizeof(ControlCommand),
                                        command_queue_buffer_.data(), &command_queue_storage_);
    if (command_queue_ == nullptr) {
        latchStatusFault();
        return ESP_ERR_NO_MEM;
    }

    result = lcd_backlight_button_.initialize();
    if (result == ESP_OK) {
        lcd_backlight_button_initialized_ = true;
        ESP_LOGI(kTag, "GPIO19 przy starcie: %s",
                 gpio_get_level(board_config::kLcdBacklightButtonPin) != 0 ? "HIGH" : "LOW");
    } else {
        ESP_LOGE(kTag, "Nie udalo sie uruchomic przycisku LCD: %s", esp_err_to_name(result));
        latchStatusFault();
    }

#if LED_CONTROLLER_DS1307_NVRAM_ENABLED
    lcd_service_.attachLocalController(*this);
#endif
    result = lcd_service_.initialize();
    if (result == ESP_OK) {
        lcd_service_initialized_ = true;
        last_lcd_refresh_at_ = xTaskGetTickCount();
    } else {
        ESP_LOGE(kTag, "LCD niedostepny; sterowanie lokalne pozostaje aktywne: %s",
                 esp_err_to_name(result));
        latchStatusFault();
    }

    initialized_ = true;
    ESP_LOGI(kTag, "Sterownik lokalny gotowy: %u kanalow", static_cast<unsigned>(leds_.size()));
    return ESP_OK;
}

esp_err_t LedController::start() noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (task_handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (lcd_service_initialized_) {
        const esp_err_t lcd_result = lcd_service_.start();
        if (lcd_result == ESP_OK) {
            lcd_service_started_ = true;
        } else {
            ESP_LOGE(kTag, "Nie udalo sie uruchomic zadania LCD: %s", esp_err_to_name(lcd_result));
            latchStatusFault();
        }
    }

    const BaseType_t result =
        xTaskCreate(taskEntry, "led_control", kTaskStackSize, this, kTaskPriority, &task_handle_);
    if (result != pdPASS) {
        latchStatusFault();
    }
    return (result == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t LedController::attachMqttService(MqttService* const mqtt_service) noexcept {
    if (initialized_ || (mqtt_service == nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }
    mqtt_service_ = mqtt_service;
    return ESP_OK;
}

#if LED_CONTROLLER_AT24C08_ENABLED
esp_err_t LedController::attachSettingsService(SettingsService* const settings_service) noexcept {
    if (initialized_ || (settings_service == nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }
    return lcd_service_.attachSettingsService(settings_service);
}
#endif

esp_err_t LedController::enqueueCommand(const ControlCommand& command) noexcept {
    if (command_queue_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((command.kind == ControlCommand::Kind::channel) &&
        (command.channel_index >= leds_.size())) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((command.kind == ControlCommand::Kind::infrared) &&
        ((command.infrared.type == IrRemoteAction::Type::none) ||
         (command.infrared.target > IrRemoteAction::kAllChannelsTarget))) {
        return ESP_ERR_INVALID_ARG;
    }

    return (xQueueSend(command_queue_, &command, 0) == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

#if LED_CONTROLLER_INA228_ENABLED
bool LedController::tryGetIna228Telemetry(Ina228Telemetry& output) const noexcept {
    return lcd_service_.tryGetIna228Telemetry(output);
}
#endif

#if LED_CONTROLLER_DS18B20_ENABLED
void LedController::requestTemperatureScan() noexcept {
    lcd_service_.requestTemperatureScan();
}

esp_err_t LedController::assignTemperatureRole(const TemperatureRole role,
                                               const std::size_t one_based_sensor_index) noexcept {
    return lcd_service_.assignTemperatureRole(role, one_based_sensor_index);
}

esp_err_t LedController::clearTemperatureRole(const TemperatureRole role) noexcept {
    return lcd_service_.clearTemperatureRole(role);
}

esp_err_t LedController::clearTemperatureAssignments() noexcept {
    return lcd_service_.clearTemperatureAssignments();
}

bool LedController::tryGetTemperatureTelemetry(TemperatureTelemetry& output) const noexcept {
    return lcd_service_.tryGetTemperatureTelemetry(output);
}
#endif

void LedController::taskEntry(void* const context) noexcept {
    auto* const controller = static_cast<LedController*>(context);
    controller->taskLoop();
}

[[noreturn]] void LedController::taskLoop() noexcept {
    bool watchdog_subscribed = false;
    if (watchdog_initialized_) {
        const esp_err_t result = TaskWatchdog::subscribeCurrentTask();
        if (result == ESP_OK) {
            watchdog_subscribed = true;
        } else {
            ESP_LOGE(kTag, "Nie udalo sie dolaczyc zadania do watchdog: %s",
                     esp_err_to_name(result));
            latchStatusFault();
        }
    }

    TickType_t next_wake = xTaskGetTickCount();
    last_uptime_tick_ = next_wake;
    const TickType_t interval = pdMS_TO_TICKS(board_config::kButtonPollIntervalMs);
    bool status_error_reported = false;
    bool watchdog_error_reported = false;
    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        uptime_ticks_ += static_cast<TickType_t>(now - last_uptime_tick_);
        last_uptime_tick_ = now;

        if (status_led_initialized_) {
            const esp_err_t result = status_led_.update(static_cast<std::uint32_t>(now));
            if ((result != ESP_OK) && !status_error_reported) {
                ESP_LOGE(kTag, "Blad aktualizacji LED statusu: %s", esp_err_to_name(result));
                status_error_reported = true;
            }
        }

        for (std::size_t index = 0; index < pir_inputs_.size(); ++index) {
            if (!pir_input_initialized_[index]) {
                continue;
            }
            const PirInput::Event event = pir_inputs_[index].poll(now);
            if (event != PirInput::Event::none) {
                ESP_LOGI(kTag, "%s -> %s", pir_inputs_[index].name(),
                         event == PirInput::Event::activated ? "AKTYWNE (HIGH)" : "NORMALNY (LOW)");
            }
        }

        for (std::size_t index = 0; index < buttons_.size(); ++index) {
            handleButton(index, buttons_[index].poll(now), static_cast<std::uint32_t>(now));
        }
        // Every output must progress even when its physical button is disabled or remapped.
        for (std::size_t index = 0; index < leds_.size(); ++index) {
            if (!led_error_reported_[index]) {
                handleChannelResult(index, leds_[index].update(static_cast<std::uint32_t>(now)));
            }
        }

        processControlCommands(static_cast<std::uint32_t>(now));
#if LED_CONTROLLER_IR_NEC_ENABLED
        serviceIrTimers(static_cast<std::uint32_t>(now));
#endif
        servicePir(static_cast<std::uint32_t>(now));
        serviceMqtt();

        serviceLcd(now);

        if (watchdog_subscribed) {
            const esp_err_t result = TaskWatchdog::feed();
            if ((result != ESP_OK) && !watchdog_error_reported) {
                ESP_LOGE(kTag, "Blad karmienia watchdog: %s", esp_err_to_name(result));
                latchStatusFault();
                watchdog_error_reported = true;
            }
        }

        xTaskDelayUntil(&next_wake, interval);
    }
}

void LedController::handleChannelResult(const std::size_t index, const esp_err_t result) noexcept {
    if (result != ESP_OK && !led_error_reported_[index]) {
        ESP_LOGE(kTag, "%s: blad PWM: %s", leds_[index].name(), esp_err_to_name(result));
        led_error_reported_[index] = true;
        latchStatusFault();
    }
}

void LedController::manualOverride(const std::size_t index) noexcept {
    if (index < pir_automation_.size()) {
        pir_automation_[index].manualOverride(pir_inputs_[index].isActive());
    }
}

void LedController::servicePir(const std::uint32_t now) noexcept {
    for (std::size_t index = 0; index < pir_automation_.size(); ++index) {
        if (!pir_input_initialized_[index] || led_error_reported_[index] ||
            manual_dimming_active_[index]) {
            continue;
        }
        const std::uint32_t delay = static_cast<std::uint32_t>(pdMS_TO_TICKS(
            static_cast<std::uint32_t>(local_config_.off_delay_seconds[index]) * 1000U));
        const auto action = pir_automation_[index].update(pir_inputs_[index].isActive(),
                                                          leds_[index].isOn(), now, delay);
        if (action == PirAutomation::Action::none) {
            continue;
        }
#if LED_CONTROLLER_IR_NEC_ENABLED
        clearIrNightForChannel(index);
#endif
        const auto fade = static_cast<std::uint32_t>(pdMS_TO_TICKS(board_config::kFadeTimeMs));
        handleChannelResult(index, action == PirAutomation::Action::turn_on
                                       ? leds_[index].turnOn(now, fade)
                                       : leds_[index].turnOff(now, fade));
    }
}

void LedController::handleButton(const std::size_t index, const Button::Event event,
                                 const std::uint32_t now) noexcept {
    if (event == Button::Event::none) {
        return;
    }
    if (event == Button::Event::long_press_ended) {
        for (std::size_t channel = 0; channel < leds_.size(); ++channel) {
            if (local_control::includes(gesture_masks_[index], channel)) {
                leds_[channel].endDimming();
                manual_dimming_active_[channel] = false;
            }
        }
        gesture_masks_[index] = 0;
        return;
    }
    if (event == Button::Event::short_press || event == Button::Event::long_press_started) {
        auto mask = local_config_.button_masks[index];
        for (std::size_t channel = 0; channel < leds_.size(); ++channel) {
            if (led_error_reported_[channel]) {
                mask = static_cast<std::uint8_t>(mask & ~(1U << channel));
            }
        }
        if (mask == 0U) {
            return;
        }
        // A second held button must not steal another gesture's outputs.
        for (std::size_t channel = 0; channel < leds_.size(); ++channel) {
            if (local_control::includes(mask, channel) && manual_dimming_active_[channel]) {
                return;
            }
        }
        std::array<bool, local_control::kChannelCount> on{};
        std::size_t leader = leds_.size();
        for (std::size_t channel = 0; channel < leds_.size(); ++channel) {
            on[channel] = leds_[channel].isOn();
            gesture_levels_[index][channel] = leds_[channel].brightness();
            if (!local_control::includes(mask, channel) || led_error_reported_[channel]) {
                continue;
            }
            manualOverride(channel);
#if LED_CONTROLLER_IR_NEC_ENABLED
            clearIrNightForChannel(channel);
#endif
            if (leader == leds_.size() ||
                leds_[channel].brightness() > leds_[leader].brightness()) {
                leader = channel;
            }
        }
        if (event == Button::Event::short_press) {
            const bool turn_on = local_control::groupShouldTurnOn(mask, on);
            const auto fade = static_cast<std::uint32_t>(pdMS_TO_TICKS(board_config::kFadeTimeMs));
            for (std::size_t channel = 0; channel < leds_.size(); ++channel) {
                if (local_control::includes(mask, channel) && !led_error_reported_[channel]) {
                    handleChannelResult(channel, turn_on ? leds_[channel].turnOn(now, fade)
                                                         : leds_[channel].turnOff(now, fade));
                }
            }
            return;
        }
        if (leader == leds_.size()) {
            return;
        }
        gesture_masks_[index] = mask;
        gesture_leaders_[index] = leader;
        for (std::size_t channel = 0; channel < leds_.size(); ++channel) {
            if (local_control::includes(mask, channel)) {
                manual_dimming_active_[channel] = true;
            }
        }
        handleChannelResult(leader, leds_[leader].beginDimming(now));
    } else if (event == Button::Event::long_press_step) {
        if (gesture_masks_[index] == 0U) {
            return;
        }
        const auto leader = gesture_leaders_[index];
        if (!led_error_reported_[leader]) {
            handleChannelResult(leader, leds_[leader].stepDimming(now));
        }
    }
    const auto leader = gesture_leaders_[index];
    if (gesture_masks_[index] == 0U || led_error_reported_[leader]) {
        return;
    }
    for (std::size_t channel = 0; channel < leds_.size(); ++channel) {
        if (channel == leader || !local_control::includes(gesture_masks_[index], channel) ||
            led_error_reported_[channel]) {
            continue;
        }
        const auto level = local_control::scaledBrightness(gesture_levels_[index][channel],
                                                           gesture_levels_[index][leader],
                                                           leds_[leader].brightness());
        esp_err_t result = leds_[channel].setBrightness(level, now, 0);
        if (result == ESP_OK && !leds_[channel].isOn()) {
            result = leds_[channel].turnOn(now, 0);
        }
        handleChannelResult(channel, result);
    }
}

void LedController::serviceMqtt() noexcept {
    if (mqtt_service_ == nullptr) {
        return;
    }

    MqttService::Snapshot snapshot{};
    snapshot.local_config = local_config_;
    snapshot.lcd_night_active = lcd_night_active_;
    bool changed = !mqtt_snapshot_published_ || lcd_night_active_ != mqtt_last_lcd_night_ ||
                   !local_control::equal(local_config_, mqtt_last_local_config_);
    for (std::size_t index = 0; index < snapshot.channels.size(); ++index) {
        snapshot.channels[index].on = leds_[index].isOn();
        snapshot.channels[index].brightness_percent = leds_[index].brightness();
        changed = changed || (mqtt_last_on_[index] != snapshot.channels[index].on) ||
                  (mqtt_last_brightness_[index] != snapshot.channels[index].brightness_percent);
    }
    for (std::size_t index = 0; index < snapshot.pir_inputs.size(); ++index) {
        snapshot.pir_inputs[index].active =
            pir_input_initialized_[index] && pir_inputs_[index].isActive();
        changed = changed || (mqtt_last_pir_active_[index] != snapshot.pir_inputs[index].active);
    }
#if LED_CONTROLLER_INA228_ENABLED
    Ina228Telemetry telemetry{};
    if (lcd_service_.tryGetIna228Telemetry(telemetry)) {
        ina228_telemetry_cache_ = telemetry;
    }
    snapshot.ina228 = ina228_telemetry_cache_;
    changed = changed || (mqtt_last_ina228_revision_ != snapshot.ina228.revision);
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    TemperatureTelemetry temperature{};
    if (lcd_service_.tryGetTemperatureTelemetry(temperature)) {
        temperature_telemetry_cache_ = temperature;
    }
    snapshot.temperatures = temperature_telemetry_cache_;
    changed = changed || (mqtt_last_temperature_revision_ != snapshot.temperatures.revision);
#endif
    if (!changed) {
        return;
    }

    const esp_err_t result = mqtt_service_->submitSnapshot(snapshot);
    if (result != ESP_OK) {
        if (!mqtt_snapshot_error_reported_) {
            ESP_LOGW(kTag, "MQTT: nie udalo sie przekazac migawki: %s", esp_err_to_name(result));
            mqtt_snapshot_error_reported_ = true;
        }
        return;
    }

    for (std::size_t index = 0; index < snapshot.channels.size(); ++index) {
        mqtt_last_on_[index] = snapshot.channels[index].on;
        mqtt_last_brightness_[index] = snapshot.channels[index].brightness_percent;
    }
    for (std::size_t index = 0; index < snapshot.pir_inputs.size(); ++index) {
        mqtt_last_pir_active_[index] = snapshot.pir_inputs[index].active;
    }
#if LED_CONTROLLER_INA228_ENABLED
    mqtt_last_ina228_revision_ = snapshot.ina228.revision;
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    mqtt_last_temperature_revision_ = snapshot.temperatures.revision;
#endif
    mqtt_last_local_config_ = local_config_;
    mqtt_last_lcd_night_ = lcd_night_active_;
    mqtt_snapshot_published_ = true;
    mqtt_snapshot_error_reported_ = false;
}

void LedController::processControlCommands(const std::uint32_t now) noexcept {
    if (command_queue_ == nullptr) {
        return;
    }

    const UBaseType_t pending_at_start = uxQueueMessagesWaiting(command_queue_);
    for (UBaseType_t index = 0; index < pending_at_start; ++index) {
        ControlCommand command{};
        if (xQueueReceive(command_queue_, &command, 0) != pdPASS) {
            return;
        }

        if (command.kind == ControlCommand::Kind::lcd_night) {
            if (command.setting_value <= 1U) {
                lcd_night_active_ = command.setting_value != 0U;
            }
            continue;
        }
        if (command.kind == ControlCommand::Kind::pwm_setting ||
            command.kind == ControlCommand::Kind::button_mapping ||
            command.kind == ControlCommand::Kind::pir_delay ||
            command.kind == ControlCommand::Kind::lcd_override ||
            command.kind == ControlCommand::Kind::lcd_mode ||
            command.kind == ControlCommand::Kind::restore_local_config) {
            auto next = local_config_;
            const bool pwm_command = command.kind == ControlCommand::Kind::pwm_setting;
            const auto report_pwm_result = [this, pwm_command](const bool applied) noexcept {
                if (pwm_command && mqtt_service_ != nullptr) {
                    mqtt_service_->recordPwmCommandResult(applied);
                }
            };
            if (command.kind == ControlCommand::Kind::pwm_setting && command.channel_index < 2) {
                auto& pwm = next.pwm[command.channel_index];
                bool setting_valid = true;
                switch (command.setting_index) {
                case 0:
                    pwm.frequency_hz = command.setting_value;
                    break;
                case 1:
                    if (command.setting_value > 100)
                        setting_valid = false;
                    else
                        pwm.minimum_percent = static_cast<std::uint8_t>(command.setting_value);
                    break;
                case 2:
                    if (command.setting_value >= 16)
                        setting_valid = false;
                    else
                        pwm.on_index = static_cast<std::uint8_t>(command.setting_value);
                    break;
                case 3:
                    if (command.setting_value >= 16)
                        setting_valid = false;
                    else
                        pwm.off_index = static_cast<std::uint8_t>(command.setting_value);
                    break;
                case 4:
                    if (command.setting_value > 1)
                        setting_valid = false;
                    else
                        pwm.on_off = command.setting_value != 0;
                    break;
                default:
                    setting_valid = false;
                    break;
                }
                if (!setting_valid) {
                    report_pwm_result(false);
                    continue;
                }
                if (!local_control::valid(next)) {
                    ESP_LOGW(kTag, "Odrzucono sprzeczne ograniczenia PWM kanalu %u",
                             static_cast<unsigned>(command.channel_index + 4));
                    report_pwm_result(false);
                    continue;
                }
                if (leds_[command.channel_index + 3].operationalStatus() != ESP_OK) {
                    report_pwm_result(false);
                    continue;
                }
            } else if (command.kind == ControlCommand::Kind::button_mapping &&
                       command.channel_index < next.button_masks.size() &&
                       command.setting_value <= 31U &&
                       local_control::validMask(static_cast<std::uint8_t>(command.setting_value))) {
                next.button_masks[command.channel_index] =
                    static_cast<std::uint8_t>(command.setting_value);
            } else if (command.kind == ControlCommand::Kind::pir_delay &&
                       command.channel_index < next.off_delay_seconds.size() &&
                       command.setting_value <= local_control::kMaxOffDelaySeconds) {
                next.off_delay_seconds[command.channel_index] = command.setting_value;
            } else if (command.kind == ControlCommand::Kind::lcd_override &&
                       command.setting_value > 0U &&
                       command.setting_value <= local_control::kMaxOffDelaySeconds) {
                next.lcd_override_seconds = command.setting_value;
            } else if (command.kind == ControlCommand::Kind::lcd_mode &&
                       command.setting_value < local_control::kBacklightOptions.size()) {
                next.backlight_mode =
                    static_cast<local_control::BacklightMode>(command.setting_value);
                backlight_policy_.cancelOverride();
            } else if (command.kind == ControlCommand::Kind::restore_local_config &&
                       local_config_revision_ == 0U && local_control::valid(command.local_config)) {
                next = command.local_config;
            } else {
                report_pwm_result(false);
                continue;
            }
            if (!local_control::equal(next, local_config_) || local_config_revision_ == 0U) {
                bool pwm_ok = true;
                for (std::size_t p = 0; p < 2; ++p) {
                    if (!pwm_config::equal(next.pwm[p], local_config_.pwm[p])) {
                        const auto error = leds_[p + 3].configurePwm(next.pwm[p]);
                        if (error != ESP_OK) {
                            ESP_LOGE(kTag, "Nie udalo sie zastosowac PWM: %s",
                                     esp_err_to_name(error));
                            latchStatusFault();
                            pwm_ok = false;
                        }
                    }
                }
                if (!pwm_ok) {
                    for (std::size_t p = 0; p < 2; ++p)
                        static_cast<void>(leds_[p + 3].configurePwm(local_config_.pwm[p]));
                    report_pwm_result(false);
                    continue;
                }
                local_config_ = next;
                ++local_config_revision_;
                if (local_config_revision_ == 0U) {
                    ++local_config_revision_;
                }
            }
            report_pwm_result(true);
            continue;
        }

        if (command.kind == ControlCommand::Kind::infrared) {
#if LED_CONTROLLER_IR_NEC_ENABLED
            if (irActionNeedsLedAccess(command.infrared) &&
                irTargetHasManualDimming(command.infrared.target)) {
                if (command.infrared.repeated) {
                    continue;
                }
                if (xQueueSend(command_queue_, &command, 0) != pdPASS) {
                    ESP_LOGW(kTag, "Kolejka pelna podczas odkladania komendy IR");
                }
                continue;
            }
            processIrAction(command.infrared, now);
#else
            ESP_LOGW(kTag, "Odrzucono komende IR, poniewaz funkcja jest wylaczona");
#endif
            continue;
        }

        if (command.kind != ControlCommand::Kind::channel) {
            ESP_LOGW(kTag, "Odrzucono komende nieznanego typu");
            continue;
        }

        if (command.channel_index >= leds_.size()) {
            ESP_LOGW(kTag, "Odrzucono komende z niepoprawnym kanalem");
            continue;
        }

        if (manual_dimming_active_[command.channel_index]) {
            if (xQueueSend(command_queue_, &command, 0) != pdPASS) {
                ESP_LOGW(kTag, "Kolejka pelna podczas odkladania komendy kanalu %u",
                         static_cast<unsigned>(command.channel_index + 1U));
            }
            continue;
        }

        if (led_error_reported_[command.channel_index]) {
            continue;
        }

        const esp_err_t result = applyControlCommand(command, now);
        if (result != ESP_OK) {
            ESP_LOGE(kTag, "%s: blad komendy sieciowej PWM: %s",
                     leds_[command.channel_index].name(), esp_err_to_name(result));
            led_error_reported_[command.channel_index] = true;
            latchStatusFault();
        }
    }
}

esp_err_t LedController::applyControlCommand(const ControlCommand& command,
                                             const std::uint32_t now) noexcept {
#if LED_CONTROLLER_IR_NEC_ENABLED
    clearIrNightForChannel(command.channel_index);
#endif
    manualOverride(command.channel_index);
    LedChannel& led = leds_[command.channel_index];
    const std::uint32_t duration_ms =
        command.transition_present ? command.transition_ms : board_config::kFadeTimeMs;
    const std::uint32_t transition_ticks = static_cast<std::uint32_t>(pdMS_TO_TICKS(duration_ms));

    if (command.brightness_present && (command.brightness == 0U)) {
        const esp_err_t result = led.turnOff(now, transition_ticks);
        if (result == ESP_OK) {
            ESP_LOGI(kTag, "%s: MQTT -> OFF (brightness 0), fade %lu ms", led.name(),
                     static_cast<unsigned long>(duration_ms));
        }
        return result;
    }

    esp_err_t result = ESP_OK;
    if (command.state == ControlCommand::StateAction::turn_off) {
        result = led.turnOff(now, transition_ticks);
        if ((result == ESP_OK) && command.brightness_present) {
            result =
                led.setBrightness(mqtt_protocol::brightnessToPercent(command.brightness), now, 0);
        }
    } else {
        if (command.brightness_present) {
            result = led.setBrightness(mqtt_protocol::brightnessToPercent(command.brightness), now,
                                       transition_ticks);
        }
        if ((result == ESP_OK) && (command.state == ControlCommand::StateAction::turn_on)) {
            result = led.turnOn(now, transition_ticks);
        }
    }

    if (result == ESP_OK) {
        ESP_LOGI(kTag, "%s: MQTT -> %s, jasnosc %u%%, fade %lu ms", led.name(),
                 led.isOn() ? "ON" : "OFF", static_cast<unsigned>(led.brightness()),
                 static_cast<unsigned long>(duration_ms));
    }
    return result;
}

#if LED_CONTROLLER_IR_NEC_ENABLED
void LedController::processIrAction(const IrRemoteAction& action,
                                    const std::uint32_t now) noexcept {
    switch (action.type) {
    case IrRemoteAction::Type::none:
        return;
    case IrRemoteAction::Type::white_role_selected:
        showIrNotice("IR: BIALE AKTYWNE", now);
        ESP_LOGI(kTag, "IR: wybrano sterownik bialych LED");
        return;
    case IrRemoteAction::Type::rgb_role_selected:
        showIrNotice("IR: TRYB RGB", now);
        ESP_LOGI(kTag, "IR: wybrano przyszly sterownik RGB; lokalne komendy IR sa ignorowane");
        return;
    case IrRemoteAction::Type::selected_power:
        applyIrSelectedPower(action, now);
        return;
    case IrRemoteAction::Type::power_target_required:
        showIrNotice("IR: WYBIERZ KANAL", now);
        ESP_LOGI(kTag, "IR: POWER wymaga wybranego kanalu 1-5");
        return;
    case IrRemoteAction::Type::target_toggle:
        applyIrTargetToggle(action, now);
        return;
    case IrRemoteAction::Type::night_toggle:
        applyIrNightToggle(action, now);
        return;
    case IrRemoteAction::Type::brightness_up:
        applyIrBrightness(action, now, true);
        return;
    case IrRemoteAction::Type::brightness_down:
        applyIrBrightness(action, now, false);
        return;
    case IrRemoteAction::Type::target_changed:
        showIrTargetNotice(action.target, now);
        if (action.target == IrRemoteAction::kAllChannelsTarget) {
            ESP_LOGI(kTag, "IR: wybrano cel WSZYSTKIE");
        } else {
            ESP_LOGI(kTag, "IR: wybrano kanal %u", static_cast<unsigned>(action.target + 1U));
        }
        return;
    case IrRemoteAction::Type::timer_toggle:
        applyIrTimerToggle(action, now);
        return;
    case IrRemoteAction::Type::transition_changed: {
        std::array<char, 32> text{};
        static_cast<void>(std::snprintf(text.data(), text.size(), "IR: FADE %lu ms",
                                        static_cast<unsigned long>(action.transition_ms)));
        showIrNotice(text.data(), now);
        ESP_LOGI(kTag, "IR: czas przejscia %lu ms",
                 static_cast<unsigned long>(action.transition_ms));
        return;
    }
    case IrRemoteAction::Type::scene_previous:
        showIrNotice("IR: SCENA - REZERWA", now);
        ESP_LOGI(kTag, "IR: poprzednia scena pozostaje zarezerwowana");
        return;
    case IrRemoteAction::Type::scene_next:
        showIrNotice("IR: SCENA + REZERWA", now);
        ESP_LOGI(kTag, "IR: nastepna scena pozostaje zarezerwowana");
        return;
    }
}

void LedController::serviceIrTimers(const std::uint32_t now) noexcept {
    bool any_expired = false;
    const std::uint32_t transition_ticks =
        static_cast<std::uint32_t>(pdMS_TO_TICKS(board_config::kFadeTimeMs));
    for (std::size_t index = 0; index < leds_.size(); ++index) {
        if (!ir_timer_active_[index] || !deadlineReached(now, ir_timer_deadline_[index])) {
            continue;
        }

        ir_timer_active_[index] = false;
        ir_night_active_[index] = false;
        if (!led_error_reported_[index]) {
            handleIrChannelResult(index, "timer 30 min",
                                  leds_[index].turnOff(now, transition_ticks));
        }
        any_expired = true;
        ESP_LOGI(kTag, "%s: timer IR -> OFF", leds_[index].name());
    }

    if (any_expired) {
        showIrNotice("IR: TIMER -> OFF", now);
    }
}

void LedController::applyIrSelectedPower(const IrRemoteAction& action,
                                         const std::uint32_t now) noexcept {
    if (action.target >= leds_.size()) {
        showIrNotice("IR: WYBIERZ KANAL", now);
        ESP_LOGI(kTag, "IR: POWER wymaga wybranego kanalu 1-5");
        return;
    }

    const std::size_t index = action.target;
    if (led_error_reported_[index]) {
        showIrNotice("IR: BLAD KANALU", now);
        return;
    }

    clearIrNightForChannel(index);
    const bool turn_on = !leds_[index].isOn();
    const std::uint32_t transition_ticks =
        static_cast<std::uint32_t>(pdMS_TO_TICKS(action.transition_ms));
    const esp_err_t result = turn_on ? leds_[index].turnOn(now, transition_ticks)
                                     : leds_[index].turnOff(now, transition_ticks);
    handleIrChannelResult(index, "POWER", result);
    if (result != ESP_OK) {
        return;
    }

    std::array<char, 24> text{};
    static_cast<void>(std::snprintf(text.data(), text.size(), "IR: KANAL %u %s",
                                    static_cast<unsigned>(index + 1U), turn_on ? "ON" : "OFF"));
    showIrNotice(text.data(), now);
    ESP_LOGI(kTag, "IR: POWER kanal %u -> %s", static_cast<unsigned>(index + 1U),
             turn_on ? "ON" : "OFF");
}

void LedController::applyIrTargetToggle(const IrRemoteAction& action,
                                        const std::uint32_t now) noexcept {
    bool any_on = false;
    for (std::size_t index = 0; index < leds_.size(); ++index) {
        if (irTargetIncludes(action.target, index)) {
            any_on = any_on || leds_[index].isOn();
        }
    }

    clearIrNightForTarget(action.target);
    const std::uint32_t transition_ticks =
        static_cast<std::uint32_t>(pdMS_TO_TICKS(action.transition_ms));
    for (std::size_t index = 0; index < leds_.size(); ++index) {
        if (!irTargetIncludes(action.target, index) || led_error_reported_[index]) {
            continue;
        }
        const esp_err_t result = any_on ? leds_[index].turnOff(now, transition_ticks)
                                        : leds_[index].turnOn(now, transition_ticks);
        handleIrChannelResult(index, "przelaczenie celu", result);
    }

    showIrNotice(any_on ? "IR: CEL OFF" : "IR: CEL ON", now);
    ESP_LOGI(kTag, "IR: cel -> %s", any_on ? "OFF" : "ON");
}

void LedController::applyIrNightToggle(const IrRemoteAction& action,
                                       const std::uint32_t now) noexcept {
    bool all_in_night_mode = true;
    for (std::size_t index = 0; index < leds_.size(); ++index) {
        if (irTargetIncludes(action.target, index)) {
            all_in_night_mode = all_in_night_mode && ir_night_active_[index];
        }
    }

    const std::uint32_t transition_ticks =
        static_cast<std::uint32_t>(pdMS_TO_TICKS(action.transition_ms));
    for (std::size_t index = 0; index < leds_.size(); ++index) {
        if (!irTargetIncludes(action.target, index) || led_error_reported_[index]) {
            continue;
        }

        esp_err_t result = ESP_OK;
        if (all_in_night_mode) {
            if (ir_night_saved_on_[index]) {
                result = leds_[index].setBrightness(ir_night_saved_brightness_[index], now,
                                                    transition_ticks);
                if (result == ESP_OK) {
                    result = leds_[index].turnOn(now, transition_ticks);
                }
            } else {
                result = leds_[index].turnOff(now, transition_ticks);
                if (result == ESP_OK) {
                    result = leds_[index].setBrightness(ir_night_saved_brightness_[index], now,
                                                        transition_ticks);
                }
            }
            ir_night_active_[index] = false;
        } else {
            if (!ir_night_active_[index]) {
                ir_night_saved_on_[index] = leds_[index].isOn();
                ir_night_saved_brightness_[index] = leds_[index].brightness();
            }
            ir_night_active_[index] = true;
            result = leds_[index].setBrightness(kIrNightBrightnessPercent, now, transition_ticks);
            if (result == ESP_OK) {
                result = leds_[index].turnOn(now, transition_ticks);
            }
        }
        handleIrChannelResult(index, "tryb nocny", result);
    }

    showIrNotice(all_in_night_mode ? "IR: NOC PRZYWR." : "IR: TRYB NOCNY", now);
    ESP_LOGI(kTag, "IR: tryb nocny %s", all_in_night_mode ? "wylaczony" : "wlaczony");
}

void LedController::applyIrBrightness(const IrRemoteAction& action, const std::uint32_t now,
                                      const bool increase) noexcept {
    clearIrNightForTarget(action.target);
    const std::uint32_t transition_ticks =
        static_cast<std::uint32_t>(pdMS_TO_TICKS(action.transition_ms));

    for (std::size_t index = 0; index < leds_.size(); ++index) {
        if (!irTargetIncludes(action.target, index) || led_error_reported_[index]) {
            continue;
        }

        const std::uint8_t current = leds_[index].brightness();
        const std::uint8_t next =
            increase ? static_cast<std::uint8_t>(
                           std::min<unsigned>(100U, current + kIrBrightnessStepPercent))
                     : static_cast<std::uint8_t>((current > kIrBrightnessStepPercent)
                                                     ? current - kIrBrightnessStepPercent
                                                     : 1U);
        esp_err_t result = leds_[index].setBrightness(next, now, transition_ticks);
        if ((result == ESP_OK) && !leds_[index].isOn()) {
            result = leds_[index].turnOn(now, transition_ticks);
        }
        handleIrChannelResult(index, increase ? "jasnosc +" : "jasnosc -", result);
    }

    if (!action.repeated) {
        ESP_LOGI(kTag, "IR: jasnosc %s, krok %u%%", increase ? "+" : "-",
                 static_cast<unsigned>(kIrBrightnessStepPercent));
    }
}

void LedController::applyIrTimerToggle(const IrRemoteAction& action,
                                       const std::uint32_t now) noexcept {
    bool all_timers_active = true;
    for (std::size_t index = 0; index < leds_.size(); ++index) {
        if (irTargetIncludes(action.target, index)) {
            all_timers_active = all_timers_active && ir_timer_active_[index];
        }
    }

    const bool enable = !all_timers_active;
    const std::uint32_t timer_ticks = static_cast<std::uint32_t>(pdMS_TO_TICKS(kIrOffTimerMs));
    for (std::size_t index = 0; index < leds_.size(); ++index) {
        if (!irTargetIncludes(action.target, index)) {
            continue;
        }
        ir_timer_active_[index] = enable;
        ir_timer_deadline_[index] = now + timer_ticks;
    }

    showIrNotice(enable ? "IR: TIMER 30 MIN" : "IR: TIMER OFF", now);
    ESP_LOGI(kTag, "IR: timer 30 min %s", enable ? "wlaczony" : "wylaczony");
}

void LedController::handleIrChannelResult(const std::size_t index, const char* const operation,
                                          const esp_err_t result) noexcept {
    manualOverride(index);
    if (result == ESP_OK) {
        return;
    }

    ESP_LOGE(kTag, "%s: blad IR (%s): %s", leds_[index].name(), operation, esp_err_to_name(result));
    led_error_reported_[index] = true;
    latchStatusFault();
}

void LedController::clearIrNightForChannel(const std::size_t index) noexcept {
    ir_night_active_[index] = false;
}

void LedController::clearIrNightForTarget(const std::uint8_t target) noexcept {
    for (std::size_t index = 0; index < leds_.size(); ++index) {
        if (irTargetIncludes(target, index)) {
            ir_night_active_[index] = false;
        }
    }
}

void LedController::showIrTargetNotice(const std::uint8_t target,
                                       const std::uint32_t now) noexcept {
    if (target == IrRemoteAction::kAllChannelsTarget) {
        showIrNotice("IR: WSZYSTKIE", now);
        return;
    }

    std::array<char, 24> text{};
    static_cast<void>(std::snprintf(text.data(), text.size(), "IR: KANAL %u",
                                    static_cast<unsigned>(target + 1U)));
    showIrNotice(text.data(), now);
}

void LedController::showIrNotice(const char* const text, const std::uint32_t now) noexcept {
    ir_notice_text_.fill('\0');
    static_cast<void>(std::snprintf(ir_notice_text_.data(), ir_notice_text_.size(), "%s", text));
    ir_notice_expires_at_ = now + static_cast<std::uint32_t>(pdMS_TO_TICKS(kIrNoticeDurationMs));
    ir_notice_active_ = true;
    lcd_snapshot_published_ = false;
}

bool LedController::irTargetIncludes(const std::uint8_t target,
                                     const std::size_t channel_index) const noexcept {
    return (target == IrRemoteAction::kAllChannelsTarget) ||
           (target == static_cast<std::uint8_t>(channel_index));
}

bool LedController::irTargetHasManualDimming(const std::uint8_t target) const noexcept {
    for (std::size_t index = 0; index < manual_dimming_active_.size(); ++index) {
        if (irTargetIncludes(target, index) && manual_dimming_active_[index]) {
            return true;
        }
    }
    return false;
}

bool LedController::irActionNeedsLedAccess(const IrRemoteAction& action) const noexcept {
    switch (action.type) {
    case IrRemoteAction::Type::selected_power:
        return action.target < leds_.size();
    case IrRemoteAction::Type::target_toggle:
    case IrRemoteAction::Type::night_toggle:
    case IrRemoteAction::Type::brightness_up:
    case IrRemoteAction::Type::brightness_down:
        return true;
    case IrRemoteAction::Type::none:
    case IrRemoteAction::Type::white_role_selected:
    case IrRemoteAction::Type::rgb_role_selected:
    case IrRemoteAction::Type::power_target_required:
    case IrRemoteAction::Type::target_changed:
    case IrRemoteAction::Type::timer_toggle:
    case IrRemoteAction::Type::transition_changed:
    case IrRemoteAction::Type::scene_previous:
    case IrRemoteAction::Type::scene_next:
        return false;
    }
    return false;
}
#endif

void LedController::serviceLcd(const TickType_t now) noexcept {
    if (!lcd_service_started_) {
        return;
    }

    const esp_err_t task_fault = lcd_service_.currentFault();
    if ((task_fault != ESP_OK) && !lcd_fault_active_) {
        handleLcdFailure("magistrala I2C", task_fault);
    } else if ((task_fault == ESP_OK) && lcd_fault_active_) {
        handleLcdRecovery(now);
    }

    const auto now_value = static_cast<std::uint32_t>(now);
    const auto pir_delay = static_cast<std::uint32_t>(
        pdMS_TO_TICKS(static_cast<std::uint32_t>(local_config_.off_delay_seconds[1]) * 1000U));
    static_cast<void>(backlight_policy_.update(
        local_config_.backlight_mode, lcd_night_active_,
        pir_input_initialized_[1] && pir_inputs_[1].isActive(), now_value, pir_delay));
    if (lcd_backlight_button_initialized_) {
        const Button::Event event = lcd_backlight_button_.poll(now);
        if (event == Button::Event::long_press_started && local_ota_)
            local_ota_->requestStart();
        if (event == Button::Event::short_press) {
            backlight_policy_.toggle(
                now_value,
                static_cast<std::uint32_t>(pdMS_TO_TICKS(
                    static_cast<std::uint32_t>(local_config_.lcd_override_seconds) * 1000U)));
        }
    }
    LcdDashboard::Lines service_screen{};
    const bool service_active = local_ota_ && local_ota_->screen(service_screen);
    const esp_err_t backlight_result =
        lcd_service_.requestBacklight(service_active || backlight_policy_.isOn());
    if (backlight_result != ESP_OK) {
        handleLcdInternalFailure("ustawienie podswietlenia", backlight_result);
        return;
    }

    const TickType_t refresh_interval = pdMS_TO_TICKS(board_config::kLcdRefreshIntervalMs);
    if (lcd_snapshot_published_ && ((now - last_lcd_refresh_at_) < refresh_interval)) {
        return;
    }

    const LcdService::Snapshot snapshot = makeLcdSnapshot();
    const esp_err_t publish_result = lcd_service_.publish(snapshot);
    if (publish_result != ESP_OK) {
        handleLcdInternalFailure("kolejka danych", publish_result);
        return;
    }

    lcd_snapshot_published_ = true;
    last_lcd_refresh_at_ = now;
}

void LedController::handleLcdFailure(const char* const operation, const esp_err_t error) noexcept {
    ESP_LOGE(kTag, "LCD: %s nie powiodla sie: %s; trwa automatyczne odzyskiwanie", operation,
             esp_err_to_name(error));
    lcd_fault_active_ = true;
    if (status_led_initialized_) {
        const esp_err_t result = status_led_.latchFault();
        if (result != ESP_OK) {
            ESP_LOGE(kTag, "Nie udalo sie ustawic alarmu LCD na LED: %s", esp_err_to_name(result));
        }
    }
}

void LedController::handleLcdRecovery(const TickType_t now) noexcept {
    lcd_fault_active_ = false;
    lcd_snapshot_published_ = false;
    ESP_LOGI(kTag, "LCD: blad I2C skasowany, obsluga ekranu przywrocona");

    if (!status_led_initialized_ || system_fault_latched_ || watchdog_reset_detected_) {
        return;
    }

    const esp_err_t result = status_led_.clearFault(static_cast<std::uint32_t>(now));
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Nie udalo sie przywrocic heartbeat LED: %s", esp_err_to_name(result));
    }
}

void LedController::handleLcdInternalFailure(const char* const operation,
                                             const esp_err_t error) noexcept {
    ESP_LOGE(kTag, "LCD: %s nie powiodla sie: %s; zadanie LCD zostaje wylaczone", operation,
             esp_err_to_name(error));
    lcd_service_started_ = false;
    latchStatusFault();
}

LcdService::Snapshot LedController::makeLcdSnapshot() noexcept {
    LcdService::Snapshot snapshot{};
    for (std::size_t index = 0; index < snapshot.channels.size(); ++index) {
        snapshot.channels[index].on = leds_[index].isOn();
        snapshot.channels[index].brightness = leds_[index].displayBrightness();
    }

    snapshot.local_config = local_config_;
    snapshot.local_config_revision = local_config_revision_;
    snapshot.service_active = local_ota_ && local_ota_->screen(snapshot.service_lines);
    snapshot.status = LcdDashboard::Status::ok;
    if (system_fault_latched_ || lcd_fault_active_) {
        snapshot.status = LcdDashboard::Status::fault;
    } else if (watchdog_reset_detected_) {
        snapshot.status = LcdDashboard::Status::watchdog_reset;
    }
    snapshot.uptime_seconds = uptime_ticks_ / configTICK_RATE_HZ;

    WifiService::Status wifi{};
    if ((wifi_service_ != nullptr) && wifi_service_->tryGetStatus(wifi)) {
        switch (wifi.state) {
        case WifiService::State::unavailable:
            network_snapshot_cache_.state = LcdDashboard::NetworkState::unavailable;
            break;
        case WifiService::State::unconfigured:
            network_snapshot_cache_.state = LcdDashboard::NetworkState::unconfigured;
            break;
        case WifiService::State::ready:
            network_snapshot_cache_.state = LcdDashboard::NetworkState::ready;
            break;
        case WifiService::State::connecting:
            network_snapshot_cache_.state = LcdDashboard::NetworkState::connecting;
            break;
        case WifiService::State::connected:
            network_snapshot_cache_.state = LcdDashboard::NetworkState::connected;
            break;
        case WifiService::State::disconnected:
            network_snapshot_cache_.state = LcdDashboard::NetworkState::disconnected;
            break;
        }
        network_snapshot_cache_.ssid = wifi.ssid;
        network_snapshot_cache_.ip = wifi.ip;
        network_snapshot_cache_.local_time = wifi.local_time;
        network_snapshot_cache_.rssi_dbm = wifi.rssi_dbm;
        network_snapshot_cache_.time_synchronized = wifi.time_synchronized;
    }
    snapshot.network = network_snapshot_cache_;

    MqttService::Status mqtt{};
    if ((mqtt_service_ != nullptr) && mqtt_service_->tryGetStatus(mqtt)) {
        switch (mqtt.state) {
        case MqttService::State::unavailable:
            mqtt_snapshot_cache_.state = LcdDashboard::MqttState::unavailable;
            break;
        case MqttService::State::unconfigured:
            mqtt_snapshot_cache_.state = LcdDashboard::MqttState::unconfigured;
            break;
        case MqttService::State::ready:
            mqtt_snapshot_cache_.state = LcdDashboard::MqttState::ready;
            break;
        case MqttService::State::connecting:
            mqtt_snapshot_cache_.state = LcdDashboard::MqttState::connecting;
            break;
        case MqttService::State::connected:
            mqtt_snapshot_cache_.state = LcdDashboard::MqttState::connected;
            break;
        case MqttService::State::disconnected:
            mqtt_snapshot_cache_.state = LcdDashboard::MqttState::disconnected;
            break;
        case MqttService::State::authentication_error:
            mqtt_snapshot_cache_.state = LcdDashboard::MqttState::authentication_error;
            break;
        }
        mqtt_snapshot_cache_.publish_backlog = mqtt.publish_backlog;
    }
    snapshot.mqtt = mqtt_snapshot_cache_;
#if LED_CONTROLLER_INA228_ENABLED
    Ina228Telemetry telemetry{};
    if (lcd_service_.tryGetIna228Telemetry(telemetry)) {
        ina228_telemetry_cache_ = telemetry;
    }
    snapshot.measurement.current_available =
        ina228_telemetry_cache_.present && ina228_telemetry_cache_.valid;
    snapshot.measurement.current_milliamps = ina228_telemetry_cache_.current_milliamps;
    snapshot.measurement.current_supported = true;
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    TemperatureTelemetry temperature{};
    if (lcd_service_.tryGetTemperatureTelemetry(temperature)) {
        temperature_telemetry_cache_ = temperature;
    }
    const TemperatureRoleTelemetry& radiator = temperature_telemetry_cache_.roles[0];
    const TemperatureRoleTelemetry& enclosure = temperature_telemetry_cache_.roles[1];
    snapshot.measurement.radiator_configured = radiator.configured;
    snapshot.measurement.radiator_temperature_available = radiator.present && radiator.valid;
    snapshot.measurement.radiator_temperature_millicelsius = radiator.temperature_millicelsius;
    snapshot.measurement.enclosure_configured = enclosure.configured;
    snapshot.measurement.enclosure_temperature_available = enclosure.present && enclosure.valid;
    snapshot.measurement.enclosure_temperature_millicelsius = enclosure.temperature_millicelsius;
#endif
#if LED_CONTROLLER_IR_NEC_ENABLED
    const std::uint32_t now = static_cast<std::uint32_t>(xTaskGetTickCount());
    if (ir_notice_active_ && deadlineReached(now, ir_notice_expires_at_)) {
        ir_notice_active_ = false;
    }
    snapshot.notice.active = ir_notice_active_;
    snapshot.notice.text = ir_notice_text_;
#endif
    return snapshot;
}

void LedController::latchStatusFault() noexcept {
    system_fault_latched_ = true;
    if (!status_led_initialized_) {
        return;
    }

    const esp_err_t result = status_led_.latchFault();
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Nie udalo sie ustawic stalego alarmu LED: %s", esp_err_to_name(result));
    }
}
