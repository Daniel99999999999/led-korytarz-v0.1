#pragma once

#include "ButtonDebouncer.hpp"
#include "ButtonGesture.hpp"
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include <cstdint>

class Button final {
  public:
    using Event = ButtonGesture::Event;

    Button(const char* name, gpio_num_t pin, bool active_low, std::uint32_t debounce_ms,
           std::uint32_t long_press_ms, std::uint32_t step_interval_ms,
           bool enable_internal_bias = true) noexcept;

    esp_err_t initialize() noexcept;
    [[nodiscard]] Event poll(TickType_t now) noexcept;
    [[nodiscard]] const char* name() const noexcept;

  private:
    [[nodiscard]] bool readPressed() const noexcept;

    const char* name_;
    gpio_num_t pin_;
    bool active_low_;
    bool enable_internal_bias_;
    ButtonDebouncer debouncer_;
    ButtonGesture gesture_;

    bool initialized_{false};
};
