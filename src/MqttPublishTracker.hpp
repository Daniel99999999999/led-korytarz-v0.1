#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class MqttPublishTracker final {
  public:
    enum class Field : std::uint8_t {
        channel_1,
        channel_2,
        channel_3,
        channel_4,
        channel_5,
        pir_1,
        pir_2,
        ina_availability,
        ina_current,
        ina_bus_voltage,
        ina_power,
        ina_energy,
        ina_die_temperature,
        radiator_availability,
        radiator_temperature,
        enclosure_availability,
        enclosure_temperature,
        mapping_1,
        mapping_2,
        mapping_3,
        mapping_4,
        mapping_5,
        delay_1,
        delay_2,
        lcd_mode,
        lcd_override,
        lcd_night,
        pwm_4_frequency,
        pwm_4_minimum,
        pwm_4_on,
        pwm_4_off,
        pwm_4_mode,
        pwm_5_frequency,
        pwm_5_minimum,
        pwm_5_on,
        pwm_5_off,
        pwm_5_mode,
        count,
    };

    static constexpr std::size_t kFieldCount = static_cast<std::size_t>(Field::count);

    [[nodiscard]] bool needsPublish(const Field field,
                                    const std::uint64_t fingerprint) const noexcept {
        const std::size_t index = static_cast<std::size_t>(field);
        return !valid_[index] || (fingerprints_[index] != fingerprint);
    }

    void markPublished(const Field field, const std::uint64_t fingerprint) noexcept {
        const std::size_t index = static_cast<std::size_t>(field);
        fingerprints_[index] = fingerprint;
        valid_[index] = true;
    }

    void invalidate(const Field field) noexcept {
        valid_[static_cast<std::size_t>(field)] = false;
    }

    void invalidateAll() noexcept {
        valid_.fill(false);
    }

    [[nodiscard]] static constexpr Field channelField(const std::size_t index) noexcept {
        return static_cast<Field>(static_cast<std::size_t>(Field::channel_1) + index);
    }

    [[nodiscard]] static constexpr Field pirField(const std::size_t index) noexcept {
        return static_cast<Field>(static_cast<std::size_t>(Field::pir_1) + index);
    }

    [[nodiscard]] static constexpr Field mappingField(const std::size_t index) noexcept {
        return static_cast<Field>(static_cast<std::size_t>(Field::mapping_1) + index);
    }

    [[nodiscard]] static constexpr Field delayField(const std::size_t index) noexcept {
        return static_cast<Field>(static_cast<std::size_t>(Field::delay_1) + index);
    }

    [[nodiscard]] static constexpr Field
    temperatureAvailabilityField(const std::size_t index) noexcept {
        return static_cast<Field>(static_cast<std::size_t>(Field::radiator_availability) +
                                  (index * 2U));
    }

    [[nodiscard]] static constexpr Field temperatureValueField(const std::size_t index) noexcept {
        return static_cast<Field>(static_cast<std::size_t>(Field::radiator_temperature) +
                                  (index * 2U));
    }

  private:
    std::array<std::uint64_t, kFieldCount> fingerprints_{};
    std::array<bool, kFieldCount> valid_{};
};

static_assert(MqttPublishTracker::channelField(4U) == MqttPublishTracker::Field::channel_5);
static_assert(MqttPublishTracker::pirField(1U) == MqttPublishTracker::Field::pir_2);
static_assert(MqttPublishTracker::temperatureAvailabilityField(1U) ==
              MqttPublishTracker::Field::enclosure_availability);
static_assert(MqttPublishTracker::temperatureValueField(1U) ==
              MqttPublishTracker::Field::enclosure_temperature);
