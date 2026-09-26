#include "PirInput.hpp"

#include "esp_log.h"

namespace {
constexpr char kTag[] = "pir_input";
}

PirInput::PirInput(const char* const name, const gpio_num_t pin, const bool active_high,
                   const std::uint32_t debounce_ms) noexcept
    : name_(name), pin_(pin), active_high_(active_high),
      debouncer_(static_cast<std::uint32_t>(pdMS_TO_TICKS(debounce_ms))) {}

esp_err_t PirInput::initialize() noexcept {
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << static_cast<unsigned>(pin_);
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    const esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) {
        return result;
    }

    const bool active = readActive();
    debouncer_.reset(active, static_cast<std::uint32_t>(xTaskGetTickCount()));
    initialized_ = true;

    ESP_LOGI(kTag, "%s: GPIO %d, aktywny %s, pull-up zewnetrzny, start %s", name_,
             static_cast<int>(pin_), active_high_ ? "HIGH" : "LOW",
             active ? "AKTYWNE" : "NORMALNY");
    return ESP_OK;
}

PirInput::Event PirInput::poll(const TickType_t now) noexcept {
    if (!initialized_) {
        return Event::none;
    }

    switch (debouncer_.update(readActive(), static_cast<std::uint32_t>(now))) {
    case ButtonDebouncer::Event::pressed:
        return Event::activated;
    case ButtonDebouncer::Event::released:
        return Event::deactivated;
    case ButtonDebouncer::Event::none:
        return Event::none;
    }
    return Event::none;
}

bool PirInput::isActive() const noexcept {
    return initialized_ && debouncer_.isPressed();
}

const char* PirInput::name() const noexcept {
    return name_;
}

bool PirInput::readActive() const noexcept {
    const bool pin_high = gpio_get_level(pin_) != 0;
    return active_high_ ? pin_high : !pin_high;
}
