#pragma once

#include "ChannelState.hpp"
#include "DutyTransition.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"

#include <cstdint>

class LedChannel final {
  public:
    LedChannel(const char* name, gpio_num_t pwm_pin, ledc_channel_t channel) noexcept;

    static esp_err_t initializePeripheral() noexcept;

    esp_err_t prepareOutputLow() noexcept;
    esp_err_t initialize() noexcept;
    esp_err_t turnOn(std::uint32_t now, std::uint32_t transition_duration) noexcept;
    esp_err_t turnOff(std::uint32_t now, std::uint32_t transition_duration) noexcept;
    esp_err_t toggle(std::uint32_t now, std::uint32_t transition_duration) noexcept;
    esp_err_t setBrightness(std::uint8_t percent, std::uint32_t now,
                            std::uint32_t transition_duration) noexcept;
    esp_err_t beginDimming(std::uint32_t now) noexcept;
    esp_err_t stepDimming(std::uint32_t now) noexcept;
    void endDimming() noexcept;
    esp_err_t update(std::uint32_t now) noexcept;

    [[nodiscard]] bool isOn() const noexcept;
    [[nodiscard]] std::uint8_t brightness() const noexcept;
    [[nodiscard]] std::uint8_t displayBrightness() const noexcept;
    [[nodiscard]] std::uint8_t appliedBrightness() const noexcept;
    [[nodiscard]] bool isTransitionActive() const noexcept;
    [[nodiscard]] const char* name() const noexcept;

  private:
    [[nodiscard]] esp_err_t operationalStatus() const noexcept;
    esp_err_t scheduleTarget(std::uint32_t duty, std::uint32_t transition_duration,
                             std::uint32_t now) noexcept;
    esp_err_t synchronize() noexcept;
    esp_err_t latchOutputFault(esp_err_t error) noexcept;

    const char* name_;
    gpio_num_t pwm_pin_;
    ledc_channel_t channel_;
    bool initialized_{false};
    bool power_transition_{false};
    ChannelState state_{};
    DutyTransition transition_{};
    std::uint32_t applied_duty_{0};
    esp_err_t output_fault_{ESP_OK};

    static bool peripheral_initialized_;
};
