#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include "At24c08.hpp"
#include "BoardConfig.hpp"
#include "esp_timer.h"

#include <algorithm>
#include <array>

esp_err_t At24c08::initialize(const i2c_master_bus_handle_t bus_handle) noexcept {
    if (isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (bus_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    bus_handle_ = bus_handle;

    esp_err_t result = ESP_OK;
    for (std::size_t block = 0; block < device_handles_.size(); ++block) {
        i2c_device_config_t device_config{};
        device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device_config.device_address = deviceAddress(block);
        device_config.scl_speed_hz = board_config::kSharedI2cFrequencyHz;
        device_config.scl_wait_us = 0;
        device_config.flags.disable_ack_check = false;

        result = i2c_master_bus_add_device(bus_handle_, &device_config, &device_handles_[block]);
        if (result != ESP_OK) {
            const esp_err_t release_result = releaseDevices();
            return (release_result == ESP_OK) ? result : release_result;
        }
    }

    pending_block_ = 0;
    write_started_us_ = 0;
    next_probe_us_ = 0;
    write_pending_ = false;
    return ESP_OK;
}

esp_err_t At24c08::reset() noexcept {
    const esp_err_t result = releaseDevices();
    if (result == ESP_OK) {
        pending_block_ = 0;
        write_started_us_ = 0;
        next_probe_us_ = 0;
        write_pending_ = false;
    }
    return result;
}

esp_err_t At24c08::probe(bool& present) noexcept {
    present = false;
    if (!isInitialized() || write_pending_) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = i2c_master_probe(
        bus_handle_, deviceAddress(0), static_cast<int>(board_config::kSettingsProbeTimeoutMs));
    if (result == ESP_OK) {
        present = true;
        return ESP_OK;
    }
    return (result == ESP_ERR_NOT_FOUND) ? ESP_OK : result;
}

esp_err_t At24c08::read(const std::uint16_t address, std::uint8_t* const data,
                        const std::size_t size) noexcept {
    if (!isInitialized() || write_pending_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (((data == nullptr) && (size != 0U)) ||
        (static_cast<std::size_t>(address) + size > board_config::kSettingsEepromSize)) {
        return ESP_ERR_INVALID_ARG;
    }

    std::size_t remaining = size;
    std::size_t output_offset = 0;
    std::uint16_t current_address = address;
    while (remaining > 0U) {
        const std::size_t block = blockIndex(current_address);
        const std::size_t offset_in_block =
            static_cast<std::size_t>(current_address) % board_config::kSettingsEepromBlockSize;
        const std::size_t transfer_size =
            std::min(remaining, board_config::kSettingsEepromBlockSize - offset_in_block);
        const std::uint8_t word_address = static_cast<std::uint8_t>(offset_in_block);

        const esp_err_t result = i2c_master_transmit_receive(
            device_handles_[block], &word_address, 1, data + output_offset, transfer_size,
            static_cast<int>(board_config::kSharedI2cTimeoutMs));
        if (result != ESP_OK) {
            return result;
        }

        current_address = static_cast<std::uint16_t>(current_address + transfer_size);
        output_offset += transfer_size;
        remaining -= transfer_size;
    }
    return ESP_OK;
}

esp_err_t At24c08::beginPageWrite(const std::uint16_t address, const std::uint8_t* const data,
                                  const std::size_t size) noexcept {
    if (!isInitialized() || write_pending_) {
        return ESP_ERR_INVALID_STATE;
    }
    const std::size_t numeric_address = static_cast<std::size_t>(address);
    const std::size_t page_offset = numeric_address % board_config::kSettingsEepromPageSize;
    if ((data == nullptr) || (size == 0U) || (size > board_config::kSettingsEepromPageSize) ||
        (page_offset + size > board_config::kSettingsEepromPageSize) ||
        (numeric_address + size > board_config::kSettingsEepromSize)) {
        return ESP_ERR_INVALID_ARG;
    }

    const std::size_t block = blockIndex(address);
    const std::uint8_t word_address =
        static_cast<std::uint8_t>(numeric_address % board_config::kSettingsEepromBlockSize);
    std::array<std::uint8_t, board_config::kSettingsEepromPageSize + 1U> transfer{};
    transfer[0] = word_address;
    std::copy_n(data, size, transfer.begin() + 1);

    const esp_err_t result =
        i2c_master_transmit(device_handles_[block], transfer.data(), size + 1U,
                            static_cast<int>(board_config::kSharedI2cTimeoutMs));
    if (result == ESP_OK) {
        const std::int64_t now_us = esp_timer_get_time();
        pending_block_ = block;
        write_started_us_ = now_us;
        next_probe_us_ = now_us + board_config::kSettingsWritePollIntervalUs;
        write_pending_ = true;
    }
    return result;
}

esp_err_t At24c08::pollWriteComplete(bool& complete) noexcept {
    complete = false;
    if (!isInitialized() || !write_pending_) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::int64_t now_us = esp_timer_get_time();
    if (now_us < next_probe_us_) {
        return ESP_OK;
    }

    const esp_err_t result =
        i2c_master_probe(bus_handle_, deviceAddress(pending_block_),
                         static_cast<int>(board_config::kSettingsProbeTimeoutMs));
    if (result == ESP_OK) {
        write_pending_ = false;
        complete = true;
        return ESP_OK;
    }
    if (result == ESP_ERR_NOT_FOUND) {
        // Check the deadline only after a failed ACK probe. The service task can wake slightly
        // late, and a ready EEPROM must not be reported as timed out solely due to scheduling.
        if ((now_us - write_started_us_) >= board_config::kSettingsWriteCycleTimeoutUs) {
            write_pending_ = false;
            return ESP_ERR_TIMEOUT;
        }
        next_probe_us_ = now_us + board_config::kSettingsWritePollIntervalUs;
        return ESP_OK;
    }
    write_pending_ = false;
    return result;
}

bool At24c08::isInitialized() const noexcept {
    return bus_handle_ != nullptr;
}

bool At24c08::isWritePending() const noexcept {
    return write_pending_;
}

std::size_t At24c08::blockIndex(const std::uint16_t address) noexcept {
    return static_cast<std::size_t>(address) / board_config::kSettingsEepromBlockSize;
}

std::uint16_t At24c08::deviceAddress(const std::size_t block) noexcept {
    return static_cast<std::uint16_t>(board_config::kSettingsEepromBaseAddress + block);
}

esp_err_t At24c08::releaseDevices() noexcept {
    esp_err_t first_error = ESP_OK;
    for (std::size_t index = device_handles_.size(); index > 0U; --index) {
        i2c_master_dev_handle_t& handle = device_handles_[index - 1U];
        if (handle == nullptr) {
            continue;
        }

        const esp_err_t result = i2c_master_bus_rm_device(handle);
        if (result == ESP_OK) {
            handle = nullptr;
        } else if (first_error == ESP_OK) {
            first_error = result;
        }
    }

    if (first_error == ESP_OK) {
        bus_handle_ = nullptr;
    }
    return first_error;
}

#endif
