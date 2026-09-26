#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

enum class TemperatureRole : std::uint8_t {
    radiator,
    enclosure,
    count,
};

inline constexpr std::size_t kTemperatureRoleCount =
    static_cast<std::size_t>(TemperatureRole::count);

struct TemperatureAssignments final {
    std::array<std::uint64_t, kTemperatureRoleCount> rom_ids{};
};

namespace temperature_model {

inline constexpr std::uint8_t kDs18b20FamilyCode = 0x28;
inline constexpr std::size_t kRomSize = 8;
inline constexpr std::size_t kScratchpadSize = 9;

[[nodiscard]] std::uint8_t dallasCrc8(const std::uint8_t* data, std::size_t size) noexcept;
[[nodiscard]] bool romIsValid(std::uint64_t rom_id) noexcept;
[[nodiscard]] bool assignmentsAreValid(const TemperatureAssignments& assignments) noexcept;
[[nodiscard]] bool parseScratchpad(const std::uint8_t* scratchpad, std::size_t size,
                                   std::int32_t& temperature_millicelsius) noexcept;
void romToBytes(std::uint64_t rom_id, std::array<std::uint8_t, kRomSize>& bytes) noexcept;

} // namespace temperature_model
