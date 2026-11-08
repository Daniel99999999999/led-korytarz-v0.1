#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace auto_ota {
inline constexpr char kReleaseBase[] =
    "https://github.com/Daniel99999999999/led-korytarz-firmware/releases";
inline constexpr std::uint32_t kIntervalSeconds = 6U * 60U * 60U;
inline constexpr std::size_t kManifestLimit = 2048;
struct Manifest {
    std::array<char, 32> version{};
    std::array<std::uint8_t, 32> sha256{};
    std::uint32_t size{0};
};
bool newer(const char* candidate, const char* current) noexcept;
bool parseManifest(const char* json, std::size_t length, Manifest& output) noexcept;
constexpr std::uint32_t retrySeconds(unsigned failures) noexcept {
    const unsigned shift = failures == 0 ? 0 : (failures > 5 ? 5 : failures - 1);
    const auto seconds = 900U << shift;
    return seconds < kIntervalSeconds ? seconds : kIntervalSeconds;
}
} // namespace auto_ota
