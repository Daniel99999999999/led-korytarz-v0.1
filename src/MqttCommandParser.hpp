#pragma once

#include "ControlCommand.hpp"

#include <cstddef>
#include <cstdint>

namespace mqtt_protocol {

inline constexpr std::size_t kMaxCommandPayloadLength = 256;
inline constexpr std::uint32_t kMaxTransitionMs = 3'600'000;

enum class ParseResult : std::uint8_t {
    ok,
    empty_payload,
    payload_too_long,
    invalid_json,
    root_not_object,
    unsupported_field,
    duplicate_field,
    invalid_state,
    invalid_brightness,
    invalid_transition,
    no_action,
};

[[nodiscard]] ParseResult parseCommand(const char* payload, std::size_t length,
                                       ControlCommand& output) noexcept;
[[nodiscard]] std::uint8_t brightnessToPercent(std::uint8_t brightness) noexcept;
[[nodiscard]] std::uint8_t percentToBrightness(std::uint8_t percent) noexcept;
[[nodiscard]] const char* parseResultName(ParseResult result) noexcept;

} // namespace mqtt_protocol
