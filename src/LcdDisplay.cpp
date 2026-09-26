#include "LcdDisplay.hpp"

#include "BoardConfig.hpp"
#include "esp_log.h"

#include <array>
#include <cinttypes>

namespace {
constexpr char kTag[] = "lcd";
constexpr std::uint8_t kFunctionSet4Bit2Line = 0x28;
constexpr std::uint8_t kDisplayOff = 0x08;
constexpr std::uint8_t kClearDisplay = 0x01;
constexpr std::uint8_t kEntryModeIncrement = 0x06;
constexpr std::uint8_t kDisplayOnNoCursor = 0x0C;
constexpr std::uint8_t kSetDdramAddress = 0x80;
} // namespace

esp_err_t LcdDisplay::probe(const i2c_master_bus_handle_t bus_handle,
                            bool& present) const noexcept {
    present = false;
    if (bus_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result = i2c_master_probe(bus_handle, board_config::kLcdI2cAddress,
                                              static_cast<int>(board_config::kSharedI2cTimeoutMs));
    if (result == ESP_OK) {
        present = true;
        return ESP_OK;
    }
    return (result == ESP_ERR_NOT_FOUND) ? ESP_OK : result;
}

esp_err_t LcdDisplay::initialize(const i2c_master_bus_handle_t bus_handle, const TickType_t now,
                                 const bool initial_backlight) noexcept {
    if ((bus_handle_ != nullptr) || (device_handle_ != nullptr) ||
        (initialization_state_ != InitializationState::not_started)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (bus_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    bus_handle_ = bus_handle;

    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = board_config::kLcdI2cAddress;
    device_config.scl_speed_hz = board_config::kSharedI2cFrequencyHz;
    device_config.scl_wait_us = 0;
    device_config.flags.disable_ack_check = false;

    const esp_err_t result =
        i2c_master_bus_add_device(bus_handle_, &device_config, &device_handle_);
    if (result != ESP_OK) {
        bus_handle_ = nullptr;
        initialization_state_ = InitializationState::fault;
        return result;
    }

    backlight_on_ = initial_backlight;
    initialization_pass_ = 0;
    schedule(InitializationState::set_idle, now, board_config::kLcdPowerStabilizationMs);
    return ESP_OK;
}

esp_err_t LcdDisplay::service(const TickType_t now) noexcept {
    if (initialization_state_ == InitializationState::ready) {
        return ESP_OK;
    }
    if ((initialization_state_ == InitializationState::not_started) ||
        (initialization_state_ == InitializationState::fault)) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((now - state_started_at_) < minimum_wait_ticks_) {
        return ESP_OK;
    }

    const esp_err_t result = performInitializationStep(now);
    if (result != ESP_OK) {
        initialization_state_ = InitializationState::fault;
    }
    return result;
}

esp_err_t LcdDisplay::reset() noexcept {
    const esp_err_t result = releaseDevice();
    if (result != ESP_OK) {
        initialization_state_ = InitializationState::fault;
        return result;
    }

    initialization_state_ = InitializationState::not_started;
    state_started_at_ = 0;
    minimum_wait_ticks_ = 0;
    initialization_pass_ = 0;
    backlight_on_ = false;
    written_lines_valid_ = false;
    verification_position_ = 0;
    return ESP_OK;
}

esp_err_t LcdDisplay::show(const LcdDashboard::Lines& lines) noexcept {
    if (!isReady()) {
        return ESP_ERR_INVALID_STATE;
    }

    // A full 1 Hz frame repairs LCD-side DDRAM corruption without resetting the MCU.
    for (std::size_t row = 0; row < LcdDashboard::kRowCount; ++row) {
        const esp_err_t result = writeLine(row, lines[row]);
        if (result != ESP_OK) {
            initialization_state_ = InitializationState::fault;
            return result;
        }
    }

    written_lines_ = lines;
    written_lines_valid_ = true;
    return ESP_OK;
}

esp_err_t LcdDisplay::setBacklight(const bool enabled) noexcept {
    if ((device_handle_ == nullptr) || (initialization_state_ == InitializationState::fault)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (enabled == backlight_on_) {
        return ESP_OK;
    }

    const std::uint8_t output = enabled ? board_config::kLcdPcfBacklightMask : 0U;
    const esp_err_t result = transmit(&output, 1);
    if (result == ESP_OK) {
        backlight_on_ = enabled;
    } else {
        initialization_state_ = InitializationState::fault;
    }
    return result;
}

esp_err_t LcdDisplay::toggleBacklight() noexcept {
    return setBacklight(!backlight_on_);
}

esp_err_t LcdDisplay::validateExpanderState() noexcept {
    if (!isReady() || (device_handle_ == nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    std::uint8_t expander_state = 0;
    const esp_err_t result = i2c_master_receive(
        device_handle_, &expander_state, 1, static_cast<int>(board_config::kSharedI2cTimeoutMs));
    if (result != ESP_OK) {
        initialization_state_ = InitializationState::fault;
        return result;
    }

    const std::uint8_t idle_low_mask =
        board_config::kLcdPcfRwMask | board_config::kLcdPcfEnableMask;
    if ((expander_state & idle_low_mask) != 0U) {
        initialization_state_ = InitializationState::fault;
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t LcdDisplay::readData(std::uint8_t& value) noexcept {
    // PCF8574 pins must be released HIGH before the LCD drives D4-D7.
    const std::uint8_t base = static_cast<std::uint8_t>(
        (0x0FU << board_config::kLcdPcfDataShift) | board_config::kLcdPcfRsMask |
        board_config::kLcdPcfRwMask | (backlight_on_ ? board_config::kLcdPcfBacklightMask : 0U));
    const std::array<std::uint8_t, 2> pulse{
        {base, static_cast<std::uint8_t>(base | board_config::kLcdPcfEnableMask)}};
    std::uint8_t high = 0;
    std::uint8_t low = 0;
    esp_err_t result = transmit(pulse.data(), pulse.size());
    if (result == ESP_OK) {
        result = i2c_master_receive(device_handle_, &high, 1, board_config::kSharedI2cTimeoutMs);
    }
    if (result == ESP_OK) {
        result = transmit(pulse.data(), pulse.size());
    }
    if (result == ESP_OK) {
        result = i2c_master_receive(device_handle_, &low, 1, board_config::kSharedI2cTimeoutMs);
    }
    // Lower E while RW is still HIGH, then return to write mode without driving the data bus LOW.
    const std::array<std::uint8_t, 2> idle{
        {base, static_cast<std::uint8_t>(
                   base & ~(board_config::kLcdPcfRwMask | board_config::kLcdPcfRsMask))}};
    const esp_err_t cleanup = transmit(idle.data(), idle.size());
    if (result != ESP_OK) {
        return result;
    }
    if (cleanup != ESP_OK) {
        return cleanup;
    }
    value = static_cast<std::uint8_t>((high & 0xF0U) | ((low >> 4U) & 0x0FU));
    return ESP_OK;
}

esp_err_t LcdDisplay::verifyNextCharacter() noexcept {
    if (!isReady() || !written_lines_valid_) {
        return ESP_OK;
    }
    const std::size_t row = verification_position_ / LcdDashboard::kColumnCount;
    const std::size_t column = verification_position_ % LcdDashboard::kColumnCount;
    const std::uint8_t address =
        static_cast<std::uint8_t>(board_config::kLcdDdramRowOffsets[row] + column);
    esp_err_t result = sendCommand(static_cast<std::uint8_t>(kSetDdramAddress | address));
    std::uint8_t actual = 0;
    // I2C transfers themselves exceed the HD44780 instruction setup time; no busy wait.
    if (result == ESP_OK) {
        result = readData(actual);
    }
    if (result != ESP_OK) {
        return result;
    }
    if (actual != static_cast<std::uint8_t>(written_lines_[row][column])) {
        ESP_LOGW(kTag, "DDRAM mismatch row=%u col=%u expected=0x%02X actual=0x%02X",
                 static_cast<unsigned>(row), static_cast<unsigned>(column),
                 static_cast<unsigned>(static_cast<std::uint8_t>(written_lines_[row][column])),
                 static_cast<unsigned>(actual));
        return ESP_ERR_INVALID_CRC;
    }
    verification_position_ =
        (verification_position_ + 1U) % (LcdDashboard::kRowCount * LcdDashboard::kColumnCount);
    return ESP_OK;
}

bool LcdDisplay::isReady() const noexcept {
    return initialization_state_ == InitializationState::ready;
}

bool LcdDisplay::isBacklightOn() const noexcept {
    return backlight_on_;
}

void LcdDisplay::appendNibble(std::uint8_t* const buffer, std::size_t& offset,
                              const std::uint8_t nibble, const bool data) const noexcept {
    std::uint8_t output = static_cast<std::uint8_t>(
        (nibble & 0x0FU) << static_cast<unsigned>(board_config::kLcdPcfDataShift));
    if (data) {
        output = static_cast<std::uint8_t>(output | board_config::kLcdPcfRsMask);
    }
    if (backlight_on_) {
        output = static_cast<std::uint8_t>(output | board_config::kLcdPcfBacklightMask);
    }

    buffer[offset++] = output;
    buffer[offset++] = static_cast<std::uint8_t>(output | board_config::kLcdPcfEnableMask);
    buffer[offset++] = output;
}

void LcdDisplay::appendByte(std::uint8_t* const buffer, std::size_t& offset,
                            const std::uint8_t value, const bool data) const noexcept {
    appendNibble(buffer, offset, static_cast<std::uint8_t>(value >> 4U), data);
    appendNibble(buffer, offset, static_cast<std::uint8_t>(value & 0x0FU), data);
}

void LcdDisplay::schedule(const InitializationState next_state, const TickType_t now,
                          const std::uint32_t minimum_delay_ms) noexcept {
    const TickType_t converted_ticks = pdMS_TO_TICKS(minimum_delay_ms);
    initialization_state_ = next_state;
    state_started_at_ = now;
    minimum_wait_ticks_ = (converted_ticks > 0U) ? converted_ticks : 1U;
}

esp_err_t LcdDisplay::transmit(const std::uint8_t* const data, const std::size_t size) noexcept {
    if (device_handle_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(device_handle_, data, size,
                               static_cast<int>(board_config::kSharedI2cTimeoutMs));
}

esp_err_t LcdDisplay::sendNibble(const std::uint8_t nibble) noexcept {
    std::array<std::uint8_t, kExpanderWritesPerNibble> transfer{};
    std::size_t offset = 0;
    appendNibble(transfer.data(), offset, nibble, false);
    return transmit(transfer.data(), offset);
}

esp_err_t LcdDisplay::sendCommand(const std::uint8_t command) noexcept {
    std::array<std::uint8_t, kExpanderWritesPerByte> transfer{};
    std::size_t offset = 0;
    appendByte(transfer.data(), offset, command, false);
    return transmit(transfer.data(), offset);
}

esp_err_t LcdDisplay::writeLine(const std::size_t row, const LcdDashboard::Line& line) noexcept {
    if (row >= board_config::kLcdDdramRowOffsets.size()) {
        return ESP_ERR_INVALID_ARG;
    }

    std::array<std::uint8_t, kLineTransferSize> transfer{};
    std::size_t offset = 0;
    const std::uint8_t address =
        static_cast<std::uint8_t>(kSetDdramAddress | board_config::kLcdDdramRowOffsets[row]);
    appendByte(transfer.data(), offset, address, false);

    for (std::size_t column = 0; column < LcdDashboard::kColumnCount; ++column) {
        appendByte(transfer.data(), offset, static_cast<std::uint8_t>(line[column]), true);
    }
    return transmit(transfer.data(), offset);
}

esp_err_t LcdDisplay::performInitializationStep(const TickType_t now) noexcept {
    esp_err_t result = ESP_OK;

    switch (initialization_state_) {
    case InitializationState::set_idle: {
        const std::uint8_t idle_output = backlight_on_ ? board_config::kLcdPcfBacklightMask : 0U;
        result = transmit(&idle_output, 1);
        if (result == ESP_OK) {
            schedule(InitializationState::reset_1, now, 1);
        }
        break;
    }
    case InitializationState::reset_1:
        result = sendNibble(0x03);
        if (result == ESP_OK) {
            schedule(InitializationState::reset_2, now, 5);
        }
        break;
    case InitializationState::reset_2:
        result = sendNibble(0x03);
        if (result == ESP_OK) {
            schedule(InitializationState::reset_3, now, 1);
        }
        break;
    case InitializationState::reset_3:
        result = sendNibble(0x03);
        if (result == ESP_OK) {
            schedule(InitializationState::switch_to_4_bit, now, 1);
        }
        break;
    case InitializationState::switch_to_4_bit:
        result = sendNibble(0x02);
        if (result == ESP_OK) {
            schedule(InitializationState::function_set, now, 1);
        }
        break;
    case InitializationState::function_set:
        result = sendCommand(kFunctionSet4Bit2Line);
        if (result == ESP_OK) {
            schedule(InitializationState::display_off, now, 1);
        }
        break;
    case InitializationState::display_off:
        result = sendCommand(kDisplayOff);
        if (result == ESP_OK) {
            schedule(InitializationState::clear_display, now, 1);
        }
        break;
    case InitializationState::clear_display:
        result = sendCommand(kClearDisplay);
        if (result == ESP_OK) {
            schedule(InitializationState::entry_mode, now, 2);
        }
        break;
    case InitializationState::entry_mode:
        result = sendCommand(kEntryModeIncrement);
        if (result == ESP_OK) {
            schedule(InitializationState::display_on, now, 1);
        }
        break;
    case InitializationState::display_on:
        result = sendCommand(kDisplayOnNoCursor);
        if (result == ESP_OK) {
            ++initialization_pass_;
            if (initialization_pass_ < board_config::kLcdInitializationPassCount) {
                schedule(InitializationState::set_idle, now,
                         board_config::kLcdInitializationPassGapMs);
            } else {
                initialization_state_ = InitializationState::ready;
                ESP_LOGI(kTag,
                         "LCD 20x4 gotowy po %u przebiegach: PCF8574AT 7-bit 0x%02" PRIX16
                         ", I2C %" PRIu32 " Hz",
                         static_cast<unsigned>(initialization_pass_), board_config::kLcdI2cAddress,
                         board_config::kSharedI2cFrequencyHz);
            }
        }
        break;
    case InitializationState::not_started:
    case InitializationState::ready:
    case InitializationState::fault:
        result = ESP_ERR_INVALID_STATE;
        break;
    }

    return result;
}

esp_err_t LcdDisplay::releaseDevice() noexcept {
    if (device_handle_ != nullptr) {
        esp_err_t result = i2c_master_bus_rm_device(device_handle_);
        if ((result != ESP_OK) && (bus_handle_ != nullptr)) {
            const esp_err_t reset_result = i2c_master_bus_reset(bus_handle_);
            if (reset_result != ESP_OK) {
                return reset_result;
            }
            result = i2c_master_bus_rm_device(device_handle_);
        }
        if (result != ESP_OK) {
            return result;
        }
        device_handle_ = nullptr;
    }

    bus_handle_ = nullptr;

    return ESP_OK;
}
