#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace pwm_config {
// Fixed APB clock, 12-bit period. Frequency limit preserves the original default.
inline constexpr std::uint32_t kPeriod = 4096;
inline constexpr std::uint16_t kMinHz = 100, kMaxHz = 19531;
inline constexpr std::array<std::uint16_t, 16> kPulseUs{
    {0, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 3000, 4000, 5000, 10000}};
inline constexpr std::array<const char*, 5> kFields{
    {"frequency", "minimum", "on_us", "off_us", "mode"}};
inline constexpr std::array<const char*, 5> kNames{{"czestotliwosc PWM", "minimalne wypelnienie",
                                                    "minimalny impuls ON (us)",
                                                    "minimalny impuls OFF (us)", "tryb wyjscia"}};
inline bool parse(unsigned field, const char* text, std::size_t length,
                  std::uint16_t& out) noexcept {
    if (!text || !length || field >= 5)
        return false;
    if (field == 4) {
        if (length == 3 && std::memcmp(text, "PWM", 3) == 0) {
            out = 0;
            return true;
        }
        if (length == 6 && std::memcmp(text, "ON/OFF", 6) == 0) {
            out = 1;
            return true;
        }
        return false;
    }
    if (length > 5)
        return false;
    unsigned n = 0;
    for (std::size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9')
            return false;
        n = n * 10U + static_cast<unsigned>(text[i] - '0');
    }
    if (field >= 2) {
        for (unsigned i = 0; i < kPulseUs.size(); ++i)
            if (n == kPulseUs[i]) {
                out = static_cast<std::uint16_t>(i);
                return true;
            }
        return false;
    }
    if ((field == 0 && (n < kMinHz || n > kMaxHz)) || (field == 1 && n > 100))
        return false;
    out = static_cast<std::uint16_t>(n);
    return true;
}
struct Config {
    std::uint16_t frequency_hz{19531};
    std::uint8_t minimum_percent{0};
    std::uint8_t on_index{0}, off_index{0};
    bool on_off{false};
};
inline std::uint16_t value(const Config& c, unsigned field) noexcept {
    switch (field) {
    case 0:
        return c.frequency_hz;
    case 1:
        return c.minimum_percent;
    case 2:
        return kPulseUs[c.on_index];
    case 3:
        return kPulseUs[c.off_index];
    default:
        return c.on_off ? 1 : 0;
    }
}
inline bool equal(const Config& a, const Config& b) noexcept {
    return a.frequency_hz == b.frequency_hz && a.minimum_percent == b.minimum_percent &&
           a.on_index == b.on_index && a.off_index == b.off_index && a.on_off == b.on_off;
}
inline std::uint32_t pulseTicks(std::uint16_t us, std::uint32_t hz) noexcept {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(us) * hz * kPeriod + 999999U) /
                                      1000000U);
}
inline bool valid(const Config& c) noexcept {
    if (c.frequency_hz < kMinHz || c.frequency_hz > kMaxHz || c.minimum_percent > 100 ||
        c.on_index >= 16 || c.off_index >= 16)
        return false;
    const auto low = std::max((static_cast<std::uint32_t>(c.minimum_percent) * 4095U + 99U) / 100U,
                              pulseTicks(kPulseUs[c.on_index], c.frequency_hz));
    const auto off = pulseTicks(kPulseUs[c.off_index], c.frequency_hz);
    return off < kPeriod && low <= std::min<std::uint32_t>(4095U, kPeriod - off);
}
inline std::uint32_t constrain(std::uint32_t requested, const Config& c) noexcept {
    if (requested == 0)
        return 0;
    if (c.on_off)
        return kPeriod; // real constant HIGH; OFF always remains LOW
    const auto low = std::max((static_cast<std::uint32_t>(c.minimum_percent) * 4095U + 99U) / 100U,
                              pulseTicks(kPulseUs[c.on_index], c.frequency_hz));
    const auto high =
        std::min<std::uint32_t>(4095U, kPeriod - pulseTicks(kPulseUs[c.off_index], c.frequency_hz));
    return std::clamp(requested, low, high);
}
} // namespace pwm_config
