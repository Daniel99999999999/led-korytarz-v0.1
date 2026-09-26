#pragma once

#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <array>
#include <cstddef>
#include <cstdint>

class At24c08 final {
  public:
    esp_err_t initialize(i2c_master_bus_handle_t bus_handle) noexcept;
    esp_err_t reset() noexcept;

    esp_err_t probe(bool& present) noexcept;
    esp_err_t read(std::uint16_t address, std::uint8_t* data, std::size_t size) noexcept;
    esp_err_t beginPageWrite(std::uint16_t address, const std::uint8_t* data,
                             std::size_t size) noexcept;
    esp_err_t pollWriteComplete(bool& complete) noexcept;

    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] bool isWritePending() const noexcept;

  private:
    static constexpr std::size_t kDeviceCount = 4;

    [[nodiscard]] static std::size_t blockIndex(std::uint16_t address) noexcept;
    [[nodiscard]] static std::uint16_t deviceAddress(std::size_t block) noexcept;
    esp_err_t releaseDevices() noexcept;

    i2c_master_bus_handle_t bus_handle_{nullptr};
    std::array<i2c_master_dev_handle_t, kDeviceCount> device_handles_{};
    std::size_t pending_block_{0};
    std::int64_t write_started_us_{0};
    std::int64_t next_probe_us_{0};
    bool write_pending_{false};
};

#endif
