#pragma once

#include <cstdint>

struct IrRemoteAction final {
    enum class Type : std::uint8_t {
        none,
        white_role_selected,
        rgb_role_selected,
        master_power,
        target_toggle,
        night_toggle,
        brightness_up,
        brightness_down,
        target_changed,
        timer_toggle,
        transition_changed,
        scene_previous,
        scene_next,
    };

    static constexpr std::uint8_t kAllChannelsTarget = 5;

    Type type{Type::none};
    std::uint8_t target{0};
    std::uint32_t transition_ms{0};
    bool repeated{false};
};
