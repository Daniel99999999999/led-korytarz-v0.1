#pragma once

#include "ButtonDebouncer.hpp"
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include <cstdint>

class PirInput final {
  public:
    enum class Event : std::uint8_t {
        none,
        activated,
        deactivated,
    };

    PirInput(const char* name, gpio_num_t pin, bool active_high,
             std::uint32_t debounce_ms) noexcept;

    esp_err_t initialize() noexcept;
    [[nodiscard]] Event poll(TickType_t now) noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] const char* name() const noexcept;

  private:
    [[nodiscard]] bool readActive() const noexcept;

    const char* name_;
    gpio_num_t pin_;
    bool active_high_;
    ButtonDebouncer debouncer_;
    bool initialized_{false};
};
