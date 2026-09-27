#pragma once

#include "PwmConfig.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace local_control {
inline constexpr std::size_t kChannelCount = 5;
inline constexpr std::size_t kPirCount = 2;
inline constexpr std::uint16_t kMaxOffDelaySeconds = 3600;
inline constexpr std::array<std::uint8_t, 8> kButtonMasks{{0, 1, 2, 4, 8, 16, 3, 31}};
inline constexpr std::array<const char*, 8> kButtonOptions{{
    "Wylaczony",
    "Kanal 1",
    "Kanal 2",
    "Kanal 3",
    "Kanal 4",
    "Kanal 5",
    "Kanaly 1+2",
    "Wszystkie",
}};

enum class BacklightMode : std::uint8_t { automatic, always_off, always_on };
inline constexpr std::array<const char*, 3> kBacklightOptions{
    {"Automatyczne", "Stale OFF", "Stale ON"}};

struct Config final {
    std::array<pwm_config::Config, 2> pwm{};
    std::array<std::uint8_t, kChannelCount> button_masks{{1, 2, 4, 8, 16}};
    std::array<std::uint16_t, kPirCount> off_delay_seconds{};
    std::uint16_t lcd_override_seconds{120};
    BacklightMode backlight_mode{BacklightMode::automatic};
};

[[nodiscard]] bool validMask(std::uint8_t mask) noexcept;
[[nodiscard]] bool valid(const Config& config) noexcept;
[[nodiscard]] bool equal(const Config& left, const Config& right) noexcept;
[[nodiscard]] const char* optionForMask(std::uint8_t mask) noexcept;
[[nodiscard]] bool parseMask(const char* data, std::size_t size, std::uint8_t& mask) noexcept;
[[nodiscard]] bool parseBacklightMode(const char* data, std::size_t size,
                                      std::uint16_t& mode) noexcept;
[[nodiscard]] bool parseDelay(const char* data, std::size_t size, std::uint16_t& seconds) noexcept;
[[nodiscard]] constexpr bool includes(const std::uint8_t mask, const std::size_t channel) noexcept {
    return channel < kChannelCount && (mask & (1U << channel)) != 0U;
}
[[nodiscard]] bool groupShouldTurnOn(std::uint8_t mask,
                                     const std::array<bool, kChannelCount>& on) noexcept;
[[nodiscard]] std::uint8_t scaledBrightness(std::uint8_t original, std::uint8_t original_leader,
                                            std::uint8_t leader) noexcept;
} // namespace local_control
