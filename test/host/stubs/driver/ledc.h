#pragma once
#include "esp_err.h"

#include <array>
#include <cstdint>
namespace fake_ledc {
inline std::array<std::uint32_t, 4> frequencies{};
inline std::array<std::uint32_t, 8> duties{};
inline std::array<int, 8> timers{};
inline bool fail_frequency = false;
inline bool fail_stop = false;
} // namespace fake_ledc

using ledc_mode_t = int;
using ledc_timer_t = int;
using ledc_timer_bit_t = int;
using ledc_channel_t = int;
constexpr int LEDC_LOW_SPEED_MODE = 0, LEDC_TIMER_0 = 0, LEDC_TIMER_1 = 1, LEDC_TIMER_2 = 2,
              LEDC_USE_APB_CLK = 1, LEDC_TIMER_12_BIT = 12, LEDC_AUTO_CLK = 0,
              LEDC_SLEEP_MODE_NO_ALIVE_NO_PD = 0;
constexpr int LEDC_CHANNEL_0 = 0, LEDC_CHANNEL_1 = 1, LEDC_CHANNEL_2 = 2, LEDC_CHANNEL_3 = 3,
              LEDC_CHANNEL_4 = 4;
struct ledc_timer_config_t {
    int speed_mode, duty_resolution, timer_num;
    std::uint32_t freq_hz;
    int clk_cfg;
    bool deconfigure;
};
struct ledc_channel_config_t {
    int gpio_num, speed_mode, channel, timer_sel;
    std::uint32_t duty;
    int hpoint, sleep_mode;
    struct {
        int output_invert;
    } flags;
};
inline int ledc_timer_config(const ledc_timer_config_t* c) {
    fake_ledc::frequencies[c->timer_num] = c->freq_hz;
    return ESP_OK;
}
inline int ledc_channel_config(const ledc_channel_config_t* c) {
    fake_ledc::timers[c->channel] = c->timer_sel;
    fake_ledc::duties[c->channel] = c->duty;
    return ESP_OK;
}
inline int ledc_set_duty(int, int channel, std::uint32_t duty) {
    fake_ledc::duties[channel] = duty;
    return ESP_OK;
}
inline int ledc_update_duty(int, int) {
    return ESP_OK;
}
inline int ledc_stop(int, int channel, int) {
    if (fake_ledc::fail_stop)
        return ESP_FAIL;
    fake_ledc::duties[channel] = 0;
    return ESP_OK;
}

inline int ledc_set_freq(int, int timer, std::uint32_t hz) {
    if (fake_ledc::fail_frequency)
        return ESP_FAIL;
    fake_ledc::frequencies[timer] = hz;
    return ESP_OK;
}
