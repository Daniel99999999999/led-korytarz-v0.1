#pragma once

#include "LocalControlConfig.hpp"
#include "PirAutomation.hpp"

class BacklightPolicy final {
  public:
    [[nodiscard]] bool update(const local_control::BacklightMode mode, const bool night,
                              const bool pir2_active, const std::uint32_t now,
                              const std::uint32_t pir_delay) noexcept {
        const auto action = motion_.update(pir2_active, motion_on_, now, pir_delay);
        if (action != PirAutomation::Action::none) {
            motion_on_ = action == PirAutomation::Action::turn_on;
        }
        if (manual_active_ &&
            static_cast<std::uint32_t>(now - manual_started_) >= manual_duration_) {
            manual_active_ = false;
        }
        const bool automatic =
            mode == local_control::BacklightMode::always_on ||
            (mode == local_control::BacklightMode::automatic && (!night || motion_on_));
        on_ = manual_active_ ? manual_on_ : automatic;
        return on_;
    }

    void toggle(const std::uint32_t now, const std::uint32_t duration) noexcept {
        manual_active_ = true;
        manual_started_ = now;
        manual_duration_ = duration;
        manual_on_ = !on_;
        on_ = manual_on_;
    }

    void cancelOverride() noexcept {
        manual_active_ = false;
    }
    [[nodiscard]] bool isOn() const noexcept {
        return on_;
    }

  private:
    PirAutomation motion_{};
    std::uint32_t manual_started_{0};
    std::uint32_t manual_duration_{0};
    bool motion_on_{false};
    bool manual_active_{false};
    bool manual_on_{false};
    bool on_{true};
};
