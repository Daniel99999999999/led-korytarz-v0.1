#include "ChannelState.hpp"

#include <algorithm>

ChannelState::ChannelState(const std::uint8_t default_brightness_percent) noexcept {
    reset(default_brightness_percent);
}

void ChannelState::reset(const std::uint8_t default_brightness_percent) noexcept {
    on_ = false;
    brightness_percent_ = normalizeBrightness(default_brightness_percent);
    dimming_active_ = false;
    active_dimming_direction_ = DimmingDirection::dimmer;
    next_dimming_direction_ = DimmingDirection::dimmer;
}

void ChannelState::turnOn() noexcept {
    on_ = true;
    dimming_active_ = false;
}

void ChannelState::turnOff() noexcept {
    on_ = false;
    dimming_active_ = false;
}

void ChannelState::toggle() noexcept {
    on_ = !on_;
    dimming_active_ = false;
}

void ChannelState::setBrightness(const std::uint8_t percent) noexcept {
    brightness_percent_ = normalizeBrightness(percent);
}

bool ChannelState::beginDimming() noexcept {
    dimming_active_ = true;

    if (!on_) {
        on_ = true;
        brightness_percent_ = kMinimumBrightnessPercent;
        active_dimming_direction_ = DimmingDirection::brighter;
        next_dimming_direction_ = DimmingDirection::dimmer;
        return true;
    }

    active_dimming_direction_ = next_dimming_direction_;
    if (((brightness_percent_ == kMinimumBrightnessPercent) &&
         (active_dimming_direction_ == DimmingDirection::dimmer)) ||
        ((brightness_percent_ == kMaximumBrightnessPercent) &&
         (active_dimming_direction_ == DimmingDirection::brighter))) {
        active_dimming_direction_ = (active_dimming_direction_ == DimmingDirection::brighter)
                                        ? DimmingDirection::dimmer
                                        : DimmingDirection::brighter;
    }

    next_dimming_direction_ = (active_dimming_direction_ == DimmingDirection::brighter)
                                  ? DimmingDirection::dimmer
                                  : DimmingDirection::brighter;
    return stepDimming();
}

bool ChannelState::stepDimming() noexcept {
    if (!dimming_active_) {
        return false;
    }

    if (active_dimming_direction_ == DimmingDirection::brighter) {
        if (brightness_percent_ >= kMaximumBrightnessPercent) {
            return false;
        }
        ++brightness_percent_;
        return true;
    }

    if (brightness_percent_ <= kMinimumBrightnessPercent) {
        return false;
    }
    --brightness_percent_;
    return true;
}

void ChannelState::endDimming() noexcept {
    dimming_active_ = false;
}

bool ChannelState::isOn() const noexcept {
    return on_;
}

std::uint8_t ChannelState::brightness() const noexcept {
    return brightness_percent_;
}

bool ChannelState::isDimming() const noexcept {
    return dimming_active_;
}

ChannelState::DimmingDirection ChannelState::dimmingDirection() const noexcept {
    return active_dimming_direction_;
}

std::uint32_t ChannelState::targetDuty(const std::uint32_t maximum_duty) const noexcept {
    if (!on_) {
        return 0;
    }

    const std::uint64_t scaled = static_cast<std::uint64_t>(maximum_duty) * brightness_percent_;
    return static_cast<std::uint32_t>((scaled + 50U) / 100U);
}

std::uint8_t ChannelState::normalizeBrightness(const std::uint8_t percent) noexcept {
    return std::clamp<std::uint8_t>(percent, kMinimumBrightnessPercent, kMaximumBrightnessPercent);
}
