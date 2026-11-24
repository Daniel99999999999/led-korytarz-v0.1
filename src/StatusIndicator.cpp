#include "StatusIndicator.hpp"

StatusIndicator::StatusIndicator(const std::uint32_t toggle_interval) noexcept
    : toggle_interval_(toggle_interval) {}

void StatusIndicator::reset(const bool fault_latched, const std::uint32_t now) noexcept {
    last_toggle_ = now;
    initialized_ = true;
    updating_ = false;
    fault_latched_ = fault_latched;
    illuminated_ = true;
}

bool StatusIndicator::update(const std::uint32_t now) noexcept {
    const auto interval =
        updating_ ? (toggle_interval_ > 1 ? toggle_interval_ / 2 : 1) : toggle_interval_;
    if (!initialized_ || (fault_latched_ && !updating_) || ((now - last_toggle_) < interval)) {
        return false;
    }

    last_toggle_ = now;
    illuminated_ = !illuminated_;
    return true;
}

void StatusIndicator::setUpdating(const bool updating, const std::uint32_t now) noexcept {
    if (updating_ == updating)
        return;
    updating_ = updating;
    illuminated_ = true;
    last_toggle_ = now;
}

void StatusIndicator::latchFault() noexcept {
    initialized_ = true;
    fault_latched_ = true;
    illuminated_ = true;
}

void StatusIndicator::clearFault(const std::uint32_t now) noexcept {
    initialized_ = true;
    fault_latched_ = false;
    illuminated_ = true;
    last_toggle_ = now;
}

bool StatusIndicator::isIlluminated() const noexcept {
    return illuminated_;
}

bool StatusIndicator::isFaultLatched() const noexcept {
    return fault_latched_;
}
