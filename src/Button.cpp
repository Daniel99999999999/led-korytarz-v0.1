#include "Button.hpp"

#include "esp_log.h"

namespace {
constexpr char kTag[] = "button";
}

Button::Button(const char* const name, const gpio_num_t pin, const bool active_low,
               const std::uint32_t debounce_ms, const std::uint32_t long_press_ms,
               const std::uint32_t step_interval_ms, const bool enable_internal_bias) noexcept
    : name_(name), pin_(pin), active_low_(active_low), enable_internal_bias_(enable_internal_bias),
      debouncer_(static_cast<std::uint32_t>(pdMS_TO_TICKS(debounce_ms))),
      gesture_(static_cast<std::uint32_t>(pdMS_TO_TICKS(long_press_ms)),
               static_cast<std::uint32_t>(pdMS_TO_TICKS(step_interval_ms))) {}

esp_err_t Button::initialize() noexcept {
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << static_cast<unsigned>(pin_);
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en =
        (enable_internal_bias_ && active_low_) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    config.pull_down_en =
        (enable_internal_bias_ && !active_low_) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    const esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) {
        return result;
    }

    // Stan zastany przy starcie staje sie stanem bazowym. Trzymanie
    // przycisku podczas resetu nie wlaczy przypadkowo oswietlenia.
    const bool pressed = readPressed();
    const std::uint32_t now = static_cast<std::uint32_t>(xTaskGetTickCount());
    debouncer_.reset(pressed, now);
    gesture_.reset(pressed, now);
    initialized_ = true;

    ESP_LOGI(kTag, "%s: GPIO %d, aktywny stan %s, bias %s", name_, static_cast<int>(pin_),
             active_low_ ? "niski" : "wysoki", enable_internal_bias_ ? "wewnetrzny" : "zewnetrzny");
    return ESP_OK;
}

Button::Event Button::poll(const TickType_t now) noexcept {
    if (!initialized_) {
        return Event::none;
    }

    static_cast<void>(debouncer_.update(readPressed(), static_cast<std::uint32_t>(now)));
    return gesture_.update(debouncer_.isPressed(), static_cast<std::uint32_t>(now));
}

const char* Button::name() const noexcept {
    return name_;
}

bool Button::readPressed() const noexcept {
    const bool pin_high = gpio_get_level(pin_) != 0;
    return active_low_ ? !pin_high : pin_high;
}
