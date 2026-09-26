#pragma once

#include <cstdint>

// Called exclusively by the controller. All times use the same unsigned tick unit.
class PirAutomation final {
  public:
    enum class Action : std::uint8_t { none, turn_on, turn_off };

    void manualOverride(const bool active) noexcept {
        owns_output_ = false;
        blocked_ = active;
    }

    [[nodiscard]] Action update(const bool active, const bool on, const std::uint32_t now,
                                const std::uint32_t delay) noexcept {
        if (!active) {
            blocked_ = false;
        }
        if (delay == 0U) {
            const bool turn_off = owns_output_ && on;
            owns_output_ = false;
            return turn_off ? Action::turn_off : Action::none;
        }
        if (blocked_) {
            return Action::none;
        }
        if (active) {
            last_active_ = now;
            if (!on) {
                owns_output_ = true;
                return Action::turn_on;
            }
        } else if (owns_output_ && (static_cast<std::uint32_t>(now - last_active_) >= delay)) {
            owns_output_ = false;
            return on ? Action::turn_off : Action::none;
        }
        return Action::none;
    }

  private:
    std::uint32_t last_active_{0};
    bool owns_output_{false};
    bool blocked_{false};
};
