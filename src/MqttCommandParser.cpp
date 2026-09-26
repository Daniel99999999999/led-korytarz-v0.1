#include "MqttCommandParser.hpp"

#include "cJSON.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {
class JsonOwner final {
  public:
    explicit JsonOwner(cJSON* const value) noexcept : value_(value) {}
    ~JsonOwner() {
        cJSON_Delete(value_);
    }

    JsonOwner(const JsonOwner&) = delete;
    JsonOwner& operator=(const JsonOwner&) = delete;

    [[nodiscard]] cJSON* get() const noexcept {
        return value_;
    }

  private:
    cJSON* value_;
};

[[nodiscard]] bool isInteger(const double value) noexcept {
    return std::isfinite(value) && (std::trunc(value) == value);
}
} // namespace

namespace mqtt_protocol {

ParseResult parseCommand(const char* const payload, const std::size_t length,
                         ControlCommand& output) noexcept {
    if ((payload == nullptr) || (length == 0U)) {
        return ParseResult::empty_payload;
    }
    if (length > kMaxCommandPayloadLength) {
        return ParseResult::payload_too_long;
    }

    std::array<char, kMaxCommandPayloadLength + 1U> buffer{};
    std::memcpy(buffer.data(), payload, length);
    buffer[length] = '\0';

    const char* parse_end = nullptr;
    JsonOwner root(cJSON_ParseWithLengthOpts(buffer.data(), length + 1U, &parse_end, true));
    if ((root.get() == nullptr) || (parse_end != (buffer.data() + length))) {
        return ParseResult::invalid_json;
    }
    if (!cJSON_IsObject(root.get())) {
        return ParseResult::root_not_object;
    }

    ControlCommand parsed{};
    bool state_seen = false;
    bool brightness_seen = false;
    bool transition_seen = false;

    for (const cJSON* item = root.get()->child; item != nullptr; item = item->next) {
        if ((item->string == nullptr) || (item->string[0] == '\0')) {
            return ParseResult::unsupported_field;
        }

        if (std::strcmp(item->string, "state") == 0) {
            if (state_seen) {
                return ParseResult::duplicate_field;
            }
            state_seen = true;
            if (!cJSON_IsString(item) || (item->valuestring == nullptr)) {
                return ParseResult::invalid_state;
            }
            if (std::strcmp(item->valuestring, "ON") == 0) {
                parsed.state = ControlCommand::StateAction::turn_on;
            } else if (std::strcmp(item->valuestring, "OFF") == 0) {
                parsed.state = ControlCommand::StateAction::turn_off;
            } else {
                return ParseResult::invalid_state;
            }
            continue;
        }

        if (std::strcmp(item->string, "brightness") == 0) {
            if (brightness_seen) {
                return ParseResult::duplicate_field;
            }
            brightness_seen = true;
            if (!cJSON_IsNumber(item) || !isInteger(item->valuedouble) ||
                (item->valuedouble < 0.0) || (item->valuedouble > 255.0)) {
                return ParseResult::invalid_brightness;
            }
            parsed.brightness = static_cast<std::uint8_t>(item->valuedouble);
            parsed.brightness_present = true;
            continue;
        }

        if (std::strcmp(item->string, "transition") == 0) {
            if (transition_seen) {
                return ParseResult::duplicate_field;
            }
            transition_seen = true;
            if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
                (item->valuedouble < 0.0) ||
                (item->valuedouble > (static_cast<double>(kMaxTransitionMs) / 1'000.0))) {
                return ParseResult::invalid_transition;
            }
            const double milliseconds = item->valuedouble * 1'000.0;
            parsed.transition_ms = static_cast<std::uint32_t>(milliseconds + 0.5);
            parsed.transition_present = true;
            continue;
        }

        return ParseResult::unsupported_field;
    }

    if (!state_seen && !brightness_seen) {
        return ParseResult::no_action;
    }

    output = parsed;
    return ParseResult::ok;
}

std::uint8_t brightnessToPercent(const std::uint8_t brightness) noexcept {
    if (brightness == 0U) {
        return 1U;
    }
    const std::uint32_t scaled = ((static_cast<std::uint32_t>(brightness) * 100U) + 127U) / 255U;
    return static_cast<std::uint8_t>(std::clamp<std::uint32_t>(scaled, 1U, 100U));
}

std::uint8_t percentToBrightness(const std::uint8_t percent) noexcept {
    const std::uint32_t limited = std::clamp<std::uint32_t>(percent, 1U, 100U);
    return static_cast<std::uint8_t>(((limited * 255U) + 50U) / 100U);
}

const char* parseResultName(const ParseResult result) noexcept {
    switch (result) {
    case ParseResult::ok:
        return "ok";
    case ParseResult::empty_payload:
        return "pusty payload";
    case ParseResult::payload_too_long:
        return "payload za dlugi";
    case ParseResult::invalid_json:
        return "niepoprawny JSON";
    case ParseResult::root_not_object:
        return "korzen JSON nie jest obiektem";
    case ParseResult::unsupported_field:
        return "nieobslugiwane pole";
    case ParseResult::duplicate_field:
        return "powtorzone pole";
    case ParseResult::invalid_state:
        return "niepoprawny state";
    case ParseResult::invalid_brightness:
        return "niepoprawny brightness";
    case ParseResult::invalid_transition:
        return "niepoprawny transition";
    case ParseResult::no_action:
        return "brak state i brightness";
    }
    return "nieznany blad";
}

} // namespace mqtt_protocol
