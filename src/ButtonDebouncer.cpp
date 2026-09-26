#include "ButtonDebouncer.hpp"

ButtonDebouncer::ButtonDebouncer(const std::uint32_t debounce_interval) noexcept
    : debounce_interval_(debounce_interval) {}

void ButtonDebouncer::reset(const bool pressed, const std::uint32_t now) noexcept {
    stable_pressed_ = pressed;
    candidate_pressed_ = pressed;
    candidate_since_ = now;
    initialized_ = true;
}

ButtonDebouncer::Event ButtonDebouncer::update(const bool pressed,
                                               const std::uint32_t now) noexcept {
    if (!initialized_) {
        reset(pressed, now);
        return Event::none;
    }

    if (pressed != candidate_pressed_) {
        candidate_pressed_ = pressed;
        candidate_since_ = now;
        return Event::none;
    }

    if (candidate_pressed_ == stable_pressed_) {
        return Event::none;
    }

    if ((now - candidate_since_) < debounce_interval_) {
        return Event::none;
    }

    stable_pressed_ = candidate_pressed_;
    return stable_pressed_ ? Event::pressed : Event::released;
}

bool ButtonDebouncer::isPressed() const noexcept {
    return stable_pressed_;
}
