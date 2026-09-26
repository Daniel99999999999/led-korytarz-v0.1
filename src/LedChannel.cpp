#include "LedChannel.hpp"

#include "BoardConfig.hpp"
#include "esp_log.h"

#include <cinttypes>

namespace {
constexpr char kTag[] = "led_channel";
}

bool LedChannel::peripheral_initialized_ = false;

LedChannel::LedChannel(const char* const name, const gpio_num_t pwm_pin,
                       const ledc_channel_t channel) noexcept
    : name_(name), pwm_pin_(pwm_pin), channel_(channel) {}

esp_err_t LedChannel::initializePeripheral() noexcept {
    if (peripheral_initialized_) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_config{};
    timer_config.speed_mode = board_config::kLedcSpeedMode;
    timer_config.duty_resolution = board_config::kPwmResolution;
    timer_config.timer_num = board_config::kLedcTimer;
    timer_config.freq_hz = board_config::kPwmFrequencyHz;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    timer_config.deconfigure = false;

    esp_err_t result = ledc_timer_config(&timer_config);
    if (result != ESP_OK) {
        return result;
    }

    peripheral_initialized_ = true;
    ESP_LOGI(kTag, "LEDC: %" PRIu32 " Hz, rozdzielczosc 12 bitow", board_config::kPwmFrequencyHz);
    return ESP_OK;
}

esp_err_t LedChannel::prepareOutputLow() noexcept {
    esp_err_t result = gpio_set_level(pwm_pin_, 0);
    if (result != ESP_OK) {
        return result;
    }

    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << static_cast<unsigned>(pwm_pin_);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&config);
}

esp_err_t LedChannel::initialize() noexcept {
    if (!peripheral_initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    ledc_channel_config_t channel_config{};
    channel_config.gpio_num = static_cast<int>(pwm_pin_);
    channel_config.speed_mode = board_config::kLedcSpeedMode;
    channel_config.channel = channel_;
    channel_config.timer_sel = board_config::kLedcTimer;
    channel_config.duty = 0;
    channel_config.hpoint = 0;
    channel_config.sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;
    channel_config.flags.output_invert = 0;

    const esp_err_t result = ledc_channel_config(&channel_config);
    if (result != ESP_OK) {
        return result;
    }

    initialized_ = true;
    state_.reset(board_config::kDefaultBrightnessPercent);
    transition_.reset(0, 0);
    applied_duty_ = 0;
    output_fault_ = ESP_OK;

    ESP_LOGI(kTag, "%s: GPIO %d, kanal LEDC %d, start OFF", name_, static_cast<int>(pwm_pin_),
             static_cast<int>(channel_));
    return ESP_OK;
}

esp_err_t LedChannel::turnOn(const std::uint32_t now,
                             const std::uint32_t transition_duration) noexcept {
    const esp_err_t status = operationalStatus();
    if (status != ESP_OK) {
        return status;
    }

    if (!state_.isOn()) {
        power_transition_ = true;
    }
    state_.turnOn();
    return scheduleTarget(state_.targetDuty(board_config::kPwmMaxDuty), transition_duration, now);
}

esp_err_t LedChannel::turnOff(const std::uint32_t now,
                              const std::uint32_t transition_duration) noexcept {
    const esp_err_t status = operationalStatus();
    if (status != ESP_OK) {
        return status;
    }

    power_transition_ = true;
    state_.turnOff();
    return scheduleTarget(0, transition_duration, now);
}

esp_err_t LedChannel::toggle(const std::uint32_t now,
                             const std::uint32_t transition_duration) noexcept {
    const esp_err_t status = operationalStatus();
    if (status != ESP_OK) {
        return status;
    }

    power_transition_ = true;
    state_.toggle();
    return scheduleTarget(state_.targetDuty(board_config::kPwmMaxDuty), transition_duration, now);
}

esp_err_t LedChannel::setBrightness(const std::uint8_t percent, const std::uint32_t now,
                                    const std::uint32_t transition_duration) noexcept {
    const esp_err_t status = operationalStatus();
    if (status != ESP_OK) {
        return status;
    }

    power_transition_ = false;
    state_.setBrightness(percent);
    if (!state_.isOn()) {
        return ESP_OK;
    }

    return scheduleTarget(state_.targetDuty(board_config::kPwmMaxDuty), transition_duration, now);
}

esp_err_t LedChannel::beginDimming(const std::uint32_t now) noexcept {
    const esp_err_t status = operationalStatus();
    if (status != ESP_OK) {
        return status;
    }

    power_transition_ = false;
    if (!state_.beginDimming()) {
        return ESP_OK;
    }
    return scheduleTarget(state_.targetDuty(board_config::kPwmMaxDuty), 0, now);
}

esp_err_t LedChannel::stepDimming(const std::uint32_t now) noexcept {
    const esp_err_t status = operationalStatus();
    if (status != ESP_OK) {
        return status;
    }

    if (!state_.stepDimming()) {
        return ESP_OK;
    }
    return scheduleTarget(state_.targetDuty(board_config::kPwmMaxDuty), 0, now);
}

void LedChannel::endDimming() noexcept {
    if (operationalStatus() != ESP_OK) {
        return;
    }
    state_.endDimming();
}

esp_err_t LedChannel::update(const std::uint32_t now) noexcept {
    const esp_err_t status = operationalStatus();
    if (status != ESP_OK) {
        return status;
    }

    static_cast<void>(transition_.update(now));
    return synchronize();
}

bool LedChannel::isOn() const noexcept {
    return state_.isOn();
}

std::uint8_t LedChannel::brightness() const noexcept {
    return state_.brightness();
}

std::uint8_t LedChannel::displayBrightness() const noexcept {
    return (!power_transition_ && transition_.isActive()) ? appliedBrightness() : brightness();
}

std::uint8_t LedChannel::appliedBrightness() const noexcept {
    const std::uint64_t scaled = static_cast<std::uint64_t>(transition_.currentDuty()) * 100U;
    return static_cast<std::uint8_t>((scaled + (board_config::kPwmMaxDuty / 2U)) /
                                     board_config::kPwmMaxDuty);
}

bool LedChannel::isTransitionActive() const noexcept {
    return transition_.isActive();
}

const char* LedChannel::name() const noexcept {
    return name_;
}

esp_err_t LedChannel::operationalStatus() const noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    return output_fault_;
}

esp_err_t LedChannel::scheduleTarget(const std::uint32_t duty,
                                     const std::uint32_t transition_duration,
                                     const std::uint32_t now) noexcept {
    static_cast<void>(transition_.retarget(duty, transition_duration, now));
    return synchronize();
}

esp_err_t LedChannel::synchronize() noexcept {
    const esp_err_t status = operationalStatus();
    if (status != ESP_OK) {
        return status;
    }

    const std::uint32_t requested_duty = transition_.currentDuty();
    if (requested_duty == applied_duty_) {
        return ESP_OK;
    }

    esp_err_t result = ledc_set_duty(board_config::kLedcSpeedMode, channel_, requested_duty);
    if (result != ESP_OK) {
        return latchOutputFault(result);
    }

    result = ledc_update_duty(board_config::kLedcSpeedMode, channel_);
    if (result != ESP_OK) {
        return latchOutputFault(result);
    }

    applied_duty_ = requested_duty;
    return ESP_OK;
}

esp_err_t LedChannel::latchOutputFault(const esp_err_t error) noexcept {
    if (output_fault_ != ESP_OK) {
        return output_fault_;
    }

    output_fault_ = error;
    static_cast<void>(ledc_stop(board_config::kLedcSpeedMode, channel_, 0));
    power_transition_ = true;
    state_.turnOff();
    transition_.reset(0, 0);
    applied_duty_ = 0;
    return output_fault_;
}
