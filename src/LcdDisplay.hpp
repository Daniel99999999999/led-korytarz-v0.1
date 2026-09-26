#pragma once

#include "LcdDashboard.hpp"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include <array>
#include <cstddef>
#include <cstdint>

class LcdDisplay final {
  public:
    esp_err_t probe(i2c_master_bus_handle_t bus_handle, bool& present) const noexcept;
    esp_err_t initialize(i2c_master_bus_handle_t bus_handle, TickType_t now,
                         bool initial_backlight) noexcept;
    esp_err_t service(TickType_t now) noexcept;
    esp_err_t reset() noexcept;
    esp_err_t show(const LcdDashboard::Lines& lines) noexcept;
    esp_err_t setBacklight(bool enabled) noexcept;
    esp_err_t toggleBacklight() noexcept;
    esp_err_t validateExpanderState() noexcept;
    esp_err_t verifyNextCharacter() noexcept;

    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] bool isBacklightOn() const noexcept;

  private:
    enum class InitializationState : std::uint8_t {
        not_started,
        set_idle,
        reset_1,
        reset_2,
        reset_3,
        switch_to_4_bit,
        function_set,
        display_off,
        clear_display,
        entry_mode,
        display_on,
        ready,
        fault,
    };

    static constexpr std::size_t kExpanderWritesPerNibble = 3;
    static constexpr std::size_t kExpanderWritesPerByte = 2U * kExpanderWritesPerNibble;
    static constexpr std::size_t kLineTransferSize =
        (LcdDashboard::kColumnCount + 1U) * kExpanderWritesPerByte;

    void appendNibble(std::uint8_t* buffer, std::size_t& offset, std::uint8_t nibble,
                      bool data) const noexcept;
    void appendByte(std::uint8_t* buffer, std::size_t& offset, std::uint8_t value,
                    bool data) const noexcept;
    void schedule(InitializationState next_state, TickType_t now,
                  std::uint32_t minimum_delay_ms) noexcept;
    esp_err_t transmit(const std::uint8_t* data, std::size_t size) noexcept;
    esp_err_t sendNibble(std::uint8_t nibble) noexcept;
    esp_err_t sendCommand(std::uint8_t command) noexcept;
    esp_err_t readData(std::uint8_t& value) noexcept;
    esp_err_t writeLine(std::size_t row, const LcdDashboard::Line& line) noexcept;
    esp_err_t performInitializationStep(TickType_t now) noexcept;
    esp_err_t releaseDevice() noexcept;

    i2c_master_bus_handle_t bus_handle_{nullptr};
    i2c_master_dev_handle_t device_handle_{nullptr};
    InitializationState initialization_state_{InitializationState::not_started};
    TickType_t state_started_at_{0};
    TickType_t minimum_wait_ticks_{0};
    std::uint8_t initialization_pass_{0};
    bool backlight_on_{false};
    LcdDashboard::Lines written_lines_{};
    std::size_t verification_position_{0};
    bool written_lines_valid_{false};
};
