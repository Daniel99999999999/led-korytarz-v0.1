#include "StatusIndicator.hpp"

StatusIndicator::StatusIndicator(const std::uint32_t toggle_interval) noexcept
    : toggle_interval_(toggle_interval) {}

void StatusIndicator::reset(const bool fault_latched, const std::uint32_t now) noexcept {
    last_toggle_ = now;
    initialized_ = true;
    fault_latched_ = fault_latched;
    illuminated_ = true;
}

bool StatusIndicator::update(const std::uint32_t now) noexcept {
    if (!initialized_ || fault_latched_ || ((now - last_toggle_) < toggle_interval_)) {
        return false;
    }

    last_toggle_ = now;
    illuminated_ = !illuminated_;
    return true;
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
