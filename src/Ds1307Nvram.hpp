#pragma once

#include "LocalConfigRecord.hpp"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"

class LedController;

class Ds1307Nvram final {
  public:
    // Only LcdService may call this: one I2C owner, at most one transaction per call.
    esp_err_t service(i2c_master_bus_handle_t bus, LedController& controller,
                      const local_control::Config& config, std::uint32_t revision,
                      TickType_t now) noexcept;

  private:
    enum class Phase : std::uint8_t { load, restore, idle, invalidate, write, commit, verify };
    esp_err_t write(std::uint8_t address, const std::uint8_t* data, std::size_t size) noexcept;
    esp_err_t read(std::uint8_t address, std::uint8_t* data, std::size_t size) noexcept;
    i2c_master_dev_handle_t device_{nullptr};
    local_control::Config stored_{};
    local_control::Config saving_{};
    local_control::Record record_{};
    Phase phase_{Phase::load};
    TickType_t retry_at_{0};
    TickType_t changed_at_{0};
    std::uint32_t observed_revision_{0};
    std::uint32_t sequence_{0};
    int active_slot_{-1};
    std::uint8_t target_address_{0x08};
    bool stored_valid_{false};
    bool retry_pending_{false};
    bool unavailable_reported_{false};
};
