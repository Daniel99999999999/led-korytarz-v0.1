#include "LcdDashboard.hpp"

#include <array>
#include <cinttypes>
#include <cstdio>

namespace {
constexpr char kHd44780DegreeSymbol = static_cast<char>(0xDF);

void copyPadded(LcdDashboard::Line& line, const char* const text) noexcept {
    line.fill(' ');
    line[LcdDashboard::kColumnCount] = '\0';

    std::size_t index = 0;
    while ((index < LcdDashboard::kColumnCount) && (text[index] != '\0')) {
        line[index] = text[index];
        ++index;
    }
}

void formatPair(LcdDashboard::Line& line, const std::size_t left_index,
                const LcdDashboard::ChannelSnapshot& left, const std::size_t right_index,
                const LcdDashboard::ChannelSnapshot& right) noexcept {
    std::array<char, 32> text{};
    static_cast<void>(std::snprintf(
        text.data(), text.size(), "%u:%-3s%3u%%  %u:%-3s%3u%%",
        static_cast<unsigned>(left_index + 1U), left.on ? "ON" : "OFF",
        static_cast<unsigned>(left.brightness), static_cast<unsigned>(right_index + 1U),
        right.on ? "ON" : "OFF", static_cast<unsigned>(right.brightness)));
    copyPadded(line, text.data());
}

void formatLastChannel(LcdDashboard::Line& line, const LcdDashboard::ChannelSnapshot& channel,
                       const LcdDashboard::MeasurementSnapshot& measurement,
                       const LcdDashboard::MeasurementPage measurement_page) noexcept {
    std::array<char, 32> text{};
    std::array<char, 16> field{};
    const auto format_temperature = [&field](const char label, const std::int32_t value,
                                             const bool available) noexcept {
        if (!available) {
            static_cast<void>(std::snprintf(field.data(), field.size(), "%c: BRAK", label));
            return;
        }
        const std::int64_t temperature = value;
        const std::uint64_t magnitude =
            static_cast<std::uint64_t>((temperature < 0) ? -temperature : temperature);
        if (temperature < 0) {
            static_cast<void>(
                std::snprintf(field.data(), field.size(), "%c:-%llu.%llu%cC", label,
                              static_cast<unsigned long long>(magnitude / 1'000U),
                              static_cast<unsigned long long>((magnitude % 1'000U) / 100U),
                              kHd44780DegreeSymbol));
        } else {
            static_cast<void>(
                std::snprintf(field.data(), field.size(), "%c:%3llu.%llu%cC", label,
                              static_cast<unsigned long long>(magnitude / 1'000U),
                              static_cast<unsigned long long>((magnitude % 1'000U) / 100U),
                              kHd44780DegreeSymbol));
        }
    };

    switch (measurement_page) {
    case LcdDashboard::MeasurementPage::radiator_temperature:
        if (measurement.radiator_configured) {
            format_temperature('R', measurement.radiator_temperature_millicelsius,
                               measurement.radiator_temperature_available);
        }
        break;
    case LcdDashboard::MeasurementPage::enclosure_temperature:
        if (measurement.enclosure_configured) {
            format_temperature('O', measurement.enclosure_temperature_millicelsius,
                               measurement.enclosure_temperature_available);
        }
        break;
    case LcdDashboard::MeasurementPage::current:
        if (measurement.current_supported) {
            if (measurement.current_available) {
                static_cast<void>(std::snprintf(field.data(), field.size(), "I:%5ldmA",
                                                static_cast<long>(measurement.current_milliamps)));
            } else {
                static_cast<void>(std::snprintf(field.data(), field.size(), "I: BRAK"));
            }
        }
        break;
    }

    static_cast<void>(std::snprintf(text.data(), text.size(), "5:%-3s%3u%% %s",
                                    channel.on ? "ON" : "OFF",
                                    static_cast<unsigned>(channel.brightness), field.data()));
    copyPadded(line, text.data());
}

void formatStatus(LcdDashboard::Line& line, const LcdDashboard::Status status) noexcept {
    switch (status) {
    case LcdDashboard::Status::ok:
        copyPadded(line, "LOKALNIE: OK");
        break;
    case LcdDashboard::Status::fault:
        copyPadded(line, "LOKALNIE: BLAD");
        break;
    case LcdDashboard::Status::watchdog_reset:
        copyPadded(line, "RESET: WATCHDOG");
        break;
    }
}

void formatUptime(LcdDashboard::Line& line, const std::uint64_t uptime_seconds) noexcept {
    constexpr std::uint64_t kSecondsPerMinute = 60;
    constexpr std::uint64_t kSecondsPerHour = 60 * kSecondsPerMinute;
    constexpr std::uint64_t kSecondsPerDay = 24 * kSecondsPerHour;

    const std::uint64_t days = uptime_seconds / kSecondsPerDay;
    const std::uint64_t hours = (uptime_seconds / kSecondsPerHour) % 24U;
    const std::uint64_t minutes = (uptime_seconds / kSecondsPerMinute) % 60U;
    const std::uint64_t seconds = uptime_seconds % 60U;
    std::array<char, 32> text{};
    static_cast<void>(std::snprintf(text.data(), text.size(),
                                    "UP %" PRIu64 "d %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64, days,
                                    hours, minutes, seconds));
    copyPadded(line, text.data());
}

void formatWifi(LcdDashboard::Line& line, const LcdDashboard::NetworkSnapshot& network) noexcept {
    switch (network.state) {
    case LcdDashboard::NetworkState::unavailable:
        copyPadded(line, "WIFI: NIEDOSTEPNE");
        return;
    case LcdDashboard::NetworkState::unconfigured:
        copyPadded(line, "WIFI: BRAK KONFIG");
        return;
    case LcdDashboard::NetworkState::ready:
        copyPadded(line, "WIFI: PRZED APPLY");
        return;
    case LcdDashboard::NetworkState::connecting:
        copyPadded(line, "WIFI: LACZENIE");
        return;
    case LcdDashboard::NetworkState::disconnected:
        copyPadded(line, "WIFI: ROZLACZONE");
        return;
    case LcdDashboard::NetworkState::connected:
        break;
    }

    std::array<char, 48> text{};
    static_cast<void>(std::snprintf(text.data(), text.size(), "%ddBm %s",
                                    static_cast<int>(network.rssi_dbm), network.ssid.data()));
    copyPadded(line, text.data());
}

void formatIp(LcdDashboard::Line& line, const LcdDashboard::NetworkSnapshot& network) noexcept {
    if ((network.state != LcdDashboard::NetworkState::connected) || (network.ip[0] == '\0')) {
        copyPadded(line, "IP: BRAK");
        return;
    }

    std::array<char, 32> text{};
    static_cast<void>(std::snprintf(text.data(), text.size(), "IP %s", network.ip.data()));
    copyPadded(line, text.data());
}

void formatDateTime(LcdDashboard::Line& line,
                    const LcdDashboard::NetworkSnapshot& network) noexcept {
    if (!network.time_synchronized || (network.local_time[0] == '\0')) {
        copyPadded(line, "CZAS: BRAK NTP");
        return;
    }
    copyPadded(line, network.local_time.data());
}

void formatMqtt(LcdDashboard::Line& line, const LcdDashboard::MqttSnapshot& mqtt) noexcept {
    if ((mqtt.state == LcdDashboard::MqttState::connected) && mqtt.publish_backlog) {
        copyPadded(line, "MQTT: KOLEJKA PELNA");
        return;
    }
    switch (mqtt.state) {
    case LcdDashboard::MqttState::unavailable:
        copyPadded(line, "MQTT: NIEDOSTEPNE");
        break;
    case LcdDashboard::MqttState::unconfigured:
        copyPadded(line, "MQTT: BRAK KONFIG");
        break;
    case LcdDashboard::MqttState::ready:
        copyPadded(line, "MQTT: PRZED APPLY");
        break;
    case LcdDashboard::MqttState::connecting:
        copyPadded(line, "MQTT: LACZENIE");
        break;
    case LcdDashboard::MqttState::connected:
        copyPadded(line, "MQTT: POLACZONE");
        break;
    case LcdDashboard::MqttState::disconnected:
        copyPadded(line, "MQTT: ROZLACZONE");
        break;
    case LcdDashboard::MqttState::authentication_error:
        copyPadded(line, "MQTT: BLAD LOGOWANIA");
        break;
    }
}
} // namespace

LcdDashboard::Lines LcdDashboard::render(const std::array<ChannelSnapshot, kChannelCount>& channels,
                                         const Status status, const std::uint64_t uptime_seconds,
                                         const NetworkSnapshot& network, const MqttSnapshot& mqtt,
                                         const InformationPage information_page) noexcept {
    return render(channels, status, uptime_seconds, network, mqtt, information_page,
                  NoticeSnapshot{}, MeasurementSnapshot{}, MeasurementPage::radiator_temperature);
}

LcdDashboard::Lines LcdDashboard::render(const std::array<ChannelSnapshot, kChannelCount>& channels,
                                         const Status status, const std::uint64_t uptime_seconds,
                                         const NetworkSnapshot& network, const MqttSnapshot& mqtt,
                                         const InformationPage information_page,
                                         const NoticeSnapshot& notice) noexcept {
    return render(channels, status, uptime_seconds, network, mqtt, information_page, notice,
                  MeasurementSnapshot{}, MeasurementPage::radiator_temperature);
}

LcdDashboard::Lines LcdDashboard::render(const std::array<ChannelSnapshot, kChannelCount>& channels,
                                         const Status status, const std::uint64_t uptime_seconds,
                                         const NetworkSnapshot& network, const MqttSnapshot& mqtt,
                                         const InformationPage information_page,
                                         const NoticeSnapshot& notice,
                                         const MeasurementSnapshot& measurement,
                                         const MeasurementPage measurement_page) noexcept {
    Lines lines{};
    formatPair(lines[0], 0, channels[0], 1, channels[1]);
    formatPair(lines[1], 2, channels[2], 3, channels[3]);
    formatLastChannel(lines[2], channels[4], measurement, measurement_page);

    if (status != Status::ok) {
        formatStatus(lines[3], status);
    } else if (notice.active) {
        copyPadded(lines[3], notice.text.data());
    } else if (information_page == InformationPage::status) {
        formatStatus(lines[3], status);
    } else if (information_page == InformationPage::uptime) {
        formatUptime(lines[3], uptime_seconds);
    } else if (information_page == InformationPage::wifi) {
        formatWifi(lines[3], network);
    } else if (information_page == InformationPage::ip) {
        formatIp(lines[3], network);
    } else if (information_page == InformationPage::mqtt) {
        formatMqtt(lines[3], mqtt);
    } else {
        formatDateTime(lines[3], network);
    }

    return lines;
}

LcdDashboard::InformationPage
LcdDashboard::nextInformationPage(const InformationPage current) noexcept {
    switch (current) {
    case InformationPage::status:
        return InformationPage::uptime;
    case InformationPage::uptime:
        return InformationPage::wifi;
    case InformationPage::wifi:
        return InformationPage::ip;
    case InformationPage::ip:
        return InformationPage::mqtt;
    case InformationPage::mqtt:
        return InformationPage::date_time;
    case InformationPage::date_time:
        return InformationPage::status;
    }
    return InformationPage::status;
}

LcdDashboard::MeasurementPage
LcdDashboard::nextMeasurementPage(const MeasurementPage current) noexcept {
    switch (current) {
    case MeasurementPage::radiator_temperature:
        return MeasurementPage::enclosure_temperature;
    case MeasurementPage::enclosure_temperature:
        return MeasurementPage::current;
    case MeasurementPage::current:
        return MeasurementPage::radiator_temperature;
    }
    return MeasurementPage::radiator_temperature;
}

LcdDashboard::MeasurementPage
LcdDashboard::nextMeasurementPage(const MeasurementPage current,
                                  const MeasurementSnapshot& measurement) noexcept {
    MeasurementPage candidate = current;
    for (std::size_t attempt = 0; attempt < 3U; ++attempt) {
        candidate = nextMeasurementPage(candidate);
        if (measurementPageEnabled(candidate, measurement)) {
            return candidate;
        }
    }
    return current;
}

bool LcdDashboard::measurementPageEnabled(const MeasurementPage page,
                                          const MeasurementSnapshot& measurement) noexcept {
    switch (page) {
    case MeasurementPage::radiator_temperature:
        return measurement.radiator_configured;
    case MeasurementPage::enclosure_temperature:
        return measurement.enclosure_configured;
    case MeasurementPage::current:
        return measurement.current_supported;
    }
    return false;
}
