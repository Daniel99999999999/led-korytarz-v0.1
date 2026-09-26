#include "TemperatureModel.hpp"

#include <limits>

namespace temperature_model {

std::uint8_t dallasCrc8(const std::uint8_t* const data, const std::size_t size) noexcept {
    if ((data == nullptr) && (size != 0U)) {
        return 0U;
    }

    std::uint8_t crc = 0U;
    for (std::size_t index = 0; index < size; ++index) {
        std::uint8_t value = data[index];
        for (std::uint8_t bit = 0; bit < 8U; ++bit) {
            const bool mix = ((crc ^ value) & 0x01U) != 0U;
            crc >>= 1U;
            if (mix) {
                crc ^= 0x8CU;
            }
            value >>= 1U;
        }
    }
    return crc;
}

void romToBytes(const std::uint64_t rom_id, std::array<std::uint8_t, kRomSize>& bytes) noexcept {
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>((rom_id >> (index * 8U)) & 0xFFU);
    }
}

bool romIsValid(const std::uint64_t rom_id) noexcept {
    if (rom_id == 0U) {
        return false;
    }
    std::array<std::uint8_t, kRomSize> bytes{};
    romToBytes(rom_id, bytes);
    return (bytes[0] == kDs18b20FamilyCode) &&
           (dallasCrc8(bytes.data(), bytes.size() - 1U) == bytes.back());
}

bool assignmentsAreValid(const TemperatureAssignments& assignments) noexcept {
    for (const std::uint64_t rom_id : assignments.rom_ids) {
        if ((rom_id != 0U) && !romIsValid(rom_id)) {
            return false;
        }
    }
    return (assignments.rom_ids[0] == 0U) || (assignments.rom_ids[1] == 0U) ||
           (assignments.rom_ids[0] != assignments.rom_ids[1]);
}

bool parseScratchpad(const std::uint8_t* const scratchpad, const std::size_t size,
                     std::int32_t& temperature_millicelsius) noexcept {
    temperature_millicelsius = 0;
    if ((scratchpad == nullptr) || (size != kScratchpadSize) ||
        (dallasCrc8(scratchpad, size - 1U) != scratchpad[size - 1U])) {
        return false;
    }

    std::int16_t raw = static_cast<std::int16_t>(static_cast<std::uint16_t>(scratchpad[0]) |
                                                 (static_cast<std::uint16_t>(scratchpad[1]) << 8U));
    switch ((scratchpad[4] >> 5U) & 0x03U) {
    case 0U:
        raw = static_cast<std::int16_t>(raw & static_cast<std::int16_t>(~0x0007));
        break;
    case 1U:
        raw = static_cast<std::int16_t>(raw & static_cast<std::int16_t>(~0x0003));
        break;
    case 2U:
        raw = static_cast<std::int16_t>(raw & static_cast<std::int16_t>(~0x0001));
        break;
    default:
        break;
    }

    constexpr std::int32_t kMinimumRaw = -55 * 16;
    constexpr std::int32_t kMaximumRaw = 125 * 16;
    const std::int32_t signed_raw = raw;
    if ((signed_raw < kMinimumRaw) || (signed_raw > kMaximumRaw)) {
        return false;
    }

    const std::int32_t scaled = signed_raw * 1'000;
    temperature_millicelsius = (scaled >= 0) ? ((scaled + 8) / 16) : ((scaled - 8) / 16);
    return true;
}

} // namespace temperature_model
