#pragma once

#include "FeatureConfig.hpp"

#if LED_CONTROLLER_INA228_ENABLED

#include "Ina228Telemetry.hpp"
#include "driver/i2c_master.h"
#include "esp_err.h"

#include <cstddef>
#include <cstdint>

class Ina228 final {
  public:
    esp_err_t initialize(i2c_master_bus_handle_t bus_handle) noexcept;
    esp_err_t reset() noexcept;

    esp_err_t probe(bool& present) noexcept;
    esp_err_t identifyAndConfigure(std::uint16_t& device_id) noexcept;
    esp_err_t readTelemetry(Ina228Telemetry& telemetry) noexcept;

    [[nodiscard]] bool isInitialized() const noexcept;

  private:
    esp_err_t readRegister(std::uint8_t address, std::uint8_t* data, std::size_t size) noexcept;
    esp_err_t readRegister16(std::uint8_t address, std::uint16_t& value) noexcept;
    esp_err_t writeRegister16(std::uint8_t address, std::uint16_t value) noexcept;

    i2c_master_bus_handle_t bus_handle_{nullptr};
    i2c_master_dev_handle_t device_handle_{nullptr};
};

#endif
