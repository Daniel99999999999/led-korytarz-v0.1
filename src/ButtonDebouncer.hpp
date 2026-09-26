#pragma once

#include <cstdint>

class ButtonDebouncer final {
  public:
    enum class Event : std::uint8_t {
        none,
        pressed,
        released,
    };

    explicit ButtonDebouncer(std::uint32_t debounce_interval) noexcept;

    void reset(bool pressed, std::uint32_t now) noexcept;
    [[nodiscard]] Event update(bool pressed, std::uint32_t now) noexcept;
    [[nodiscard]] bool isPressed() const noexcept;

  private:
    std::uint32_t debounce_interval_;
    bool initialized_{false};
    bool stable_pressed_{false};
    bool candidate_pressed_{false};
    std::uint32_t candidate_since_{0};
};
