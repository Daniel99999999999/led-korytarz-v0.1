#pragma once

#include "TemperatureModel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace persistent_settings {

inline constexpr std::size_t kMaxSsidLength = 32;
inline constexpr std::size_t kMaxWifiPasswordLength = 64;
inline constexpr std::size_t kMaxMqttUriLength = 127;
inline constexpr std::size_t kMaxMqttUsernameLength = 63;
inline constexpr std::size_t kMaxMqttPasswordLength = 127;
inline constexpr std::size_t kMaxTopicPrefixLength = 63;
inline constexpr std::size_t kMaxDiscoveryPrefixLength = 31;

inline constexpr char kDefaultTopicPrefix[] = "sterownik-led";
inline constexpr char kDefaultDiscoveryPrefix[] = "homeassistant";

inline constexpr std::int32_t kMinimumInaCurrentCorrectionPpm = -100'000;
inline constexpr std::int32_t kMaximumInaCurrentCorrectionPpm = 100'000;

struct WifiConfig final {
    std::array<char, kMaxSsidLength + 1U> ssid{};
    std::array<char, kMaxWifiPasswordLength + 1U> password{};
    std::uint32_t ip{0};
    std::uint32_t netmask{0};
    std::uint32_t gateway{0};
    std::uint32_t dns_primary{0};
    std::uint32_t dns_secondary{0};
    bool configured{false};
    bool dhcp{true};
};

struct MqttConfig final {
    std::array<char, kMaxMqttUriLength + 1U> uri{};
    std::array<char, kMaxMqttUsernameLength + 1U> username{};
    std::array<char, kMaxMqttPasswordLength + 1U> password{};
    std::array<char, kMaxTopicPrefixLength + 1U> topic_prefix{};
    std::array<char, kMaxDiscoveryPrefixLength + 1U> discovery_prefix{};
    bool configured{false};
};

struct WebCredential final {
    std::array<std::uint8_t, 16> salt{};
    std::array<std::uint8_t, 32> verifier{};
    bool configured{false};
};

struct Data final {
    WifiConfig wifi{};
    MqttConfig mqtt{};
    WebCredential web{};
    TemperatureAssignments temperature_assignments{};
    std::int32_t ina_current_correction_ppm{0};
};

enum class CodecResult : std::uint8_t {
    ok,
    invalid_data,
    buffer_too_small,
};

[[nodiscard]] CodecResult encode(const Data& settings, std::uint8_t* output,
                                 std::size_t output_capacity, std::size_t& output_size) noexcept;
[[nodiscard]] CodecResult decode(const std::uint8_t* payload, std::size_t payload_size,
                                 Data& settings) noexcept;
[[nodiscard]] const char* resultName(CodecResult result) noexcept;

} // namespace persistent_settings
