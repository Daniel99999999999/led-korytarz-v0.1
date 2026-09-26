#include "DutyTransition.hpp"

void DutyTransition::reset(const std::uint32_t duty, const std::uint32_t now) noexcept {
    start_duty_ = duty;
    target_duty_ = duty;
    current_duty_ = duty;
    started_at_ = now;
    duration_ = 0;
    active_ = false;
}

bool DutyTransition::retarget(const std::uint32_t target_duty, const std::uint32_t duration,
                              const std::uint32_t now) noexcept {
    bool changed = update(now);

    start_duty_ = current_duty_;
    target_duty_ = target_duty;
    started_at_ = now;
    duration_ = duration;

    if ((duration_ == 0U) || (start_duty_ == target_duty_)) {
        changed = changed || (current_duty_ != target_duty_);
        current_duty_ = target_duty_;
        active_ = false;
        return changed;
    }

    active_ = true;
    return changed;
}

bool DutyTransition::update(const std::uint32_t now) noexcept {
    const std::uint32_t requested_duty = valueAt(now);
    const bool changed = requested_duty != current_duty_;
    current_duty_ = requested_duty;

    if (active_ && ((now - started_at_) >= duration_)) {
        active_ = false;
    }
    return changed;
}

std::uint32_t DutyTransition::currentDuty() const noexcept {
    return current_duty_;
}

std::uint32_t DutyTransition::targetDuty() const noexcept {
    return target_duty_;
}

bool DutyTransition::isActive() const noexcept {
    return active_;
}

std::uint32_t DutyTransition::valueAt(const std::uint32_t now) const noexcept {
    if (!active_) {
        return current_duty_;
    }

    const std::uint32_t elapsed = now - started_at_;
    if (elapsed >= duration_) {
        return target_duty_;
    }

    const bool increasing = target_duty_ > start_duty_;
    const std::uint32_t distance =
        increasing ? (target_duty_ - start_duty_) : (start_duty_ - target_duty_);
    const std::uint64_t scaled = (static_cast<std::uint64_t>(distance) * elapsed) +
                                 (static_cast<std::uint64_t>(duration_) / 2U);
    const std::uint32_t step = static_cast<std::uint32_t>(scaled / duration_);
    return increasing ? (start_duty_ + step) : (start_duty_ - step);
}
