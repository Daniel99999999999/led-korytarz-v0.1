#include "DebugDiscovery.hpp"

#include <array>
#include <cstdio>
namespace debug_discovery {
bool append(cJSON* components, const char* base, const char* device_id) noexcept {
    if (!components || !base || !device_id) {
        return false;
    }
    constexpr std::array<const char*, 4> ids{{"debug", "reset_reason", "uptime", "debug_dropped"}};
    constexpr std::array<const char*, 4> names{
        {"Debug - logi MQTT", "Przyczyna restartu", "Czas pracy", "Pominiete logi"}};
    constexpr std::array<const char*, 4> templates{
        {"{{ value_json.debug }}", "{{ value_json.reset_reason }}", "{{ value_json.uptime_s }}",
         "{{ value_json.dropped }}"}};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        std::array<char, 160> topic{}, command{};
        std::array<char, 80> id{};
        const int a = std::snprintf(topic.data(), topic.size(), "%s/debug/status", base);
        const int b = std::snprintf(command.data(), command.size(), "%s/debug/set", base);
        const int c = std::snprintf(id.data(), id.size(), "%s_%s", device_id, ids[i]);
        if (a < 0 || static_cast<std::size_t>(a) >= topic.size() || b < 0 ||
            static_cast<std::size_t>(b) >= command.size() || c < 0 ||
            static_cast<std::size_t>(c) >= id.size()) {
            return false;
        }
        cJSON* item = cJSON_AddObjectToObject(components, ids[i]);
        if (!item || !cJSON_AddStringToObject(item, "p", i == 0 ? "switch" : "sensor") ||
            !cJSON_AddStringToObject(item, "name", names[i]) ||
            !cJSON_AddStringToObject(item, "unique_id", id.data()) ||
            !cJSON_AddStringToObject(item, "state_topic", topic.data()) ||
            !cJSON_AddStringToObject(item, "entity_category", i == 0 ? "config" : "diagnostic") ||
            !cJSON_AddStringToObject(item, "value_template", templates[i])) {
            return false;
        }
        if (i == 0 && (!cJSON_AddStringToObject(item, "command_topic", command.data()) ||
                       !cJSON_AddStringToObject(item, "payload_on", "ON") ||
                       !cJSON_AddStringToObject(item, "payload_off", "OFF") ||
                       !cJSON_AddBoolToObject(item, "retain", false) ||
                       !cJSON_AddBoolToObject(item, "optimistic", false))) {
            return false;
        }
        if (i == 2 && (!cJSON_AddStringToObject(item, "unit_of_measurement", "s") ||
                       !cJSON_AddStringToObject(item, "device_class", "duration") ||
                       !cJSON_AddStringToObject(item, "state_class", "measurement"))) {
            return false;
        }
    }
    std::array<char, 160> topic{};
    std::array<char, 80> id{};
    const int a = std::snprintf(topic.data(), topic.size(), "%s/stack/status", base);
    const int b = std::snprintf(id.data(), id.size(), "%s_stack", device_id);
    if (a < 0 || static_cast<std::size_t>(a) >= topic.size() || b < 0 ||
        static_cast<std::size_t>(b) >= id.size())
        return false;
    cJSON* item = cJSON_AddObjectToObject(components, "stack");
    if (!item || !cJSON_AddStringToObject(item, "p", "sensor") ||
        !cJSON_AddStringToObject(item, "name", "Stos - diagnostyka") ||
        !cJSON_AddStringToObject(item, "unique_id", id.data()) ||
        !cJSON_AddStringToObject(item, "entity_category", "diagnostic") ||
        !cJSON_AddStringToObject(item, "state_topic", topic.data()) ||
        !cJSON_AddStringToObject(item, "json_attributes_topic", topic.data()) ||
        !cJSON_AddStringToObject(item, "value_template", "{{ value_json.level }}"))
        return false;
    return true;
}
} // namespace debug_discovery
