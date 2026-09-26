#pragma once

#include "DeviceIdentity.hpp"
#include "FeatureConfig.hpp"
#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "driver/ledc.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace board_config {

inline constexpr ledc_mode_t kLedcSpeedMode = LEDC_LOW_SPEED_MODE;
inline constexpr ledc_timer_t kLedcTimer = LEDC_TIMER_0;
inline constexpr ledc_timer_bit_t kPwmResolution = LEDC_TIMER_12_BIT;
// API przyjmuje cale Hz; dzielnik 80 MHz / 4096 daje 19 531,25 Hz.
inline constexpr std::uint32_t kPwmFrequencyHz = 19'531;
inline constexpr std::uint32_t kPwmMaxDuty = (1U << 12U) - 1U;

inline constexpr std::uint32_t kFadeTimeMs = 250;
inline constexpr std::uint8_t kDefaultBrightnessPercent = 100;

inline constexpr std::uint32_t kButtonDebounceMs = 25;
inline constexpr std::uint32_t kButtonPollIntervalMs = 5;
inline constexpr std::uint32_t kButtonLongPressMs = 500;
inline constexpr bool kButtonInternalPullUpsEnabled = true;
inline constexpr std::uint32_t kBrightnessStepIntervalMs = 40;
inline constexpr std::uint32_t kBrightnessRampTimeMs = 99U * kBrightnessStepIntervalMs;

// Wbudowana niebieska LED plytki DOIT. GPIO2 jest pinem strapujacym,
// dlatego konfigurujemy go dopiero po wejsciu do app_main().
inline constexpr gpio_num_t kStatusLedPin = GPIO_NUM_2;
inline constexpr bool kStatusLedActiveHigh = true;
inline constexpr std::uint32_t kStatusLedBlinkPeriodMs = 1'000;
inline constexpr std::uint32_t kStatusLedToggleIntervalMs = kStatusLedBlinkPeriodMs / 2U;
inline constexpr std::uint32_t kTaskWatchdogTimeoutMs = 5'000;

// ROM bootloader nadal korzysta z TX0/RX0 GPIO1/GPIO3. Aplikacja zachowuje TX0
// na GPIO1, ale przenosi RX konsoli na wolne wejscie VN, aby ominac TX CP2102.
inline constexpr gpio_num_t kServiceConsoleTxPin = GPIO_NUM_1;
inline constexpr gpio_num_t kServiceConsoleRxPin = GPIO_NUM_39;
inline constexpr std::uint32_t kServiceConsoleRxExternalPullUpOhms = 10'000;
inline constexpr std::uint32_t kServiceConsoleRxSeriesResistorOhms = 1'000;

struct LedDefinition {
    const char* name;
    gpio_num_t pwm_pin;
    ledc_channel_t ledc_channel;
};

struct ButtonDefinition {
    const char* name;
    gpio_num_t pin;
    bool active_low;
};

struct PirInputDefinition {
    const char* name;
    gpio_num_t pin;
};

inline constexpr std::array<LedDefinition, 5> kLeds{{
    {device_identity::kLogChannelNames[0], GPIO_NUM_25, LEDC_CHANNEL_0},
    {device_identity::kLogChannelNames[1], GPIO_NUM_26, LEDC_CHANNEL_1},
    {device_identity::kLogChannelNames[2], GPIO_NUM_27, LEDC_CHANNEL_2},
    {device_identity::kLogChannelNames[3], GPIO_NUM_32, LEDC_CHANNEL_3},
    {device_identity::kLogChannelNames[4], GPIO_NUM_33, LEDC_CHANNEL_4},
}};

inline constexpr std::array<ButtonDefinition, 5> kButtons{{
    {"Przycisk kanal 1", GPIO_NUM_13, true},
    {"Przycisk kanal 2", GPIO_NUM_14, true},
    {"Przycisk kanal 3", GPIO_NUM_16, true},
    {"Przycisk kanal 4", GPIO_NUM_17, true},
    {"Przycisk kanal 5", GPIO_NUM_18, true},
}};

// Wspolna magistrala LCD i opcjonalnego EEPROM-u. AT24C08N jest podlaczony
// po stronie 3,3 V translatora BSS138 obslugujacego PCF8574AT.
inline constexpr i2c_port_num_t kSharedI2cPort = I2C_NUM_0;
inline constexpr gpio_num_t kSharedI2cSdaPin = GPIO_NUM_21;
inline constexpr gpio_num_t kSharedI2cSclPin = GPIO_NUM_22;
inline constexpr std::uint32_t kSharedI2cFrequencyHz = 100'000;
inline constexpr std::uint32_t kSharedI2cTimeoutMs = 20;
// Optional DS1307: only battery-backed RAM 0x08..0x3F, never RTC/SQW registers.
inline constexpr std::uint8_t kDs1307Address = 0x68;

inline constexpr gpio_num_t kLcdBacklightButtonPin = GPIO_NUM_19;
inline constexpr bool kLcdBacklightButtonActiveLow = true;
inline constexpr std::uint32_t kLcdBacklightButtonExternalPullUpOhms = 10'000;

// Opcjonalna magistrala 1-Wire jest zasilana trzyprzewodowo z 3,3 V.
inline constexpr gpio_num_t kDs18b20DataPin = GPIO_NUM_4;
#if LED_CONTROLLER_DS18B20_ENABLED
inline constexpr std::uint32_t kDs18b20ExternalPullUpOhms = 4'700;
inline constexpr std::size_t kDs18b20MaximumDeviceCount = 8;
inline constexpr std::uint32_t kDs18b20ConversionTimeMs = 800;
inline constexpr std::uint32_t kDs18b20SampleIntervalMs = 2'000;
inline constexpr std::uint32_t kDs18b20RescanIntervalMs = 30'000;
inline constexpr std::uint32_t kDs18b20FaultRetryIntervalMs = 5'000;
#endif
// /ALERT INA228 jest nadal rezerwacja; telemetria nie korzysta jeszcze z przerwania.
inline constexpr gpio_num_t kIna228AlertPin = GPIO_NUM_23;
#if LED_CONTROLLER_INA228_ENABLED
// A0 i A1 sa zwarte do GND na PCB, co daje adres 7-bitowy 0x40.
inline constexpr std::uint16_t kIna228I2cAddress = 0x40;
inline constexpr std::uint32_t kIna228ProbeTimeoutMs = 2;
inline constexpr std::uint32_t kIna228SampleIntervalMs = 1'000;
inline constexpr std::uint32_t kIna228AbsentRetryIntervalMs = 30'000;
inline constexpr std::uint32_t kIna228FaultRetryIntervalMs = 5'000;
#endif
#if LED_CONTROLLER_IR_NEC_ENABLED
// GPIO36 jest tylko wejsciem i nie ma wewnetrznego pull-up. Odbiornik IR wymaga
// zewnetrznego podciagniecia 10 kOhm do 3,3 V.
inline constexpr gpio_num_t kIrReceiverPin = GPIO_NUM_36;
inline constexpr std::uint32_t kIrReceiverExternalPullUpOhms = 10'000;
#endif

inline constexpr std::uint16_t kLcdI2cAddress = 0x3F;
inline constexpr std::uint16_t kLcdI2cWriteAddress = 0x7E;
inline constexpr std::uint32_t kLcdRefreshIntervalMs = 1'000;
inline constexpr std::uint32_t kLcdInformationPageIntervalMs = 2'000;
inline constexpr std::uint32_t kLcdMeasurementPageIntervalMs = 2'000;
inline constexpr std::uint32_t kLcdRecoveryRetryIntervalMs = 2'000;
inline constexpr std::uint32_t kLcdPowerStabilizationMs = 250;
inline constexpr std::uint32_t kLcdInitializationPassGapMs = 100;
inline constexpr std::uint8_t kLcdInitializationPassCount = 2;
inline constexpr std::uint32_t kLcdRecoveryFrameIntervalMs = 100;
inline constexpr std::uint8_t kLcdRecoveryFrameCount = 2;
inline constexpr std::size_t kLcdColumns = 20;
inline constexpr std::size_t kLcdRows = 4;
inline constexpr bool kLcdBacklightDefaultOn = true;

#if LED_CONTROLLER_AT24C08_ENABLED
// AT24C08N-SU27 na wspolnej magistrali, zasilany bezposrednio z 3,3 V.
inline constexpr std::uint32_t kSettingsProbeTimeoutMs = 1;
inline constexpr std::int64_t kSettingsWritePollIntervalUs = 1'000;
inline constexpr std::int64_t kSettingsWriteCycleTimeoutUs = 10'000;
inline constexpr std::uint16_t kSettingsEepromBaseAddress = 0x50;
inline constexpr std::size_t kSettingsEepromBlockCount = 4;
inline constexpr std::size_t kSettingsEepromBlockSize = 256;
inline constexpr std::size_t kSettingsEepromPageSize = 16;
inline constexpr std::size_t kSettingsEepromSize =
    kSettingsEepromBlockCount * kSettingsEepromBlockSize;
#endif

// Standardowa mapa backpacka HD44780 z PCF8574AT.
inline constexpr std::uint8_t kLcdPcfRsMask = 1U << 0U;
inline constexpr std::uint8_t kLcdPcfRwMask = 1U << 1U;
inline constexpr std::uint8_t kLcdPcfEnableMask = 1U << 2U;
inline constexpr std::uint8_t kLcdPcfBacklightMask = 1U << 3U;
inline constexpr std::uint8_t kLcdPcfDataShift = 4U;
inline constexpr std::array<std::uint8_t, kLcdRows> kLcdDdramRowOffsets{{
    0x00,
    0x40,
    0x14,
    0x54,
}};

inline constexpr std::array<PirInputDefinition, 2> kPirInputs{{
    {device_identity::kLogPirInputNames[0], GPIO_NUM_34},
    {device_identity::kLogPirInputNames[1], GPIO_NUM_35},
}};
// Zewnetrzny pull-up 10 kOhm; NC zwiera do GND. Ruch lub przerwa daje HIGH.
inline constexpr bool kPirActiveHigh = true;
inline constexpr std::uint32_t kPirExternalPullUpOhms = 10'000;
// Filtruje tylko drgania styku. Czasy automatyki ruchowej pozostaja osobna decyzja.
inline constexpr std::uint32_t kPirContactDebounceMs = 25;

inline constexpr std::array<gpio_num_t, 4> kForbiddenPins{{
    GPIO_NUM_0,
    GPIO_NUM_5,
    GPIO_NUM_12,
    GPIO_NUM_15,
}};

[[nodiscard]] constexpr bool isForbiddenPin(const gpio_num_t pin) noexcept {
    for (std::size_t index = 0; index < kForbiddenPins.size(); ++index) {
        if (pin == kForbiddenPins[index]) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr bool configuredPinsAreAllowed() noexcept {
    for (std::size_t index = 0; index < kLeds.size(); ++index) {
        if (isForbiddenPin(kLeds[index].pwm_pin)) {
            return false;
        }
    }

    for (std::size_t index = 0; index < kButtons.size(); ++index) {
        if (isForbiddenPin(kButtons[index].pin)) {
            return false;
        }
    }

    for (std::size_t index = 0; index < kPirInputs.size(); ++index) {
        if (isForbiddenPin(kPirInputs[index].pin)) {
            return false;
        }
    }

    bool allowed = !isForbiddenPin(kStatusLedPin) && !isForbiddenPin(kServiceConsoleTxPin) &&
                   !isForbiddenPin(kServiceConsoleRxPin) && !isForbiddenPin(kSharedI2cSdaPin) &&
                   !isForbiddenPin(kSharedI2cSclPin) && !isForbiddenPin(kLcdBacklightButtonPin) &&
                   !isForbiddenPin(kDs18b20DataPin) && !isForbiddenPin(kIna228AlertPin);
#if LED_CONTROLLER_IR_NEC_ENABLED
    allowed = allowed && !isForbiddenPin(kIrReceiverPin);
#endif
    return allowed;
}

static_assert(kLeds.size() == kButtons.size(), "Kazdy kanal LED musi miec odpowiadajacy przycisk");
static_assert((kServiceConsoleTxPin == GPIO_NUM_1) && (kServiceConsoleRxPin == GPIO_NUM_39),
              "Konsola aplikacji musi uzywac TX0 GPIO1 i zewnetrznego RX GPIO39/VN");
static_assert(kServiceConsoleRxExternalPullUpOhms == 10'000U,
              "GPIO39/VN wymaga zewnetrznego pull-up 10 kOhm");
static_assert(kDs18b20DataPin != kIna228AlertPin,
              "DS18B20 i INA228 musza miec oddzielne wejscia GPIO");
#if LED_CONTROLLER_IR_NEC_ENABLED
static_assert(kIrReceiverPin == GPIO_NUM_36, "Odbiornik IR musi korzystac z GPIO36");
static_assert((kIrReceiverPin != kDs18b20DataPin) && (kIrReceiverPin != kIna228AlertPin),
              "Odbiornik IR nie moze wspoldzielic GPIO z DS18B20 ani INA228");
#endif
static_assert(kPwmMaxDuty == 4095U, "PWM 12-bit musi miec zakres duty 0-4095");
static_assert(kButtonLongPressMs > kButtonDebounceMs, "Long press musi byc dluzszy od debounce");
static_assert((kBrightnessStepIntervalMs >= kButtonPollIntervalMs) &&
                  ((kBrightnessStepIntervalMs % kButtonPollIntervalMs) == 0U),
              "Krok jasnosci musi byc wielokrotnoscia pollingu");
static_assert((kBrightnessRampTimeMs >= 3'500U) && (kBrightnessRampTimeMs <= 4'500U),
              "Pelna regulacja jasnosci powinna trwac okolo 4 s");
static_assert((kStatusLedBlinkPeriodMs > 0U) && ((kStatusLedBlinkPeriodMs % 2U) == 0U),
              "Okres LED statusu musi byc dodatni i parzysty");
static_assert(kTaskWatchdogTimeoutMs > kStatusLedBlinkPeriodMs,
              "Watchdog musi pozwolic na co najmniej jeden pelny okres LED statusu");
static_assert((kLcdI2cAddress << 1U) == kLcdI2cWriteAddress,
              "Adres 7-bit LCD musi odpowiadac adresowi 8-bit write");
static_assert(kSharedI2cPort == I2C_NUM_0, "Wspolna magistrala musi korzystac z I2C0");
static_assert(kLcdRecoveryRetryIntervalMs > kSharedI2cTimeoutMs,
              "Ponowienie LCD musi byc rzadsze od timeoutu I2C");
static_assert((kLcdInformationPageIntervalMs >= kLcdRefreshIntervalMs) &&
                  ((kLcdInformationPageIntervalMs % kLcdRefreshIntervalMs) == 0U),
              "Rotacja informacji LCD musi byc wielokrotnoscia odswiezania ekranu");
static_assert((kLcdMeasurementPageIntervalMs >= kLcdRefreshIntervalMs) &&
                  ((kLcdMeasurementPageIntervalMs % kLcdRefreshIntervalMs) == 0U),
              "Rotacja pomiarow LCD musi byc wielokrotnoscia odswiezania ekranu");
static_assert(kLcdPowerStabilizationMs >= 100U,
              "LCD musi miec czas na stabilizacje po powrocie zasilania");
static_assert((kLcdInitializationPassCount >= 2U) &&
                  (kLcdInitializationPassGapMs > kSharedI2cTimeoutMs),
              "Odzyskanie LCD wymaga co najmniej dwoch rozdzielonych przebiegow inicjalizacji");
static_assert((kLcdRecoveryFrameCount >= 2U) && (kLcdRecoveryFrameIntervalMs > kSharedI2cTimeoutMs),
              "Odzyskanie LCD wymaga co najmniej dwoch rozdzielonych pelnych ramek");
static_assert((kLcdColumns == 20U) && (kLcdRows == 4U), "Dashboard wymaga LCD 20x4");
static_assert((kPirContactDebounceMs >= kButtonPollIntervalMs) &&
                  ((kPirContactDebounceMs % kButtonPollIntervalMs) == 0U),
              "Filtr styku PIR musi byc wielokrotnoscia pollingu");
static_assert((kLcdDdramRowOffsets[0] == 0x00U) && (kLcdDdramRowOffsets[1] == 0x40U) &&
                  (kLcdDdramRowOffsets[2] == 0x14U) && (kLcdDdramRowOffsets[3] == 0x54U),
              "Mapa DDRAM musi odpowiadac standardowemu LCD 20x4");
static_assert((kLcdPcfRsMask | kLcdPcfRwMask | kLcdPcfEnableMask | kLcdPcfBacklightMask) == 0x0FU,
              "Bity sterujace PCF8574 musza zajmowac P0-P3");
#if LED_CONTROLLER_AT24C08_ENABLED
static_assert(kSettingsEepromSize == 1'024U, "AT24C08N musi udostepniac 1024 bajty");
static_assert((kSettingsEepromPageSize == 16U) &&
                  ((kSettingsEepromBlockSize % kSettingsEepromPageSize) == 0U),
              "Strony AT24C08N musza miec po 16 bajtow");
static_assert(kSettingsWriteCycleTimeoutUs >= (5 * kSettingsWritePollIntervalUs),
              "ACK polling EEPROM musi obejmowac co najmniej 5 ms cyklu zapisu");
static_assert((kSettingsEepromBaseAddress > kLcdI2cAddress) ||
                  ((kSettingsEepromBaseAddress + kSettingsEepromBlockCount) <= kLcdI2cAddress),
              "Adresy EEPROM i PCF8574AT nie moga sie nakladac");
#endif
#if LED_CONTROLLER_INA228_ENABLED
static_assert(kIna228I2cAddress != kLcdI2cAddress,
              "Adresy INA228 i PCF8574AT nie moga sie nakladac");
#if LED_CONTROLLER_AT24C08_ENABLED
static_assert((kIna228I2cAddress < kSettingsEepromBaseAddress) ||
                  (kIna228I2cAddress >= (kSettingsEepromBaseAddress + kSettingsEepromBlockCount)),
              "Adres INA228 nie moze kolidowac z blokami AT24C08N");
#endif
static_assert(kIna228SampleIntervalMs >= kLcdRefreshIntervalMs,
              "Telemetria INA228 nie musi byc probkowana czesciej niz LCD");
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
static_assert(kDs18b20DataPin == GPIO_NUM_4, "DS18B20 musi korzystac z GPIO4");
static_assert(kDs18b20ConversionTimeMs < kDs18b20SampleIntervalMs,
              "Konwersja DS18B20 musi zakonczyc sie przed kolejnym okresem pomiarowym");
static_assert(kDs18b20MaximumDeviceCount >= 2U,
              "Magistrala musi obsluzyc czujnik radiatora i obudowy");
#endif
static_assert(configuredPinsAreAllowed(), "Pinout uzywa GPIO z listy zakazanej");

} // namespace board_config
