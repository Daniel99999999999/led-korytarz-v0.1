#include "ButtonGesture.hpp"

ButtonGesture::ButtonGesture(const std::uint32_t long_press_interval,
                             const std::uint32_t step_interval) noexcept
    : long_press_interval_(long_press_interval), step_interval_(step_interval) {}

void ButtonGesture::reset(const bool pressed, const std::uint32_t now) noexcept {
    pressed_since_ = now;
    last_step_ = now;
    initialized_ = true;
    pressed_ = pressed;
    long_press_active_ = false;
    suppress_until_release_ = pressed;
}

ButtonGesture::Event ButtonGesture::update(const bool pressed, const std::uint32_t now) noexcept {
    if (!initialized_) {
        reset(pressed, now);
        return Event::none;
    }

    if (suppress_until_release_) {
        if (!pressed) {
            pressed_ = false;
            suppress_until_release_ = false;
        }
        return Event::none;
    }

    if (pressed != pressed_) {
        pressed_ = pressed;
        if (pressed_) {
            pressed_since_ = now;
            last_step_ = now;
            long_press_active_ = false;
            return Event::none;
        }

        if (long_press_active_) {
            long_press_active_ = false;
            return Event::long_press_ended;
        }
        return Event::short_press;
    }

    if (!pressed_) {
        return Event::none;
    }

    if (!long_press_active_) {
        if ((now - pressed_since_) < long_press_interval_) {
            return Event::none;
        }

        long_press_active_ = true;
        last_step_ = now;
        return Event::long_press_started;
    }

    if ((now - last_step_) < step_interval_) {
        return Event::none;
    }

    last_step_ = now;
    return Event::long_press_step;
}
