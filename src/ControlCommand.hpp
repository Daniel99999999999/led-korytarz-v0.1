#pragma once

#include "IrRemoteAction.hpp"
#include "LocalControlConfig.hpp"

#include <cstddef>
#include <cstdint>

struct ControlCommand final {
    enum class Kind : std::uint8_t {
        channel,
        infrared,
        button_mapping,
        pir_delay,
        restore_local_config,
        lcd_night,
        lcd_override,
        lcd_mode,
    };

    enum class StateAction : std::uint8_t {
        unchanged,
        turn_on,
        turn_off,
    };

    Kind kind{Kind::channel};
    std::size_t channel_index{0};
    StateAction state{StateAction::unchanged};
    std::uint8_t brightness{0};
    std::uint32_t transition_ms{0};
    bool brightness_present{false};
    bool transition_present{false};
    IrRemoteAction infrared{};
    local_control::Config local_config{};
    std::uint16_t setting_value{0};
};
