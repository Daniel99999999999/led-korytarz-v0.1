#pragma once

#include "FeatureConfig.hpp"
#if LED_CONTROLLER_DS18B20_ENABLED
#include "TemperatureModel.hpp"
#endif

#include <array>

namespace device_identity {

inline constexpr char kDisplayName[] = "Sterownik LED";
inline constexpr char kManufacturer[] = "Daniel Parzniewski";
inline constexpr char kModel[] = "ESP32 5-channel PWM";
inline constexpr char kTechnicalSlug[] = "sterownik-led";
inline constexpr char kDeviceIdPrefix[] = "sterownik_led";
inline constexpr char kFirmwareOrigin[] = "sterownik-led-firmware";
inline constexpr char kConsolePrompt[] = "sterownik-led> ";
inline constexpr char kHomeAssistantOtaStatusName[] = "Status OTA";

inline constexpr std::array<const char*, 5> kHomeAssistantChannelNames{{
    "Kanał 1",
    "Kanał 2",
    "Kanał 3",
    "Kanał 4",
    "Kanał 5",
}};

inline constexpr std::array<const char*, 5> kLogChannelNames{{
    "Kanal 1",
    "Kanal 2",
    "Kanal 3",
    "Kanal 4",
    "Kanal 5",
}};

inline constexpr std::array<const char*, 2> kHomeAssistantPirInputNames{{
    "Wejście PIR 1",
    "Wejście PIR 2",
}};

inline constexpr std::array<const char*, 2> kLogPirInputNames{{
    "Wejscie PIR 1",
    "Wejscie PIR 2",
}};

#if LED_CONTROLLER_INA228_ENABLED
inline constexpr std::array<const char*, 5> kHomeAssistantIna228SensorNames{{
    "Prad calkowity",
    "Napiecie szyny 24 V",
    "Moc calkowita",
    "Energia",
    "Temperatura INA228",
}};
#endif

#if LED_CONTROLLER_DS18B20_ENABLED
inline constexpr std::array<const char*, kTemperatureRoleCount>
    kHomeAssistantTemperatureSensorNames{{
        "Temperatura radiatora",
        "Temperatura obudowy",
    }};
#endif

} // namespace device_identity
