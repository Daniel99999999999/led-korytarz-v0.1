#include "I2cBus.hpp"

#include "BoardConfig.hpp"
#include "esp_log.h"

namespace {
constexpr char kEspIdfI2cLogTag[] = "i2c.master";
}

esp_err_t I2cBus::initialize() noexcept {
    if (isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!driver_log_suppressed_) {
        // Clients report returned errors once at the service boundary. The ESP-IDF driver logs
        // every failed probe and otherwise floods UART while an optional device is disconnected.
        esp_log_level_set(kEspIdfI2cLogTag, ESP_LOG_NONE);
        driver_log_suppressed_ = true;
    }

    i2c_master_bus_config_t config{};
    config.i2c_port = board_config::kSharedI2cPort;
    config.sda_io_num = board_config::kSharedI2cSdaPin;
    config.scl_io_num = board_config::kSharedI2cSclPin;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.intr_priority = 0;
    config.trans_queue_depth = 0;
    config.flags.enable_internal_pullup = false;
    config.flags.allow_pd = false;

    return i2c_new_master_bus(&config, &handle_);
}

esp_err_t I2cBus::recover() noexcept {
    if (handle_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    // Reset only the hardware state machine. Device handles owned by the LCD, EEPROM and INA228
    // remain valid, so a fault of one endpoint cannot detach the other clients.
    return i2c_master_bus_reset(handle_);
}

esp_err_t I2cBus::reset() noexcept {
    if (handle_ == nullptr) {
        return ESP_OK;
    }

    const esp_err_t result = i2c_del_master_bus(handle_);
    if (result == ESP_OK) {
        handle_ = nullptr;
    }
    return result;
}

i2c_master_bus_handle_t I2cBus::handle() const noexcept {
    return handle_;
}

bool I2cBus::isInitialized() const noexcept {
    return handle_ != nullptr;
}
