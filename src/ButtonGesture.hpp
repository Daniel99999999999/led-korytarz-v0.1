#pragma once

#include <cstdint>

class ButtonGesture final {
  public:
    enum class Event : std::uint8_t {
        none,
        short_press,
        long_press_started,
        long_press_step,
        long_press_ended,
    };

    ButtonGesture(std::uint32_t long_press_interval, std::uint32_t step_interval) noexcept;

    void reset(bool pressed, std::uint32_t now) noexcept;
    [[nodiscard]] Event update(bool pressed, std::uint32_t now) noexcept;

  private:
    std::uint32_t long_press_interval_;
    std::uint32_t step_interval_;
    std::uint32_t pressed_since_{0};
    std::uint32_t last_step_{0};
    bool initialized_{false};
    bool pressed_{false};
    bool long_press_active_{false};
    bool suppress_until_release_{false};
};
