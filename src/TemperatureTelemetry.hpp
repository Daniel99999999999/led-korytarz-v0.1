#pragma once

#include "BoardConfig.hpp"
#include "TemperatureModel.hpp"

#include <array>
#include <cstdint>

struct TemperatureSensorTelemetry final {
    std::uint64_t rom_id{0};
    std::int32_t temperature_millicelsius{0};
    std::int32_t last_error{0};
    bool present{false};
    bool valid{false};
};

struct TemperatureRoleTelemetry final {
    std::uint64_t rom_id{0};
    std::int32_t temperature_millicelsius{0};
    std::int32_t last_error{0};
    bool configured{false};
    bool present{false};
    bool valid{false};
};

struct TemperatureTelemetry final {
    std::array<TemperatureSensorTelemetry, board_config::kDs18b20MaximumDeviceCount> sensors{};
    std::array<TemperatureRoleTelemetry, kTemperatureRoleCount> roles{};
    std::uint32_t revision{0};
    std::int32_t last_error{0};
    std::uint8_t sensor_count{0};
    bool bus_ready{false};
    bool scan_pending{false};
};
