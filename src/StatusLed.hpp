#pragma once

#include "StatusIndicator.hpp"
#include "driver/gpio.h"
#include "esp_err.h"

#include <cstdint>

class StatusLed final {
  public:
    StatusLed(gpio_num_t pin, bool active_high, std::uint32_t toggle_interval) noexcept;

    esp_err_t initialize(bool fault_latched, std::uint32_t now) noexcept;
    esp_err_t update(std::uint32_t now) noexcept;
    esp_err_t latchFault() noexcept;
    esp_err_t clearFault(std::uint32_t now) noexcept;

    [[nodiscard]] bool isFaultLatched() const noexcept;

  private:
    [[nodiscard]] int physicalLevel(bool illuminated) const noexcept;
    esp_err_t synchronize() noexcept;

    gpio_num_t pin_;
    bool active_high_;
    StatusIndicator indicator_;
    bool initialized_{false};
    bool applied_illuminated_{false};
};
