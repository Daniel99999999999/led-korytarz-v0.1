#include "LocalControlConfig.hpp"

#include <algorithm>
#include <cstring>

namespace local_control {
bool validMask(const std::uint8_t mask) noexcept {
    return std::find(kButtonMasks.begin(), kButtonMasks.end(), mask) != kButtonMasks.end();
}

bool valid(const Config& config) noexcept {
    for (const auto& pwm : config.pwm) {
        if (!pwm_config::valid(pwm))
            return false;
    }
    if (config.lcd_override_seconds == 0U || config.lcd_override_seconds > kMaxOffDelaySeconds ||
        static_cast<unsigned>(config.backlight_mode) >= kBacklightOptions.size()) {
        return false;
    }
    for (const auto mask : config.button_masks) {
        if (!validMask(mask)) {
            return false;
        }
    }
    for (const auto seconds : config.off_delay_seconds) {
        if (seconds > kMaxOffDelaySeconds) {
            return false;
        }
    }
    return true;
}

bool equal(const Config& left, const Config& right) noexcept {
    return pwm_config::equal(left.pwm[0], right.pwm[0]) &&
           pwm_config::equal(left.pwm[1], right.pwm[1]) &&
           left.button_masks == right.button_masks &&
           left.off_delay_seconds == right.off_delay_seconds &&
           left.lcd_override_seconds == right.lcd_override_seconds &&
           left.backlight_mode == right.backlight_mode;
}

const char* optionForMask(const std::uint8_t mask) noexcept {
    for (std::size_t index = 0; index < kButtonMasks.size(); ++index) {
        if (kButtonMasks[index] == mask) {
            return kButtonOptions[index];
        }
    }
    return kButtonOptions[0];
}

bool parseMask(const char* const data, const std::size_t size, std::uint8_t& mask) noexcept {
    if (data == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < kButtonOptions.size(); ++index) {
        if (std::strlen(kButtonOptions[index]) == size &&
            std::memcmp(data, kButtonOptions[index], size) == 0) {
            mask = kButtonMasks[index];
            return true;
        }
    }
    return false;
}

bool parseBacklightMode(const char* const data, const std::size_t size,
                        std::uint16_t& mode) noexcept {
    if (data == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < kBacklightOptions.size(); ++index) {
        if (std::strlen(kBacklightOptions[index]) == size &&
            std::memcmp(data, kBacklightOptions[index], size) == 0) {
            mode = static_cast<std::uint16_t>(index);
            return true;
        }
    }
    return false;
}

bool parseDelay(const char* const data, const std::size_t size, std::uint16_t& seconds) noexcept {
    if (data == nullptr || size == 0U || size > 4U) {
        return false;
    }
    unsigned value = 0;
    for (std::size_t index = 0; index < size; ++index) {
        if (data[index] < '0' || data[index] > '9') {
            return false;
        }
        value = value * 10U + static_cast<unsigned>(data[index] - '0');
    }
    if (value > kMaxOffDelaySeconds) {
        return false;
    }
    seconds = static_cast<std::uint16_t>(value);
    return true;
}

bool groupShouldTurnOn(const std::uint8_t mask,
                       const std::array<bool, kChannelCount>& on) noexcept {
    for (std::size_t index = 0; index < on.size(); ++index) {
        if (includes(mask, index) && !on[index]) {
            return true;
        }
    }
    return false;
}

std::uint8_t scaledBrightness(const std::uint8_t original, const std::uint8_t original_leader,
                              const std::uint8_t leader) noexcept {
    if (original_leader == 0U) {
        return 1;
    }
    const unsigned value =
        (static_cast<unsigned>(original) * leader + original_leader / 2U) / original_leader;
    return static_cast<std::uint8_t>(std::clamp(value, 1U, 100U));
}
} // namespace local_control
