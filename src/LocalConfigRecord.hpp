#pragma once

#include "LocalControlConfig.hpp"

namespace local_control {
// DS1307 RAM 0x08..0x3f: two 28-byte slots. RTC registers are never touched.
using Record = std::array<std::uint8_t, 28>;
inline constexpr std::uint8_t kCommit = 0xA5;
[[nodiscard]] Record encodeRecord(const Config& config, std::uint32_t sequence) noexcept;
[[nodiscard]] bool decodeRecord(const Record& record, Config& config,
                                std::uint32_t& sequence) noexcept;
[[nodiscard]] int newestRecord(const std::array<Record, 2>& records, Config& config,
                               std::uint32_t& sequence) noexcept;
} // namespace local_control
