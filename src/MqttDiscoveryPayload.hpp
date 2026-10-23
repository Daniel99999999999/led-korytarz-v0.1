#pragma once

#include "FeatureConfig.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mqtt_discovery {

inline constexpr std::size_t kPayloadCapacity = 24'576;

struct Topics {
    const char* base{nullptr};
    const char* availability{nullptr};
    std::array<const char*, 5> command{};
    std::array<const char*, 5> state{};
    std::array<const char*, 2> pir_state{};
    const char* ota_status{nullptr};
#if LED_CONTROLLER_INA228_ENABLED
    const char* ina228_availability{nullptr};
    std::array<const char*, 5> ina228_state{};
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    std::array<const char*, 2> temperature_availability{};
    std::array<const char*, 2> temperature_state{};
#endif
};

enum class Failure : std::uint8_t { none, root, local, debug, component, buffer };
enum class Stage : std::uint8_t { before_tree, after_tree, after_serialization, after_free };
using StageObserver = void (*)(Stage, void*);

struct Result {
    Failure failure{Failure::root};
    std::size_t bytes{0};
    [[nodiscard]] bool ok() const noexcept {
        return failure == Failure::none;
    }
};

[[nodiscard]] Result build(const Topics& topics, const char* device_id, const char* version,
                           char* output, std::size_t capacity, StageObserver observer = nullptr,
                           void* observer_context = nullptr) noexcept;

} // namespace mqtt_discovery
