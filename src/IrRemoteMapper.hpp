#pragma once

#include "IrRemoteAction.hpp"
#include "NecDecoder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

class IrRemoteMapper final {
  public:
    enum class Role : std::uint8_t {
        unselected,
        white_controller,
        rgb_controller,
    };

    enum class Command : std::uint8_t {
        power = 0x45,
        rgb_toggle = 0x47,
        night_mode = 0x40,
        brightness_up = 0x16,
        target_previous = 0x09,
        timer_30_minutes = 0x5A,
        target_next = 0x07,
        brightness_down = 0x42,
        scene_previous = 0x43,
        scene_next = 0x44,
        select_rgb_controller = 0x15,
        cycle_transition = 0x4A,
        select_white_controller = 0x1C,
    };

    static constexpr std::uint8_t kAddressFirst = 0x87;
    static constexpr std::uint8_t kAddressSecond = 0x5A;
    static constexpr std::uint32_t kRepeatTimeoutMs = 250;
    static constexpr std::uint32_t kRepeatHoldDelayMs = 500;
    static constexpr std::array<std::uint32_t, 3> kTransitionOptionsMs{{0, 500, 2'000}};

    [[nodiscard]] IrRemoteAction process(const nec_remote::Frame& frame,
                                         std::uint32_t now_ms) noexcept;
    [[nodiscard]] Role activeRole() const noexcept;
    [[nodiscard]] std::uint8_t selectedTarget() const noexcept;
    [[nodiscard]] std::uint32_t transitionMs() const noexcept;

  private:
    [[nodiscard]] IrRemoteAction makeAction(IrRemoteAction::Type type,
                                            bool repeated = false) const noexcept;
    [[nodiscard]] IrRemoteAction processRepeat(std::uint32_t now_ms) noexcept;

    Role active_role_{Role::unselected};
    std::uint8_t selected_target_{0};
    std::size_t transition_index_{1};
    Command repeatable_command_{Command::brightness_up};
    std::uint32_t repeat_started_at_ms_{0};
    std::uint32_t last_repeat_frame_at_ms_{0};
    bool repeatable_command_valid_{false};
};
