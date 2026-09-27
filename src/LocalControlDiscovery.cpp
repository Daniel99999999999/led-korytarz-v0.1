#include "LocalControlDiscovery.hpp"

#include "LocalControlConfig.hpp"

#include <array>
#include <cstdio>

namespace local_control {
bool appendDiscovery(cJSON* const components, const char* const base,
                     const char* const device_id) noexcept {
    if (components == nullptr || base == nullptr || device_id == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < kChannelCount + kPirCount; ++index) {
        const bool button = index < kChannelCount;
        const unsigned number =
            static_cast<unsigned>((button ? index : index - kChannelCount) + 1U);
        const char* const type = button ? "key" : "pir_delay";
        std::array<char, 32> component_id{};
        std::array<char, 64> unique_id{};
        std::array<char, 64> name{};
        std::array<char, 160> command{};
        std::array<char, 160> state{};
        const auto fits = [](const int size, const std::size_t capacity) noexcept {
            return size >= 0 && static_cast<std::size_t>(size) < capacity;
        };
        if (!fits(std::snprintf(component_id.data(), component_id.size(), "%s_%u", type, number),
                  component_id.size()) ||
            !fits(std::snprintf(unique_id.data(), unique_id.size(), "%s_%s_%u", device_id, type,
                                number),
                  unique_id.size()) ||
            !fits(
                std::snprintf(name.data(), name.size(),
                              button ? "KEY%u - przypisanie" : "PIR%u - czas wylaczenia (0 = wyl.)",
                              number),
                name.size()) ||
            !fits(std::snprintf(command.data(), command.size(), "%s/config/%s/%u/set", base, type,
                                number),
                  command.size()) ||
            !fits(std::snprintf(state.data(), state.size(), "%s/config/%s/%u/state", base, type,
                                number),
                  state.size())) {
            return false;
        }
        cJSON* const component = cJSON_AddObjectToObject(components, component_id.data());
        if (component == nullptr ||
            cJSON_AddStringToObject(component, "p", button ? "select" : "number") == nullptr ||
            cJSON_AddStringToObject(component, "name", name.data()) == nullptr ||
            cJSON_AddStringToObject(component, "unique_id", unique_id.data()) == nullptr ||
            cJSON_AddStringToObject(component, "entity_category", "config") == nullptr ||
            cJSON_AddStringToObject(component, "command_topic", command.data()) == nullptr ||
            cJSON_AddStringToObject(component, "state_topic", state.data()) == nullptr ||
            cJSON_AddBoolToObject(component, "retain", false) == nullptr ||
            cJSON_AddBoolToObject(component, "optimistic", false) == nullptr) {
            return false;
        }
        if (button) {
            cJSON* const options = cJSON_AddArrayToObject(component, "options");
            if (options == nullptr) {
                return false;
            }
            for (const char* const option : kButtonOptions) {
                cJSON* const text = cJSON_CreateString(option);
                if (text == nullptr || !cJSON_AddItemToArray(options, text)) {
                    cJSON_Delete(text);
                    return false;
                }
            }
        } else if (cJSON_AddNumberToObject(component, "min", 0) == nullptr ||
                   cJSON_AddNumberToObject(component, "max", kMaxOffDelaySeconds) == nullptr ||
                   cJSON_AddNumberToObject(component, "step", 1) == nullptr ||
                   cJSON_AddStringToObject(component, "mode", "box") == nullptr ||
                   cJSON_AddStringToObject(component, "unit_of_measurement", "s") == nullptr ||
                   cJSON_AddStringToObject(component, "command_template", "{{ value | int }}") ==
                       nullptr) {
            return false;
        }
    }
    constexpr std::array<const char*, 3> ids{{"lcd_mode", "lcd_override", "lcd_night"}};
    constexpr std::array<const char*, 3> names{
        {"LCD - tryb podswietlenia", "LCD - priorytet przycisku", "LCD - noc (HA)"}};
    constexpr std::array<const char*, 3> types{{"select", "number", "switch"}};
    constexpr std::array<const char*, 3> paths{
        {"config/lcd_mode", "config/lcd_override", "lcd/night"}};
    for (std::size_t index = 0; index < ids.size(); ++index) {
        std::array<char, 64> id{};
        std::array<char, 160> command{};
        std::array<char, 160> state{};
        const int a = std::snprintf(id.data(), id.size(), "%s_%s", device_id, ids[index]);
        const int b =
            std::snprintf(command.data(), command.size(), "%s/%s/set", base, paths[index]);
        const int c = std::snprintf(state.data(), state.size(), "%s/%s/state", base, paths[index]);
        if (a < 0 || static_cast<std::size_t>(a) >= id.size() || b < 0 ||
            static_cast<std::size_t>(b) >= command.size() || c < 0 ||
            static_cast<std::size_t>(c) >= state.size()) {
            return false;
        }
        cJSON* component = cJSON_AddObjectToObject(components, ids[index]);
        if (component == nullptr ||
            cJSON_AddStringToObject(component, "p", types[index]) == nullptr ||
            cJSON_AddStringToObject(component, "name", names[index]) == nullptr ||
            cJSON_AddStringToObject(component, "unique_id", id.data()) == nullptr ||
            cJSON_AddStringToObject(component, "command_topic", command.data()) == nullptr ||
            cJSON_AddStringToObject(component, "state_topic", state.data()) == nullptr ||
            cJSON_AddStringToObject(component, "entity_category", "config") == nullptr ||
            cJSON_AddBoolToObject(component, "retain", false) == nullptr ||
            cJSON_AddBoolToObject(component, "optimistic", false) == nullptr) {
            return false;
        }
        if (index == 0U) {
            cJSON* options = cJSON_AddArrayToObject(component, "options");
            if (options == nullptr) {
                return false;
            }
            for (const auto* option : kBacklightOptions) {
                cJSON* text = cJSON_CreateString(option);
                if (text == nullptr || !cJSON_AddItemToArray(options, text)) {
                    cJSON_Delete(text);
                    return false;
                }
            }
        } else if (index == 1U) {
            if (cJSON_AddNumberToObject(component, "min", 1) == nullptr ||
                cJSON_AddNumberToObject(component, "max", kMaxOffDelaySeconds) == nullptr ||
                cJSON_AddNumberToObject(component, "step", 1) == nullptr ||
                cJSON_AddStringToObject(component, "mode", "box") == nullptr ||
                cJSON_AddStringToObject(component, "unit_of_measurement", "s") == nullptr ||
                cJSON_AddStringToObject(component, "command_template", "{{ value | int }}") ==
                    nullptr) {
                return false;
            }
        } else if (cJSON_AddStringToObject(component, "payload_on", "ON") == nullptr ||
                   cJSON_AddStringToObject(component, "payload_off", "OFF") == nullptr) {
            return false;
        }
    }
    for (unsigned channel = 4; channel <= 5; ++channel)
        for (unsigned field = 0; field < 5; ++field) {
            std::array<char, 64> id{}, uid{}, name{};
            std::array<char, 160> command{}, state{};
            std::snprintf(id.data(), id.size(), "pwm_%u_%s", channel, pwm_config::kFields[field]);
            const int u = std::snprintf(uid.data(), uid.size(), "%s_%s", device_id, id.data());
            std::snprintf(name.data(), name.size(), "Kanal %u - %s", channel,
                          pwm_config::kNames[field]);
            const int a = std::snprintf(command.data(), command.size(), "%s/config/pwm/%u/%s/set",
                                        base, channel, pwm_config::kFields[field]);
            const int b = std::snprintf(state.data(), state.size(), "%s/config/pwm/%u/%s/state",
                                        base, channel, pwm_config::kFields[field]);
            if (u < 0 || a < 0 || b < 0 || static_cast<std::size_t>(u) >= uid.size() ||
                static_cast<std::size_t>(a) >= command.size() ||
                static_cast<std::size_t>(b) >= state.size())
                return false;
            auto* item = cJSON_AddObjectToObject(components, id.data());
            if (!item || !cJSON_AddStringToObject(item, "p", field < 2 ? "number" : "select") ||
                !cJSON_AddStringToObject(item, "name", name.data()) ||
                !cJSON_AddStringToObject(item, "unique_id", uid.data()) ||
                !cJSON_AddStringToObject(item, "entity_category", "config") ||
                !cJSON_AddStringToObject(item, "command_topic", command.data()) ||
                !cJSON_AddStringToObject(item, "state_topic", state.data()) ||
                !cJSON_AddBoolToObject(item, "retain", false) ||
                !cJSON_AddBoolToObject(item, "optimistic", false))
                return false;
            if (field < 2) {
                if (!cJSON_AddNumberToObject(item, "min", field == 0 ? pwm_config::kMinHz : 0) ||
                    !cJSON_AddNumberToObject(item, "max", field == 0 ? pwm_config::kMaxHz : 100) ||
                    !cJSON_AddNumberToObject(item, "step", 1) ||
                    !cJSON_AddStringToObject(item, "mode", "box") ||
                    !cJSON_AddStringToObject(item, "unit_of_measurement",
                                             field == 0 ? "Hz" : "%") ||
                    !cJSON_AddStringToObject(item, "command_template", "{{ value | int }}"))
                    return false;
            } else {
                auto* options = cJSON_AddArrayToObject(item, "options");
                if (!options)
                    return false;
                for (unsigned index = 0; index < (field == 4 ? 2U : 16U); ++index) {
                    std::array<char, 12> text{};
                    if (field == 4)
                        std::snprintf(text.data(), text.size(), "%s", index ? "ON/OFF" : "PWM");
                    else
                        std::snprintf(text.data(), text.size(), "%u",
                                      static_cast<unsigned>(pwm_config::kPulseUs[index]));
                    auto* option = cJSON_CreateString(text.data());
                    if (!option || !cJSON_AddItemToArray(options, option)) {
                        cJSON_Delete(option);
                        return false;
                    }
                }
            }
        }
    return true;
}
} // namespace local_control
