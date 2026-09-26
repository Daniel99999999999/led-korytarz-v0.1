#include "StatusLed.hpp"

StatusLed::StatusLed(const gpio_num_t pin, const bool active_high,
                     const std::uint32_t toggle_interval) noexcept
    : pin_(pin), active_high_(active_high), indicator_(toggle_interval) {}

esp_err_t StatusLed::initialize(const bool fault_latched, const std::uint32_t now) noexcept {
    esp_err_t result = gpio_set_level(pin_, physicalLevel(false));
    if (result != ESP_OK) {
        return result;
    }

    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << static_cast<unsigned>(pin_);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    result = gpio_config(&config);
    if (result != ESP_OK) {
        return result;
    }

    initialized_ = true;
    applied_illuminated_ = false;
    indicator_.reset(fault_latched, now);
    return synchronize();
}

esp_err_t StatusLed::update(const std::uint32_t now) noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    static_cast<void>(indicator_.update(now));
    return synchronize();
}

esp_err_t StatusLed::latchFault() noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    indicator_.latchFault();
    return synchronize();
}

esp_err_t StatusLed::clearFault(const std::uint32_t now) noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    indicator_.clearFault(now);
    return synchronize();
}

bool StatusLed::isFaultLatched() const noexcept {
    return indicator_.isFaultLatched();
}

int StatusLed::physicalLevel(const bool illuminated) const noexcept {
    return (illuminated == active_high_) ? 1 : 0;
}

esp_err_t StatusLed::synchronize() noexcept {
    const bool requested = indicator_.isIlluminated();
    if (requested == applied_illuminated_) {
        return ESP_OK;
    }

    const esp_err_t result = gpio_set_level(pin_, physicalLevel(requested));
    if (result == ESP_OK) {
        applied_illuminated_ = requested;
    }
    return result;
}
