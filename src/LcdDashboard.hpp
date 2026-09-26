#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class LcdDashboard final {
  public:
    static constexpr std::size_t kChannelCount = 5;
    static constexpr std::size_t kColumnCount = 20;
    static constexpr std::size_t kRowCount = 4;

    struct ChannelSnapshot {
        bool on;
        std::uint8_t brightness;
    };

    enum class Status : std::uint8_t {
        ok,
        fault,
        watchdog_reset,
    };

    enum class InformationPage : std::uint8_t {
        status,
        uptime,
        wifi,
        ip,
        mqtt,
        date_time,
    };

    enum class MeasurementPage : std::uint8_t {
        radiator_temperature,
        enclosure_temperature,
        current,
    };

    struct MeasurementSnapshot {
        std::int32_t radiator_temperature_millicelsius{0};
        std::int32_t enclosure_temperature_millicelsius{0};
        std::int32_t current_milliamps{0};
        bool radiator_configured{false};
        bool radiator_temperature_available{false};
        bool enclosure_configured{false};
        bool enclosure_temperature_available{false};
        bool current_supported{false};
        bool current_available{false};
    };

    enum class NetworkState : std::uint8_t {
        unavailable,
        unconfigured,
        ready,
        connecting,
        connected,
        disconnected,
    };

    struct NetworkSnapshot {
        NetworkState state{NetworkState::unavailable};
        std::array<char, 33> ssid{};
        std::array<char, 16> ip{};
        std::array<char, 32> local_time{};
        std::int8_t rssi_dbm{0};
        bool time_synchronized{false};
    };

    enum class MqttState : std::uint8_t {
        unavailable,
        unconfigured,
        ready,
        connecting,
        connected,
        disconnected,
        authentication_error,
    };

    struct MqttSnapshot {
        MqttState state{MqttState::unavailable};
        bool publish_backlog{false};
    };

    using Line = std::array<char, kColumnCount + 1U>;
    using Lines = std::array<Line, kRowCount>;

    struct NoticeSnapshot {
        bool active{false};
        Line text{};
    };

    [[nodiscard]] static Lines render(const std::array<ChannelSnapshot, kChannelCount>& channels,
                                      Status status, std::uint64_t uptime_seconds,
                                      const NetworkSnapshot& network, const MqttSnapshot& mqtt,
                                      InformationPage information_page) noexcept;
    [[nodiscard]] static Lines render(const std::array<ChannelSnapshot, kChannelCount>& channels,
                                      Status status, std::uint64_t uptime_seconds,
                                      const NetworkSnapshot& network, const MqttSnapshot& mqtt,
                                      InformationPage information_page,
                                      const NoticeSnapshot& notice) noexcept;
    [[nodiscard]] static Lines
    render(const std::array<ChannelSnapshot, kChannelCount>& channels, Status status,
           std::uint64_t uptime_seconds, const NetworkSnapshot& network, const MqttSnapshot& mqtt,
           InformationPage information_page, const NoticeSnapshot& notice,
           const MeasurementSnapshot& measurement, MeasurementPage measurement_page) noexcept;
    [[nodiscard]] static InformationPage nextInformationPage(InformationPage current) noexcept;
    [[nodiscard]] static MeasurementPage nextMeasurementPage(MeasurementPage current) noexcept;
    [[nodiscard]] static MeasurementPage
    nextMeasurementPage(MeasurementPage current, const MeasurementSnapshot& measurement) noexcept;
    [[nodiscard]] static bool
    measurementPageEnabled(MeasurementPage page, const MeasurementSnapshot& measurement) noexcept;
};
