#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

class I2cBus final {
  public:
    esp_err_t initialize() noexcept;
    esp_err_t recover() noexcept;
    esp_err_t reset() noexcept;

    [[nodiscard]] i2c_master_bus_handle_t handle() const noexcept;
    [[nodiscard]] bool isInitialized() const noexcept;

  private:
    i2c_master_bus_handle_t handle_{nullptr};
    bool driver_log_suppressed_{false};
};
