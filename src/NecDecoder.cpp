#include "NecDecoder.hpp"

#include <array>

namespace {
constexpr std::uint32_t kLeaderMarkMinUs = 8'000;
constexpr std::uint32_t kLeaderMarkMaxUs = 10'000;
constexpr std::uint32_t kCommandSpaceMinUs = 3'800;
constexpr std::uint32_t kCommandSpaceMaxUs = 5'200;
constexpr std::uint32_t kRepeatSpaceMinUs = 1'900;
constexpr std::uint32_t kRepeatSpaceMaxUs = 2'800;
constexpr std::uint32_t kBitMarkMinUs = 350;
constexpr std::uint32_t kBitMarkMaxUs = 800;
constexpr std::uint32_t kZeroSpaceMinUs = 350;
constexpr std::uint32_t kZeroSpaceMaxUs = 850;
constexpr std::uint32_t kOneSpaceMinUs = 1'250;
constexpr std::uint32_t kOneSpaceMaxUs = 2'050;

[[nodiscard]] bool durationIsWithin(const std::uint32_t duration, const std::uint32_t minimum,
                                    const std::uint32_t maximum) noexcept {
    return (duration >= minimum) && (duration <= maximum);
}

[[nodiscard]] bool pulseMatches(const nec_remote::Pulse& pulse, const bool level_high,
                                const std::uint32_t minimum, const std::uint32_t maximum) noexcept {
    return (pulse.level_high == level_high) &&
           durationIsWithin(pulse.duration_us, minimum, maximum);
}
} // namespace

nec_remote::Frame nec_remote::decode(const Pulse* const pulses,
                                     const std::size_t pulse_count) noexcept {
    Frame frame{};
    if ((pulses == nullptr) || (pulse_count < 3U)) {
        return frame;
    }

    std::size_t index = 0;
    while ((index < pulse_count) &&
           ((pulses[index].duration_us == 0U) || pulses[index].level_high)) {
        ++index;
    }

    if ((index >= pulse_count) ||
        !pulseMatches(pulses[index], false, kLeaderMarkMinUs, kLeaderMarkMaxUs)) {
        return frame;
    }
    ++index;
    if (index >= pulse_count) {
        return frame;
    }

    if (pulseMatches(pulses[index], true, kRepeatSpaceMinUs, kRepeatSpaceMaxUs)) {
        ++index;
        if ((index < pulse_count) &&
            pulseMatches(pulses[index], false, kBitMarkMinUs, kBitMarkMaxUs)) {
            frame.type = FrameType::repeat;
        }
        return frame;
    }

    if (!pulseMatches(pulses[index], true, kCommandSpaceMinUs, kCommandSpaceMaxUs)) {
        return frame;
    }
    ++index;

    std::array<std::uint8_t, 4> bytes{};
    for (std::size_t bit = 0; bit < 32U; ++bit) {
        if (((index + 1U) >= pulse_count) ||
            !pulseMatches(pulses[index], false, kBitMarkMinUs, kBitMarkMaxUs)) {
            return Frame{};
        }
        ++index;

        bool value = false;
        if (pulseMatches(pulses[index], true, kZeroSpaceMinUs, kZeroSpaceMaxUs)) {
            value = false;
        } else if (pulseMatches(pulses[index], true, kOneSpaceMinUs, kOneSpaceMaxUs)) {
            value = true;
        } else {
            return Frame{};
        }
        ++index;

        if (value) {
            bytes[bit / 8U] = static_cast<std::uint8_t>(bytes[bit / 8U] | (1U << (bit % 8U)));
        }
    }

    if ((index < pulse_count) && (pulses[index].duration_us != 0U) &&
        !pulseMatches(pulses[index], false, kBitMarkMinUs, kBitMarkMaxUs)) {
        return Frame{};
    }

    frame.type = FrameType::command;
    frame.address_first = bytes[0];
    frame.address_second = bytes[1];
    frame.command = bytes[2];
    frame.command_inverse = bytes[3];
    return frame;
}
