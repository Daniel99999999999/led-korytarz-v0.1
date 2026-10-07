#pragma once
#include "esp_err.h"

#include <array>
#include <cstdint>
using gpio_num_t = int;
constexpr int GPIO_NUM_0 = 0;
constexpr int GPIO_NUM_1 = 1;
constexpr int GPIO_NUM_2 = 2;
constexpr int GPIO_NUM_3 = 3;
constexpr int GPIO_NUM_4 = 4;
constexpr int GPIO_NUM_5 = 5;
constexpr int GPIO_NUM_6 = 6;
constexpr int GPIO_NUM_7 = 7;
constexpr int GPIO_NUM_8 = 8;
constexpr int GPIO_NUM_9 = 9;
constexpr int GPIO_NUM_10 = 10;
constexpr int GPIO_NUM_11 = 11;
constexpr int GPIO_NUM_12 = 12;
constexpr int GPIO_NUM_13 = 13;
constexpr int GPIO_NUM_14 = 14;
constexpr int GPIO_NUM_15 = 15;
constexpr int GPIO_NUM_16 = 16;
constexpr int GPIO_NUM_17 = 17;
constexpr int GPIO_NUM_18 = 18;
constexpr int GPIO_NUM_19 = 19;
constexpr int GPIO_NUM_20 = 20;
constexpr int GPIO_NUM_21 = 21;
constexpr int GPIO_NUM_22 = 22;
constexpr int GPIO_NUM_23 = 23;
constexpr int GPIO_NUM_24 = 24;
constexpr int GPIO_NUM_25 = 25;
constexpr int GPIO_NUM_26 = 26;
constexpr int GPIO_NUM_27 = 27;
constexpr int GPIO_NUM_28 = 28;
constexpr int GPIO_NUM_29 = 29;
constexpr int GPIO_NUM_30 = 30;
constexpr int GPIO_NUM_31 = 31;
constexpr int GPIO_NUM_32 = 32;
constexpr int GPIO_NUM_33 = 33;
constexpr int GPIO_NUM_34 = 34;
constexpr int GPIO_NUM_35 = 35;
constexpr int GPIO_NUM_36 = 36;
constexpr int GPIO_NUM_37 = 37;
constexpr int GPIO_NUM_38 = 38;
constexpr int GPIO_NUM_39 = 39;
constexpr int GPIO_MODE_OUTPUT = 1, GPIO_PULLUP_DISABLE = 0, GPIO_PULLDOWN_DISABLE = 0,
              GPIO_INTR_DISABLE = 0;
struct gpio_config_t {
    std::uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
};
namespace fake_gpio {
inline std::array<int, 40> levels{};
inline std::array<bool, 40> gpio_output{};
inline bool fail_set_level_once = false;
} // namespace fake_gpio
inline int gpio_config(const gpio_config_t* config) {
    for (unsigned pin = 0; pin < fake_gpio::gpio_output.size(); ++pin) {
        if ((config->pin_bit_mask & (1ULL << pin)) != 0) {
            fake_gpio::gpio_output[pin] = config->mode == GPIO_MODE_OUTPUT;
        }
    }
    return ESP_OK;
}
inline int gpio_set_level(int pin, int level) {
    if (fake_gpio::fail_set_level_once) {
        fake_gpio::fail_set_level_once = false;
        return ESP_FAIL;
    }
    fake_gpio::levels[static_cast<std::size_t>(pin)] = level;
    return ESP_OK;
}
