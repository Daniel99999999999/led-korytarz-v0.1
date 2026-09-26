#include "PersistentSettings.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace persistent_settings {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'S', 'L', 'D', '1'}};
constexpr std::uint8_t kSchemaVersion = 1;
constexpr std::size_t kHeaderSize = 8;
constexpr std::size_t kTlvHeaderSize = 3;

enum class Field : std::uint8_t {
    wifi_enabled = 1,
    wifi_ssid = 2,
    wifi_password = 3,
    wifi_dhcp = 4,
    wifi_static_ipv4 = 5,
    wifi_dns = 6,
    mqtt_enabled = 16,
    mqtt_uri = 17,
    mqtt_username = 18,
    mqtt_password = 19,
    mqtt_topic_prefix = 20,
    mqtt_discovery_prefix = 21,
    ina_current_correction_ppm = 32,
    temperature_radiator_rom = 33,
    temperature_enclosure_rom = 34,
};

class Writer final {
  public:
    Writer(std::uint8_t* const output, const std::size_t capacity) noexcept
        : output_(output), capacity_(capacity) {}

    [[nodiscard]] bool append(const Field field, const std::uint8_t* const value,
                              const std::size_t length) noexcept {
        if ((value == nullptr) || (length > std::numeric_limits<std::uint16_t>::max()) ||
            (position_ > capacity_) || ((capacity_ - position_) < (kTlvHeaderSize + length))) {
            return false;
        }

        output_[position_++] = static_cast<std::uint8_t>(field);
        output_[position_++] = static_cast<std::uint8_t>(length & 0xFFU);
        output_[position_++] = static_cast<std::uint8_t>((length >> 8U) & 0xFFU);
        std::copy_n(value, length, output_ + position_);
        position_ += length;
        return true;
    }

    [[nodiscard]] bool appendByte(const Field field, const std::uint8_t value) noexcept {
        return append(field, &value, 1);
    }

    [[nodiscard]] bool appendUint32(const Field field, const std::uint32_t value) noexcept {
        const std::array<std::uint8_t, 4> bytes{{
            static_cast<std::uint8_t>(value & 0xFFU),
            static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
            static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
            static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
        }};
        return append(field, bytes.data(), bytes.size());
    }

    [[nodiscard]] bool appendUint64(const Field field, const std::uint64_t value) noexcept {
        std::array<std::uint8_t, 8> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
        }
        return append(field, bytes.data(), bytes.size());
    }

    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }

  private:
    std::uint8_t* output_{nullptr};
    std::size_t capacity_{0};
    std::size_t position_{kHeaderSize};
};

template <std::size_t Size>
[[nodiscard]] std::size_t textLength(const std::array<char, Size>& text) noexcept {
    const auto terminator = std::find(text.begin(), text.end(), '\0');
    return (terminator == text.end()) ? Size : static_cast<std::size_t>(terminator - text.begin());
}

template <std::size_t Size>
[[nodiscard]] bool validText(const std::array<char, Size>& text, const std::size_t minimum,
                             const std::size_t maximum) noexcept {
    const std::size_t length = textLength(text);
    return (length >= minimum) && (length <= maximum) && (length < Size);
}

template <std::size_t Size>
[[nodiscard]] bool appendText(Writer& writer, const Field field,
                              const std::array<char, Size>& text) noexcept {
    return writer.append(field, reinterpret_cast<const std::uint8_t*>(text.data()),
                         textLength(text));
}

[[nodiscard]] bool startsWith(const char* const text, const char* const prefix) noexcept {
    return std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}

[[nodiscard]] bool topicPrefixIsValid(const char* const text) noexcept {
    if ((text == nullptr) || (text[0] == '\0')) {
        return false;
    }
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if ((*cursor == '+') || (*cursor == '#') || (*cursor == ' ')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validate(const Data& settings) noexcept {
    if ((settings.ina_current_correction_ppm < kMinimumInaCurrentCorrectionPpm) ||
        (settings.ina_current_correction_ppm > kMaximumInaCurrentCorrectionPpm)) {
        return false;
    }
    if (!temperature_model::assignmentsAreValid(settings.temperature_assignments)) {
        return false;
    }

    if (settings.wifi.configured) {
        if (!validText(settings.wifi.ssid, 1, kMaxSsidLength) ||
            !validText(settings.wifi.password, 0, kMaxWifiPasswordLength)) {
            return false;
        }
        const std::size_t password_length = textLength(settings.wifi.password);
        if ((password_length > 0U) && (password_length < 8U)) {
            return false;
        }
        if (!settings.wifi.dhcp && ((settings.wifi.ip == 0U) || (settings.wifi.netmask == 0U) ||
                                    (settings.wifi.gateway == 0U))) {
            return false;
        }
    }

    if (settings.mqtt.configured) {
        if (!validText(settings.mqtt.uri, 8, kMaxMqttUriLength) ||
            !startsWith(settings.mqtt.uri.data(), "mqtt://") ||
            !validText(settings.mqtt.username, 1, kMaxMqttUsernameLength) ||
            !validText(settings.mqtt.password, 0, kMaxMqttPasswordLength) ||
            !validText(settings.mqtt.topic_prefix, 1, kMaxTopicPrefixLength) ||
            !validText(settings.mqtt.discovery_prefix, 1, kMaxDiscoveryPrefixLength) ||
            !topicPrefixIsValid(settings.mqtt.topic_prefix.data()) ||
            !topicPrefixIsValid(settings.mqtt.discovery_prefix.data())) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint32_t readUint32(const std::uint8_t* const data) noexcept {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

[[nodiscard]] std::int32_t readInt32(const std::uint8_t* const data) noexcept {
    const std::uint32_t encoded = readUint32(data);
    if (encoded <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return static_cast<std::int32_t>(encoded);
    }
    return -1 - static_cast<std::int32_t>(~encoded);
}

[[nodiscard]] std::uint64_t readUint64(const std::uint8_t* const data) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(data[index]) << (index * 8U);
    }
    return value;
}

template <std::size_t Size>
[[nodiscard]] bool readText(const std::uint8_t* const value, const std::size_t length,
                            std::array<char, Size>& output) noexcept {
    if ((value == nullptr) || (length >= Size) ||
        (std::find(value, value + length, static_cast<std::uint8_t>(0)) != value + length)) {
        return false;
    }
    output.fill('\0');
    std::copy_n(value, length, output.begin());
    return true;
}

[[nodiscard]] bool markSeen(const std::uint8_t type, std::uint64_t& seen) noexcept {
    if (type >= 64U) {
        return true;
    }
    const std::uint64_t mask = std::uint64_t{1} << type;
    if ((seen & mask) != 0U) {
        return false;
    }
    seen |= mask;
    return true;
}

} // namespace

CodecResult encode(const Data& settings, std::uint8_t* const output,
                   const std::size_t output_capacity, std::size_t& output_size) noexcept {
    output_size = 0;
    if ((output == nullptr) || (output_capacity < kHeaderSize) || !validate(settings)) {
        return CodecResult::invalid_data;
    }

    std::fill_n(output, output_capacity, static_cast<std::uint8_t>(0));
    Writer writer(output, output_capacity);
    bool written = true;

    if (settings.wifi.configured) {
        written = writer.appendByte(Field::wifi_enabled, 1U) &&
                  appendText(writer, Field::wifi_ssid, settings.wifi.ssid);
        if (written && (textLength(settings.wifi.password) > 0U)) {
            written = appendText(writer, Field::wifi_password, settings.wifi.password);
        }
        written = written && writer.appendByte(Field::wifi_dhcp, settings.wifi.dhcp ? 1U : 0U);
        if (written && !settings.wifi.dhcp) {
            const std::array<std::uint8_t, 12> static_ipv4{{
                static_cast<std::uint8_t>(settings.wifi.ip & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.ip >> 8U) & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.ip >> 16U) & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.ip >> 24U) & 0xFFU),
                static_cast<std::uint8_t>(settings.wifi.netmask & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.netmask >> 8U) & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.netmask >> 16U) & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.netmask >> 24U) & 0xFFU),
                static_cast<std::uint8_t>(settings.wifi.gateway & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.gateway >> 8U) & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.gateway >> 16U) & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.gateway >> 24U) & 0xFFU),
            }};
            written =
                writer.append(Field::wifi_static_ipv4, static_ipv4.data(), static_ipv4.size());
        }
        if (written && ((settings.wifi.dns_primary != 0U) || (settings.wifi.dns_secondary != 0U))) {
            const std::array<std::uint8_t, 8> dns{{
                static_cast<std::uint8_t>(settings.wifi.dns_primary & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.dns_primary >> 8U) & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.dns_primary >> 16U) & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.dns_primary >> 24U) & 0xFFU),
                static_cast<std::uint8_t>(settings.wifi.dns_secondary & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.dns_secondary >> 8U) & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.dns_secondary >> 16U) & 0xFFU),
                static_cast<std::uint8_t>((settings.wifi.dns_secondary >> 24U) & 0xFFU),
            }};
            written = writer.append(Field::wifi_dns, dns.data(), dns.size());
        }
    }

    if (written && settings.mqtt.configured) {
        written = writer.appendByte(Field::mqtt_enabled, 1U) &&
                  appendText(writer, Field::mqtt_uri, settings.mqtt.uri) &&
                  appendText(writer, Field::mqtt_username, settings.mqtt.username);
        if (written && (textLength(settings.mqtt.password) > 0U)) {
            written = appendText(writer, Field::mqtt_password, settings.mqtt.password);
        }
        if (written && (std::strcmp(settings.mqtt.topic_prefix.data(), kDefaultTopicPrefix) != 0)) {
            written = appendText(writer, Field::mqtt_topic_prefix, settings.mqtt.topic_prefix);
        }
        if (written &&
            (std::strcmp(settings.mqtt.discovery_prefix.data(), kDefaultDiscoveryPrefix) != 0)) {
            written =
                appendText(writer, Field::mqtt_discovery_prefix, settings.mqtt.discovery_prefix);
        }
    }

    if (written && (settings.ina_current_correction_ppm != 0)) {
        written =
            writer.appendUint32(Field::ina_current_correction_ppm,
                                static_cast<std::uint32_t>(settings.ina_current_correction_ppm));
    }
    if (written && (settings.temperature_assignments.rom_ids[0] != 0U)) {
        written = writer.appendUint64(Field::temperature_radiator_rom,
                                      settings.temperature_assignments.rom_ids[0]);
    }
    if (written && (settings.temperature_assignments.rom_ids[1] != 0U)) {
        written = writer.appendUint64(Field::temperature_enclosure_rom,
                                      settings.temperature_assignments.rom_ids[1]);
    }
    if (!written) {
        std::fill_n(output, output_capacity, static_cast<std::uint8_t>(0));
        return CodecResult::buffer_too_small;
    }

    output_size = writer.position();
    std::copy(kMagic.begin(), kMagic.end(), output);
    output[4] = kSchemaVersion;
    output[5] = static_cast<std::uint8_t>(kHeaderSize);
    output[6] = static_cast<std::uint8_t>(output_size & 0xFFU);
    output[7] = static_cast<std::uint8_t>((output_size >> 8U) & 0xFFU);
    return CodecResult::ok;
}

CodecResult decode(const std::uint8_t* const payload, const std::size_t payload_size,
                   Data& settings) noexcept {
    settings = {};
    std::copy_n(kDefaultTopicPrefix, sizeof(kDefaultTopicPrefix),
                settings.mqtt.topic_prefix.begin());
    std::copy_n(kDefaultDiscoveryPrefix, sizeof(kDefaultDiscoveryPrefix),
                settings.mqtt.discovery_prefix.begin());

    if ((payload == nullptr) || (payload_size < kHeaderSize) ||
        !std::equal(kMagic.begin(), kMagic.end(), payload) || (payload[4] != kSchemaVersion) ||
        (payload[5] != kHeaderSize)) {
        return CodecResult::invalid_data;
    }
    const std::size_t declared_size =
        static_cast<std::size_t>(payload[6]) | (static_cast<std::size_t>(payload[7]) << 8U);
    if (declared_size != payload_size) {
        return CodecResult::invalid_data;
    }

    std::size_t position = kHeaderSize;
    std::uint64_t seen = 0;
    while (position < payload_size) {
        if ((payload_size - position) < kTlvHeaderSize) {
            return CodecResult::invalid_data;
        }
        const std::uint8_t type = payload[position++];
        const std::size_t length = static_cast<std::size_t>(payload[position]) |
                                   (static_cast<std::size_t>(payload[position + 1U]) << 8U);
        position += 2U;
        if ((length > (payload_size - position)) || !markSeen(type, seen)) {
            return CodecResult::invalid_data;
        }

        const std::uint8_t* const value = payload + position;
        const Field field = static_cast<Field>(type);
        bool accepted = true;
        switch (field) {
        case Field::wifi_enabled:
            accepted = (length == 1U) && (value[0] == 1U);
            settings.wifi.configured = accepted;
            break;
        case Field::wifi_ssid:
            accepted = readText(value, length, settings.wifi.ssid);
            break;
        case Field::wifi_password:
            accepted = readText(value, length, settings.wifi.password);
            break;
        case Field::wifi_dhcp:
            accepted = (length == 1U) && (value[0] <= 1U);
            if (accepted) {
                settings.wifi.dhcp = value[0] != 0U;
            }
            break;
        case Field::wifi_static_ipv4:
            accepted = length == 12U;
            if (accepted) {
                settings.wifi.ip = readUint32(value);
                settings.wifi.netmask = readUint32(value + 4U);
                settings.wifi.gateway = readUint32(value + 8U);
            }
            break;
        case Field::wifi_dns:
            accepted = length == 8U;
            if (accepted) {
                settings.wifi.dns_primary = readUint32(value);
                settings.wifi.dns_secondary = readUint32(value + 4U);
            }
            break;
        case Field::mqtt_enabled:
            accepted = (length == 1U) && (value[0] == 1U);
            settings.mqtt.configured = accepted;
            break;
        case Field::mqtt_uri:
            accepted = readText(value, length, settings.mqtt.uri);
            break;
        case Field::mqtt_username:
            accepted = readText(value, length, settings.mqtt.username);
            break;
        case Field::mqtt_password:
            accepted = readText(value, length, settings.mqtt.password);
            break;
        case Field::mqtt_topic_prefix:
            accepted = readText(value, length, settings.mqtt.topic_prefix);
            break;
        case Field::mqtt_discovery_prefix:
            accepted = readText(value, length, settings.mqtt.discovery_prefix);
            break;
        case Field::ina_current_correction_ppm:
            accepted = length == 4U;
            if (accepted) {
                settings.ina_current_correction_ppm = readInt32(value);
            }
            break;
        case Field::temperature_radiator_rom:
            accepted = length == 8U;
            if (accepted) {
                settings.temperature_assignments.rom_ids[0] = readUint64(value);
            }
            break;
        case Field::temperature_enclosure_rom:
            accepted = length == 8U;
            if (accepted) {
                settings.temperature_assignments.rom_ids[1] = readUint64(value);
            }
            break;
        default:
            break;
        }
        if (!accepted) {
            settings = {};
            return CodecResult::invalid_data;
        }
        position += length;
    }

    if (!validate(settings)) {
        settings = {};
        return CodecResult::invalid_data;
    }
    return CodecResult::ok;
}

const char* resultName(const CodecResult result) noexcept {
    switch (result) {
    case CodecResult::ok:
        return "ok";
    case CodecResult::invalid_data:
        return "invalid data";
    case CodecResult::buffer_too_small:
        return "buffer too small";
    }
    return "unknown";
}

} // namespace persistent_settings
