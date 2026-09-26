#pragma once

#include <cstdint>

struct Ina228Telemetry final {
    std::int32_t current_milliamps{0};
    std::int32_t shunt_microvolts{0};
    std::int32_t die_temperature_millicelsius{0};
    std::uint32_t bus_voltage_millivolts{0};
    std::uint32_t power_milliwatts{0};
    std::uint32_t energy_milliwatt_hours{0};
    std::uint32_t revision{0};
    std::int32_t last_error{0};
    std::uint16_t device_id{0};
    std::uint16_t diagnostic_flags{0};
    bool present{false};
    bool valid{false};
};
