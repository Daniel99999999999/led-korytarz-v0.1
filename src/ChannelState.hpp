#pragma once

#include <cstdint>

class ChannelState final {
  public:
    enum class DimmingDirection : std::uint8_t {
        brighter,
        dimmer,
    };

    static constexpr std::uint8_t kMinimumBrightnessPercent = 1;
    static constexpr std::uint8_t kMaximumBrightnessPercent = 100;

    explicit ChannelState(std::uint8_t default_brightness_percent = 100) noexcept;

    void reset(std::uint8_t default_brightness_percent) noexcept;
    void turnOn() noexcept;
    void turnOff() noexcept;
    void toggle() noexcept;
    void setBrightness(std::uint8_t percent) noexcept;
    [[nodiscard]] bool beginDimming() noexcept;
    [[nodiscard]] bool stepDimming() noexcept;
    void endDimming() noexcept;

    [[nodiscard]] bool isOn() const noexcept;
    [[nodiscard]] std::uint8_t brightness() const noexcept;
    [[nodiscard]] bool isDimming() const noexcept;
    [[nodiscard]] DimmingDirection dimmingDirection() const noexcept;
    [[nodiscard]] std::uint32_t targetDuty(std::uint32_t maximum_duty) const noexcept;

  private:
    [[nodiscard]] static std::uint8_t normalizeBrightness(std::uint8_t percent) noexcept;

    bool on_{false};
    std::uint8_t brightness_percent_{100};
    bool dimming_active_{false};
    DimmingDirection active_dimming_direction_{DimmingDirection::dimmer};
    DimmingDirection next_dimming_direction_{DimmingDirection::dimmer};
};
