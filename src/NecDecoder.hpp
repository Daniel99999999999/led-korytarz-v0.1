#pragma once

#include <cstddef>
#include <cstdint>

namespace nec_remote {

struct Pulse final {
    std::uint32_t duration_us{0};
    bool level_high{true};
};

enum class FrameType : std::uint8_t {
    invalid,
    command,
    repeat,
};

struct Frame final {
    FrameType type{FrameType::invalid};
    std::uint8_t address_first{0};
    std::uint8_t address_second{0};
    std::uint8_t command{0};
    std::uint8_t command_inverse{0};
};

[[nodiscard]] Frame decode(const Pulse* pulses, std::size_t pulse_count) noexcept;

} // namespace nec_remote
