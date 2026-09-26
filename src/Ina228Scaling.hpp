#pragma once

#include <cstdint>

namespace ina228_scaling {

inline constexpr std::uint32_t kNominalShuntMicroOhms = 10'000;
inline constexpr std::uint32_t kCurrentLsbMicroamps = 25;

[[nodiscard]] constexpr std::uint16_t
calculateShuntCalibration(const std::uint32_t shunt_microohms,
                          const std::uint32_t current_lsb_microamps) noexcept {
    constexpr std::uint64_t kScaleNumerator = 131'072;
    constexpr std::uint64_t kScaleDenominator = 10'000'000;
    const std::uint64_t numerator = kScaleNumerator * shunt_microohms * current_lsb_microamps;
    return static_cast<std::uint16_t>((numerator + (kScaleDenominator / 2U)) / kScaleDenominator);
}

inline constexpr std::uint16_t kNominalShuntCalibration =
    calculateShuntCalibration(kNominalShuntMicroOhms, kCurrentLsbMicroamps);

[[nodiscard]] constexpr std::int32_t decodeSigned20(const std::uint32_t register_value) noexcept {
    std::uint32_t value = (register_value >> 4U) & 0x000F'FFFFU;
    if ((value & 0x0008'0000U) != 0U) {
        value |= 0xFFF0'0000U;
    }
    return static_cast<std::int32_t>(value);
}

[[nodiscard]] constexpr std::uint32_t
decodeUnsigned20(const std::uint32_t register_value) noexcept {
    return (register_value >> 4U) & 0x000F'FFFFU;
}

[[nodiscard]] constexpr std::int64_t roundedDivide(const std::int64_t numerator,
                                                   const std::int64_t denominator) noexcept {
    return (numerator >= 0) ? ((numerator + (denominator / 2)) / denominator)
                            : ((numerator - (denominator / 2)) / denominator);
}

[[nodiscard]] constexpr std::int32_t currentMilliamps(const std::int32_t counts) noexcept {
    return static_cast<std::int32_t>(
        roundedDivide(static_cast<std::int64_t>(counts) * kCurrentLsbMicroamps, 1'000));
}

[[nodiscard]] constexpr std::int32_t shuntMicrovolts(const std::int32_t counts) noexcept {
    return static_cast<std::int32_t>(roundedDivide(static_cast<std::int64_t>(counts) * 5, 16));
}

[[nodiscard]] constexpr std::uint32_t busVoltageMillivolts(const std::uint32_t counts) noexcept {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(counts) * 25U + 64U) / 128U);
}

[[nodiscard]] constexpr std::int32_t
dieTemperatureMillicelsius(const std::int16_t counts) noexcept {
    return static_cast<std::int32_t>(roundedDivide(static_cast<std::int64_t>(counts) * 125, 16));
}

[[nodiscard]] constexpr std::uint32_t powerMilliwatts(const std::uint32_t counts) noexcept {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(counts) * 2U + 12U) / 25U);
}

[[nodiscard]] constexpr std::uint32_t energyMilliwattHours(const std::uint64_t counts) noexcept {
    return static_cast<std::uint32_t>((counts * 32U + 45'000U) / 90'000U);
}

static_assert(kNominalShuntCalibration == 3'277U,
              "Bocznik 10 mOhm i CURRENT_LSB 25 uA wymagaja SHUNT_CAL=3277");
static_assert(currentMilliamps(375'000) == 9'375, "Skalowanie pradu musi odpowiadac 9,375 A");

} // namespace ina228_scaling
