#include "MqttDiscoveryPayload.hpp"

#include "DebugDiscovery.hpp"
#include "DeviceIdentity.hpp"
#include "LocalControlDiscovery.hpp"
#include "cJSON.h"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

namespace mqtt_discovery {
namespace {
constexpr char kOnline[] = "online";
constexpr char kOffline[] = "offline";

class JsonOwner final {
  public:
    explicit JsonOwner(cJSON* value) noexcept : value_(value) {}
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

class ReleaseObserver final {
  public:
    ReleaseObserver(StageObserver observer, void* context) noexcept
        : observer_(observer), context_(context) {}
    ~ReleaseObserver() {
        if (observer_ != nullptr)
            observer_(Stage::after_free, context_);
    }

  private:
    StageObserver observer_;
    void* context_;
};

[[nodiscard]] bool addString(cJSON* object, const char* name, const char* value) noexcept {
    return cJSON_AddStringToObject(object, name, value) != nullptr;
}

[[nodiscard]] bool addNumber(cJSON* object, const char* name, double value) noexcept {
    return cJSON_AddNumberToObject(object, name, value) != nullptr;
}

[[nodiscard]] bool addBool(cJSON* object, const char* name, bool value) noexcept {
    return cJSON_AddBoolToObject(object, name, value) != nullptr;
}

template <std::size_t TopicCount>
[[nodiscard]] bool addAvailabilityList(cJSON* object,
                                       const std::array<const char*, TopicCount>& topics,
                                       const char* mode = nullptr) noexcept {
    cJSON* const availability = cJSON_AddArrayToObject(object, "availability");
    if (availability == nullptr)
        return false;
    for (const char* topic : topics) {
        cJSON* const entry = cJSON_CreateObject();
        if (entry == nullptr)
            return false;
        cJSON_AddItemToArray(availability, entry);
        if (!addString(entry, "topic", topic))
            return false;
    }
    return (mode == nullptr) || addString(object, "availability_mode", mode);
}

void observe(StageObserver observer, void* context, Stage stage) noexcept {
    if (observer != nullptr)
        observer(stage, context);
}
} // namespace

Result build(const Topics& topics, const char* device_id, const char* version, char* output,
             const std::size_t capacity, StageObserver observer, void* observer_context) noexcept {
    Result result{};
    if (output == nullptr || capacity == 0 || capacity > INT_MAX || device_id == nullptr ||
        version == nullptr || topics.base == nullptr || topics.availability == nullptr) {
        return result;
    }
    output[0] = '\0';
    observe(observer, observer_context, Stage::before_tree);
    ReleaseObserver release_observer(observer, observer_context);
    JsonOwner root(cJSON_CreateObject());
    if (root.get() == nullptr)
        return result;

    cJSON* const device = cJSON_AddObjectToObject(root.get(), "dev");
    cJSON* const origin = cJSON_AddObjectToObject(root.get(), "o");
    cJSON* const components = cJSON_AddObjectToObject(root.get(), "cmps");
    if (device == nullptr || origin == nullptr || components == nullptr)
        return result;

    result.failure = Failure::local;
    if (!local_control::appendDiscovery(components, topics.base, device_id))
        return result;
    result.failure = Failure::debug;
    if (!debug_discovery::append(components, topics.base, device_id))
        return result;

    result.failure = Failure::component;
    const std::array<const char*, 1> device_availability{{topics.availability}};
    if (!addString(device, "ids", device_id) ||
        !addString(device, "name", device_identity::kDisplayName) ||
        !addString(device, "mf", device_identity::kManufacturer) ||
        !addString(device, "mdl", device_identity::kModel) || !addString(device, "sw", version) ||
        !addString(device, "sn", device_id) ||
        !addString(origin, "name", device_identity::kFirmwareOrigin) ||
        !addString(origin, "sw", version) ||
        !addAvailabilityList(root.get(), device_availability) ||
        !addString(root.get(), "payload_available", kOnline) ||
        !addString(root.get(), "payload_not_available", kOffline) ||
        !addNumber(root.get(), "qos", 1))
        return result;

    for (std::size_t index = 0; index < topics.command.size(); ++index) {
        std::array<char, 24> component_id{};
        std::array<char, 48> unique_id{};
        const unsigned channel = static_cast<unsigned>(index + 1U);
        const int component_written =
            std::snprintf(component_id.data(), component_id.size(), "channel_%u", channel);
        const int unique_written =
            std::snprintf(unique_id.data(), unique_id.size(), "%s_ch%u", device_id, channel);
        if (component_written < 0 ||
            static_cast<std::size_t>(component_written) >= component_id.size() ||
            unique_written < 0 || static_cast<std::size_t>(unique_written) >= unique_id.size())
            return result;

        cJSON* const component = cJSON_AddObjectToObject(components, component_id.data());
        if (component == nullptr)
            return result;
        cJSON* const color_modes = cJSON_AddArrayToObject(component, "supported_color_modes");
        cJSON* const brightness_mode = cJSON_CreateString("brightness");
        if (color_modes == nullptr || brightness_mode == nullptr) {
            cJSON_Delete(brightness_mode);
            return result;
        }
        cJSON_AddItemToArray(color_modes, brightness_mode);

        if (!addString(component, "p", "light") ||
            !addString(component, "name", device_identity::kHomeAssistantChannelNames[index]) ||
            !addString(component, "unique_id", unique_id.data()) ||
            !addString(component, "command_topic", topics.command[index]) ||
            !addString(component, "state_topic", topics.state[index]) ||
            !addString(component, "schema", "json") ||
            !addNumber(component, "brightness_scale", 255) ||
            !addBool(component, "transition", true) || !addBool(component, "flash", false) ||
            !addString(component, "icon", "mdi:lightbulb"))
            return result;
    }

    for (std::size_t index = 0; index < topics.pir_state.size(); ++index) {
        std::array<char, 24> component_id{};
        std::array<char, 48> unique_id{};
        const unsigned input = static_cast<unsigned>(index + 1U);
        const int component_written =
            std::snprintf(component_id.data(), component_id.size(), "pir_%u", input);
        const int unique_written =
            std::snprintf(unique_id.data(), unique_id.size(), "%s_pir%u", device_id, input);
        if (component_written < 0 ||
            static_cast<std::size_t>(component_written) >= component_id.size() ||
            unique_written < 0 || static_cast<std::size_t>(unique_written) >= unique_id.size())
            return result;
        cJSON* const component = cJSON_AddObjectToObject(components, component_id.data());
        if (component == nullptr || !addString(component, "p", "binary_sensor") ||
            !addString(component, "name", device_identity::kHomeAssistantPirInputNames[index]) ||
            !addString(component, "unique_id", unique_id.data()) ||
            !addString(component, "state_topic", topics.pir_state[index]) ||
            !addString(component, "payload_on", "ON") ||
            !addString(component, "payload_off", "OFF") ||
            !addString(component, "device_class", "motion"))
            return result;
    }

    std::array<char, 64> ota_unique_id{};
    const int ota_written =
        std::snprintf(ota_unique_id.data(), ota_unique_id.size(), "%s_ota_status", device_id);
    if (ota_written < 0 || static_cast<std::size_t>(ota_written) >= ota_unique_id.size())
        return result;
    cJSON* const ota_component = cJSON_AddObjectToObject(components, "ota_status");
    if (ota_component == nullptr || !addString(ota_component, "p", "sensor") ||
        !addString(ota_component, "name", device_identity::kHomeAssistantOtaStatusName) ||
        !addString(ota_component, "unique_id", ota_unique_id.data()) ||
        !addString(ota_component, "state_topic", topics.ota_status) ||
        !addString(ota_component, "value_template", "{{ value_json.state }}") ||
        !addString(ota_component, "json_attributes_topic", topics.ota_status) ||
        !addString(ota_component, "entity_category", "diagnostic") ||
        !addString(ota_component, "icon", "mdi:update"))
        return result;

#if LED_CONTROLLER_INA228_ENABLED
    struct SensorDefinition final {
        const char* component_id;
        const char* unique_suffix;
        const char* name;
        const char* state_topic;
        const char* device_class;
        const char* unit;
        const char* state_class;
        const char* icon;
    };
    const std::array<SensorDefinition, 5> ina_sensors{{
        {"ina228_current", "ina_current", device_identity::kHomeAssistantIna228SensorNames[0],
         topics.ina228_state[0], "current", "mA", "measurement", "mdi:current-dc"},
        {"ina228_bus_voltage", "ina_voltage", device_identity::kHomeAssistantIna228SensorNames[1],
         topics.ina228_state[1], "voltage", "mV", "measurement", "mdi:flash"},
        {"ina228_power", "ina_power", device_identity::kHomeAssistantIna228SensorNames[2],
         topics.ina228_state[2], "power", "mW", "measurement", "mdi:meter-electric"},
        {"ina228_energy", "ina_energy", device_identity::kHomeAssistantIna228SensorNames[3],
         topics.ina228_state[3], "energy", "Wh", "total_increasing", "mdi:lightning-bolt"},
        {"ina228_die_temperature", "ina_temperature",
         device_identity::kHomeAssistantIna228SensorNames[4], topics.ina228_state[4], "temperature",
         "\xC2\xB0"
         "C",
         "measurement", "mdi:thermometer"},
    }};
    for (const SensorDefinition& sensor : ina_sensors) {
        std::array<char, 64> unique_id{};
        const int written = std::snprintf(unique_id.data(), unique_id.size(), "%s_%s", device_id,
                                          sensor.unique_suffix);
        if (written < 0 || static_cast<std::size_t>(written) >= unique_id.size())
            return result;
        cJSON* const component = cJSON_AddObjectToObject(components, sensor.component_id);
        if (component == nullptr || !addString(component, "p", "sensor") ||
            !addString(component, "name", sensor.name) ||
            !addString(component, "unique_id", unique_id.data()) ||
            !addString(component, "state_topic", sensor.state_topic) ||
            !addString(component, "device_class", sensor.device_class) ||
            !addString(component, "unit_of_measurement", sensor.unit) ||
            !addString(component, "state_class", sensor.state_class) ||
            !addString(component, "icon", sensor.icon))
            return result;
        const std::array<const char*, 2> sensor_availability{
            {topics.availability, topics.ina228_availability}};
        if (!addAvailabilityList(component, sensor_availability, "all"))
            return result;
    }
#endif

#if LED_CONTROLLER_DS18B20_ENABLED
    struct TemperatureSensorDefinition final {
        const char* component_id;
        const char* unique_suffix;
        const char* name;
        const char* icon;
    };
    const std::array<TemperatureSensorDefinition, 2> temperature_sensors{{
        {"temperature_radiator", "temperature_radiator",
         device_identity::kHomeAssistantTemperatureSensorNames[0], "mdi:radiator"},
        {"temperature_enclosure", "temperature_enclosure",
         device_identity::kHomeAssistantTemperatureSensorNames[1], "mdi:thermometer"},
    }};
    for (std::size_t index = 0; index < temperature_sensors.size(); ++index) {
        const TemperatureSensorDefinition& sensor = temperature_sensors[index];
        std::array<char, 72> unique_id{};
        const int written = std::snprintf(unique_id.data(), unique_id.size(), "%s_%s", device_id,
                                          sensor.unique_suffix);
        if (written < 0 || static_cast<std::size_t>(written) >= unique_id.size())
            return result;
        cJSON* const component = cJSON_AddObjectToObject(components, sensor.component_id);
        if (component == nullptr || !addString(component, "p", "sensor") ||
            !addString(component, "name", sensor.name) ||
            !addString(component, "unique_id", unique_id.data()) ||
            !addString(component, "state_topic", topics.temperature_state[index]) ||
            !addString(component, "device_class", "temperature") ||
            !addString(component, "unit_of_measurement",
                       "\xC2\xB0"
                       "C") ||
            !addString(component, "state_class", "measurement") ||
            !addString(component, "icon", sensor.icon))
            return result;
        const std::array<const char*, 2> sensor_availability{
            {topics.availability, topics.temperature_availability[index]}};
        if (!addAvailabilityList(component, sensor_availability, "all"))
            return result;
    }
#endif

    observe(observer, observer_context, Stage::after_tree);
    result.failure = Failure::buffer;
    const bool serialized =
        cJSON_PrintPreallocated(root.get(), output, static_cast<int>(capacity), false);
    observe(observer, observer_context, Stage::after_serialization);
    if (!serialized) {
        output[0] = '\0';
        return result;
    }
    result.bytes = std::strlen(output);
    result.failure = Failure::none;
    return result;
}

} // namespace mqtt_discovery
