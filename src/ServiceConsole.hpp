#pragma once

#include "MqttService.hpp"
#if LED_CONTROLLER_AT24C08_ENABLED
#include "SettingsService.hpp"
#endif
#include "WifiService.hpp"
#include "esp_console.h"
#include "esp_err.h"

#include <cstddef>

class LedController;

class ServiceConsole final {
  public:
    ServiceConsole(WifiService& wifi_service, MqttService& mqtt_service,
                   LedController& led_controller
#if LED_CONTROLLER_AT24C08_ENABLED
                   ,
                   SettingsService& settings_service
#endif
                   ) noexcept;

    esp_err_t initialize() noexcept;
    esp_err_t start() noexcept;

  private:
    static int commandEntry(void* context, int argc, char** argv) noexcept;
    static int mqttCommandEntry(void* context, int argc, char** argv) noexcept;
#if LED_CONTROLLER_AT24C08_ENABLED
    static int settingsCommandEntry(void* context, int argc, char** argv) noexcept;
#endif
#if LED_CONTROLLER_INA228_ENABLED
    static int ina228CommandEntry(void* context, int argc, char** argv) noexcept;
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    static int temperatureCommandEntry(void* context, int argc, char** argv) noexcept;
#endif
    int handleCommand(int argc, char** argv) noexcept;
    int handleMqttCommand(int argc, char** argv) noexcept;
#if LED_CONTROLLER_AT24C08_ENABLED
    int handleSettingsCommand(int argc, char** argv) noexcept;
    void printSettingsStatus() const noexcept;
    static void printSettingsHelp() noexcept;
#endif
#if LED_CONTROLLER_INA228_ENABLED
    int handleIna228Command(int argc, char** argv) noexcept;
    void printIna228Status() const noexcept;
    static void printIna228Help() noexcept;
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    int handleTemperatureCommand(int argc, char** argv) noexcept;
    void printTemperatureStatus() const noexcept;
    static void printTemperatureHelp() noexcept;
#endif
    int handleStaticIp(int argc, char** argv) noexcept;
    int handleDns(int argc, char** argv) noexcept;
    int handlePassword() noexcept;
    int handleMqttPassword() noexcept;
    int readSecret(char* output, std::size_t output_size, const char* prompt) noexcept;
    void printStatus() const noexcept;
    void printMqttStatus() const noexcept;
    static void printWifiHelp() noexcept;
    static void printMqttHelp() noexcept;
    static const char* optionValue(int argc, char** argv, const char* option) noexcept;
    static bool parseIp(const char* text, esp_ip4_addr_t& address) noexcept;
    static const char* stateName(WifiService::State state) noexcept;
    static const char* mqttStateName(MqttService::State state) noexcept;
    static void secureZero(void* data, std::size_t size) noexcept;

    WifiService& wifi_service_;
    MqttService& mqtt_service_;
    LedController& led_controller_;
#if LED_CONTROLLER_AT24C08_ENABLED
    SettingsService& settings_service_;
#endif
    esp_console_repl_t* repl_{nullptr};
    bool initialized_{false};
    bool started_{false};
};
