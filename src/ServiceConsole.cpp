#include "ServiceConsole.hpp"

#include "BoardConfig.hpp"
#include "DebugLog.hpp"
#include "DeviceIdentity.hpp"
#if LED_CONTROLLER_INA228_ENABLED
#include "Ina228Scaling.hpp"
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
#include "TemperatureModel.hpp"
#include "TemperatureTelemetry.hpp"
#endif
#include "LedController.hpp"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr char kTag[] = "service_console";
constexpr const char* kPrompt = device_identity::kConsolePrompt;
static_assert(CONFIG_ESP_CONSOLE_UART_NUM == 0, "Konsola serwisowa wymaga UART0");
#if LED_CONTROLLER_AT24C08_ENABLED
constexpr char kWifiCommandHelp[] = "Konfiguracja Wi-Fi z szyfrowanym zapisem w AT24C08N";
constexpr char kMqttCommandHelp[] = "Konfiguracja MQTT z szyfrowanym zapisem w AT24C08N";
constexpr char kSettingsCommandHelp[] = "Szyfrowane ustawienia w opcjonalnym AT24C08N";
constexpr char kSettingsCommandHint[] = "<help|status|save|reload|clear|ina-cal|web-password>";
#else
constexpr char kWifiCommandHelp[] = "Konfiguracja testowego Wi-Fi przechowywana tylko w RAM";
constexpr char kMqttCommandHelp[] = "Konfiguracja testowego MQTT przechowywana tylko w RAM";
#endif
constexpr char kWifiCommandHint[] =
    "<help|scan|ssid|password|mode|static|dns|apply|status|disconnect|clear>";
constexpr char kMqttCommandHint[] =
    "<help|uri|username|password|topic|discovery|apply|status|disconnect|clear>";
constexpr char kLogsCommandHelp[] = "Logi ESP_LOG na UART (bufory WWW/MQTT pozostaja aktywne)";
constexpr char kLogsCommandHint[] = "<status|on|off>";
#if LED_CONTROLLER_INA228_ENABLED
constexpr char kIna228CommandHelp[] = "Diagnostyka opcjonalnego monitora energii INA228";
constexpr char kIna228CommandHint[] = "<help|status>";
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
constexpr char kTemperatureCommandHelp[] = "Konfiguracja czujnikow DS18B20 na GPIO4";
constexpr char kTemperatureCommandHint[] = "<help|scan|status|assign|clear>";
#endif

void printResult(const char* const operation, const esp_err_t result) noexcept {
    if (result == ESP_OK) {
        std::printf("OK: %s\n", operation);
    } else {
        std::printf("BLAD: %s: %s\n", operation, esp_err_to_name(result));
    }
}

[[nodiscard]] const char* shownOrDash(const char* const text) noexcept {
    return ((text != nullptr) && (text[0] != '\0')) ? text : "-";
}

#if LED_CONTROLLER_DS18B20_ENABLED
[[nodiscard]] bool parseTemperatureRole(const char* const text, TemperatureRole& role) noexcept {
    if (text == nullptr) {
        return false;
    }
    if (std::strcmp(text, "radiator") == 0) {
        role = TemperatureRole::radiator;
        return true;
    }
    if ((std::strcmp(text, "obudowa") == 0) || (std::strcmp(text, "enclosure") == 0)) {
        role = TemperatureRole::enclosure;
        return true;
    }
    return false;
}

[[nodiscard]] const char* temperatureRoleName(const TemperatureRole role) noexcept {
    return (role == TemperatureRole::radiator) ? "radiator" : "obudowa";
}

void printTemperature(const std::int32_t temperature_millicelsius) noexcept {
    const std::int64_t temperature = temperature_millicelsius;
    const std::uint64_t magnitude =
        static_cast<std::uint64_t>((temperature < 0) ? -temperature : temperature);
    std::printf("%s%" PRIu64 ".%03" PRIu64 " C", (temperature < 0) ? "-" : "", magnitude / 1'000U,
                magnitude % 1'000U);
}

void printRom(const std::uint64_t rom_id) noexcept {
    std::array<std::uint8_t, temperature_model::kRomSize> bytes{};
    temperature_model::romToBytes(rom_id, bytes);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        std::printf("%02X%s", static_cast<unsigned>(bytes[index]),
                    (index + 1U < bytes.size()) ? "-" : "");
    }
}
#endif
} // namespace

ServiceConsole::ServiceConsole(WifiService& wifi_service, MqttService& mqtt_service,
                               LedController& led_controller
#if LED_CONTROLLER_AT24C08_ENABLED
                               ,
                               SettingsService& settings_service
#endif
                               ) noexcept
    : wifi_service_(wifi_service), mqtt_service_(mqtt_service), led_controller_(led_controller)
#if LED_CONTROLLER_AT24C08_ENABLED
      ,
      settings_service_(settings_service)
#endif
{
}

esp_err_t ServiceConsole::initialize() noexcept {
    if (initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = kPrompt;
    repl_config.max_cmdline_length = 256;
    repl_config.task_stack_size = 4'096;
    repl_config.task_priority = 2;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    uart_config.tx_gpio_num = static_cast<int>(board_config::kServiceConsoleTxPin);
    uart_config.rx_gpio_num = static_cast<int>(board_config::kServiceConsoleRxPin);
    esp_err_t result = esp_console_new_repl_uart(&uart_config, &repl_config, &repl_);
    if (result != ESP_OK) {
        return result;
    }
    ESP_LOGI(kTag, "Konsola UART0: TX=GPIO%d, RX=GPIO%d/VN (zewnetrzny pull-up 10 kOhm)",
             static_cast<int>(board_config::kServiceConsoleTxPin),
             static_cast<int>(board_config::kServiceConsoleRxPin));

    result = esp_console_register_help_command();
    if (result != ESP_OK) {
        return result;
    }

    esp_console_cmd_t wifi_command{};
    wifi_command.command = "wifi";
    wifi_command.help = kWifiCommandHelp;
    wifi_command.hint = kWifiCommandHint;
    wifi_command.func_w_context = commandEntry;
    wifi_command.context = this;
    result = esp_console_cmd_register(&wifi_command);
    if (result != ESP_OK) {
        return result;
    }

    esp_console_cmd_t mqtt_command{};
    mqtt_command.command = "mqtt";
    mqtt_command.help = kMqttCommandHelp;
    mqtt_command.hint = kMqttCommandHint;
    mqtt_command.func_w_context = mqttCommandEntry;
    mqtt_command.context = this;
    result = esp_console_cmd_register(&mqtt_command);
    if (result != ESP_OK) {
        return result;
    }

    esp_console_cmd_t logs_command{};
    logs_command.command = "logs";
    logs_command.help = kLogsCommandHelp;
    logs_command.hint = kLogsCommandHint;
    logs_command.func_w_context = logsCommandEntry;
    logs_command.context = this;
    result = esp_console_cmd_register(&logs_command);
    if (result != ESP_OK) {
        return result;
    }

#if LED_CONTROLLER_AT24C08_ENABLED
    esp_console_cmd_t settings_command{};
    settings_command.command = "settings";
    settings_command.help = kSettingsCommandHelp;
    settings_command.hint = kSettingsCommandHint;
    settings_command.func_w_context = settingsCommandEntry;
    settings_command.context = this;
    result = esp_console_cmd_register(&settings_command);
    if (result != ESP_OK) {
        return result;
    }
#endif

#if LED_CONTROLLER_INA228_ENABLED
    esp_console_cmd_t ina228_command{};
    ina228_command.command = "ina";
    ina228_command.help = kIna228CommandHelp;
    ina228_command.hint = kIna228CommandHint;
    ina228_command.func_w_context = ina228CommandEntry;
    ina228_command.context = this;
    result = esp_console_cmd_register(&ina228_command);
    if (result != ESP_OK) {
        return result;
    }
#endif

#if LED_CONTROLLER_DS18B20_ENABLED
    esp_console_cmd_t temperature_command{};
    temperature_command.command = "temp";
    temperature_command.help = kTemperatureCommandHelp;
    temperature_command.hint = kTemperatureCommandHint;
    temperature_command.func_w_context = temperatureCommandEntry;
    temperature_command.context = this;
    result = esp_console_cmd_register(&temperature_command);
    if (result != ESP_OK) {
        return result;
    }
#endif

    initialized_ = true;
    return ESP_OK;
}

esp_err_t ServiceConsole::start() noexcept {
    if (!initialized_ || started_ || (repl_ == nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    DebugLog::instance().setUartEnabled(false);
    const esp_err_t result = esp_console_start_repl(repl_);
    if (result == ESP_OK) {
        started_ = true;
        std::printf("Konsola serwisowa gotowa. Wpisz: wifi help, mqtt help, logs status");
#if LED_CONTROLLER_AT24C08_ENABLED
        std::printf(", settings help");
#endif
#if LED_CONTROLLER_INA228_ENABLED
        std::printf(", ina help");
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
        std::printf(", temp help");
#endif
        std::printf("\n");
        std::printf("Logi ESP_LOG na UART: OFF (logs on/off); WWW/MQTT bez zmian\n");
    } else {
        DebugLog::instance().setUartEnabled(true);
    }
    return result;
}

int ServiceConsole::logsCommandEntry(void* const, const int argc, char** const argv) noexcept {
    auto& logger = DebugLog::instance();
    if (argc == 2) {
        if (std::strcmp(argv[1], "on") == 0) {
            logger.setUartEnabled(true);
        } else if (std::strcmp(argv[1], "off") == 0) {
            logger.setUartEnabled(false);
        } else if (std::strcmp(argv[1], "status") != 0) {
            std::printf("Uzycie: logs <status|on|off>\n");
            return 1;
        }
        std::printf("Logi ESP_LOG na UART: %s\n", logger.uartEnabled() ? "ON" : "OFF");
        return 0;
    }
    if (argc == 1) {
        std::printf("Logi ESP_LOG na UART: %s\n", logger.uartEnabled() ? "ON" : "OFF");
        return 0;
    }
    std::printf("Uzycie: logs <status|on|off>\n");
    return 1;
}

int ServiceConsole::commandEntry(void* const context, const int argc, char** const argv) noexcept {
    auto* const console = static_cast<ServiceConsole*>(context);
#if LED_CONTROLLER_AT24C08_ENABLED
    if (console->mustChangeWebPassword())
        return printPasswordChangeRequired();
#endif
    return console->handleCommand(argc, argv);
}

int ServiceConsole::mqttCommandEntry(void* const context, const int argc,
                                     char** const argv) noexcept {
    auto* const console = static_cast<ServiceConsole*>(context);
#if LED_CONTROLLER_AT24C08_ENABLED
    if (console->mustChangeWebPassword())
        return printPasswordChangeRequired();
#endif
    return console->handleMqttCommand(argc, argv);
}

#if LED_CONTROLLER_AT24C08_ENABLED
int ServiceConsole::settingsCommandEntry(void* const context, const int argc,
                                         char** const argv) noexcept {
    auto* const console = static_cast<ServiceConsole*>(context);
    if (console->mustChangeWebPassword() &&
        !(argc == 2 &&
          (std::strcmp(argv[1], "web-password") == 0 || std::strcmp(argv[1], "status") == 0 ||
           std::strcmp(argv[1], "help") == 0)) &&
        !(argc == 3 && std::strcmp(argv[1], "clear") == 0))
        return printPasswordChangeRequired();
    return console->handleSettingsCommand(argc, argv);
}

bool ServiceConsole::mustChangeWebPassword() const noexcept {
    const auto state = settings_service_.status();
    return state.storage_ready && !state.web_password_set;
}

int ServiceConsole::printPasswordChangeRequired() noexcept {
    std::printf("Najpierw ustaw haslo administratora: settings web-password\n");
    return 1;
}
#endif

#if LED_CONTROLLER_INA228_ENABLED
int ServiceConsole::ina228CommandEntry(void* const context, const int argc,
                                       char** const argv) noexcept {
    auto* const console = static_cast<ServiceConsole*>(context);
#if LED_CONTROLLER_AT24C08_ENABLED
    if (console->mustChangeWebPassword())
        return printPasswordChangeRequired();
#endif
    return console->handleIna228Command(argc, argv);
}
#endif

#if LED_CONTROLLER_DS18B20_ENABLED
int ServiceConsole::temperatureCommandEntry(void* const context, const int argc,
                                            char** const argv) noexcept {
    auto* const console = static_cast<ServiceConsole*>(context);
#if LED_CONTROLLER_AT24C08_ENABLED
    if (console->mustChangeWebPassword())
        return printPasswordChangeRequired();
#endif
    return console->handleTemperatureCommand(argc, argv);
}
#endif

int ServiceConsole::handleCommand(const int argc, char** const argv) noexcept {
    if ((argc < 2) || (std::strcmp(argv[1], "help") == 0)) {
        printWifiHelp();
        return 0;
    }

    const char* const command = argv[1];
    esp_err_t result = ESP_OK;

    if (std::strcmp(command, "scan") == 0) {
        result = wifi_service_.scan();
        printResult("skan uruchomiony; wyniki pojawia sie asynchronicznie", result);
        return (result == ESP_OK) ? 0 : 1;
    }

    if (std::strcmp(command, "ssid") == 0) {
        if (argc != 3) {
            std::printf("Uzycie: wifi ssid \"Nazwa sieci\"\n");
            return 1;
        }
        result = wifi_service_.setSsid(argv[2]);
        printResult("SSID ustawione w RAM", result);
        return (result == ESP_OK) ? 0 : 1;
    }

    if (std::strcmp(command, "password") == 0) {
        if (argc != 2) {
            std::printf("Uzycie: wifi password\n");
            return 1;
        }
        return handlePassword();
    }

    if ((std::strcmp(command, "dhcp") == 0) ||
        ((std::strcmp(command, "mode") == 0) && (argc == 3) &&
         (std::strcmp(argv[2], "dhcp") == 0))) {
        result = wifi_service_.useDhcp();
        printResult("tryb IPv4 DHCP", result);
        return (result == ESP_OK) ? 0 : 1;
    }

    if (std::strcmp(command, "static") == 0) {
        return handleStaticIp(argc, argv);
    }

    if (std::strcmp(command, "dns") == 0) {
        return handleDns(argc, argv);
    }

    if (std::strcmp(command, "apply") == 0) {
        result = wifi_service_.apply();
        if (result == ESP_ERR_INVALID_ARG) {
            std::printf("BLAD: ustaw SSID, poprawne haslo i komplet danych statycznych\n");
        } else {
            printResult("konfiguracja zastosowana; laczenie rozpoczete", result);
        }
#if LED_CONTROLLER_AT24C08_ENABLED
        if (result == ESP_OK) {
            settings_service_.notifyConfigurationChanged();
        }
#endif
        return (result == ESP_OK) ? 0 : 1;
    }

    if (std::strcmp(command, "status") == 0) {
        printStatus();
        return 0;
    }

    if (std::strcmp(command, "disconnect") == 0) {
        result = wifi_service_.disconnect();
        printResult("Wi-Fi rozlaczone; reconnect wylaczony", result);
        return (result == ESP_OK) ? 0 : 1;
    }

    if (std::strcmp(command, "clear") == 0) {
        result = wifi_service_.clear();
        printResult("konfiguracja Wi-Fi wyczyszczona z RAM", result);
#if LED_CONTROLLER_AT24C08_ENABLED
        if (result == ESP_OK) {
            settings_service_.notifyConfigurationChanged();
        }
#endif
        return (result == ESP_OK) ? 0 : 1;
    }

    std::printf("Nieznane polecenie Wi-Fi: %s\n", command);
    printWifiHelp();
    return 1;
}

int ServiceConsole::handleMqttCommand(const int argc, char** const argv) noexcept {
    if ((argc < 2) || (std::strcmp(argv[1], "help") == 0)) {
        printMqttHelp();
        return 0;
    }

    const char* const command = argv[1];
    esp_err_t result = ESP_OK;

    if (std::strcmp(command, "uri") == 0) {
        if (argc != 3) {
            std::printf("Uzycie: mqtt uri mqtt://A.B.C.D:1883\n");
            return 1;
        }
        result = mqtt_service_.setUri(argv[2]);
        if (result == ESP_ERR_INVALID_ARG) {
            std::printf("BLAD: etap testowy obsluguje URI mqtt:// bez danych logowania\n");
        } else {
            printResult("URI brokera ustawione w RAM", result);
        }
        return (result == ESP_OK) ? 0 : 1;
    }

    if (std::strcmp(command, "username") == 0) {
        if (argc != 3) {
            std::printf("Uzycie: mqtt username \"Nazwa uzytkownika\"\n");
            return 1;
        }
        result = mqtt_service_.setUsername(argv[2]);
        printResult("uzytkownik MQTT ustawiony w RAM", result);
        return (result == ESP_OK) ? 0 : 1;
    }

    if (std::strcmp(command, "password") == 0) {
        if (argc != 2) {
            std::printf("Uzycie: mqtt password\n");
            return 1;
        }
        return handleMqttPassword();
    }

    if (std::strcmp(command, "topic") == 0) {
        if (argc != 3) {
            std::printf("Uzycie: mqtt topic \"%s\"\n", device_identity::kTechnicalSlug);
            return 1;
        }
        result = mqtt_service_.setTopicPrefix(argv[2]);
        printResult("temat bazowy MQTT ustawiony w RAM", result);
        return (result == ESP_OK) ? 0 : 1;
    }

    if (std::strcmp(command, "discovery") == 0) {
        if (argc != 3) {
            std::printf("Uzycie: mqtt discovery homeassistant\n");
            return 1;
        }
        result = mqtt_service_.setDiscoveryPrefix(argv[2]);
        printResult("prefix discovery ustawiony w RAM", result);
        return (result == ESP_OK) ? 0 : 1;
    }

    if (std::strcmp(command, "apply") == 0) {
        result = mqtt_service_.apply();
        if (result == ESP_ERR_INVALID_ARG) {
            std::printf("BLAD: ustaw URI, uzytkownika i temat bazowy\n");
        } else {
            printResult("konfiguracja MQTT zastosowana; laczenie rozpoczete", result);
        }
#if LED_CONTROLLER_AT24C08_ENABLED
        if (result == ESP_OK) {
            settings_service_.notifyConfigurationChanged();
        }
#endif
        return (result == ESP_OK) ? 0 : 1;
    }

    if (std::strcmp(command, "status") == 0) {
        printMqttStatus();
        return 0;
    }

    if (std::strcmp(command, "disconnect") == 0) {
        result = mqtt_service_.disconnect();
        printResult("MQTT rozlaczone; reconnect zatrzymany", result);
        return (result == ESP_OK) ? 0 : 1;
    }

    if (std::strcmp(command, "clear") == 0) {
        result = mqtt_service_.clear();
        printResult("konfiguracja MQTT wyczyszczona z RAM", result);
#if LED_CONTROLLER_AT24C08_ENABLED
        if (result == ESP_OK) {
            settings_service_.notifyConfigurationChanged();
        }
#endif
        return (result == ESP_OK) ? 0 : 1;
    }

    std::printf("Nieznane polecenie MQTT: %s\n", command);
    printMqttHelp();
    return 1;
}

#if LED_CONTROLLER_AT24C08_ENABLED
int ServiceConsole::handleSettingsCommand(const int argc, char** const argv) noexcept {
    if ((argc < 2) || (std::strcmp(argv[1], "help") == 0)) {
        printSettingsHelp();
        return 0;
    }

    if ((argc == 2) && (std::strcmp(argv[1], "status") == 0)) {
        printSettingsStatus();
        return 0;
    }
    if (std::strcmp(argv[1], "web-password") == 0) {
        if (argc != 2) {
            std::printf("Uzycie: settings web-password (haslo podaj w ukrytym monicie)\n");
            return 1;
        }
        std::array<char, 65> password{};
        if (readSecret(password.data(), password.size(), "Nowe haslo WWW (12-64 znaki ASCII): ") !=
            0)
            return 1;
        std::array<char, 65> repeated{};
        if (readSecret(repeated.data(), repeated.size(), "Powtorz nowe haslo WWW: ") != 0) {
            secureZero(password.data(), password.size());
            return 1;
        }
        if (std::strcmp(password.data(), repeated.data()) != 0) {
            secureZero(password.data(), password.size());
            secureZero(repeated.data(), repeated.size());
            std::printf("BLAD: hasla roznia sie\n");
            return 1;
        }
        const esp_err_t result = settings_service_.setWebPassword(password.data());
        secureZero(password.data(), password.size());
        secureZero(repeated.data(), repeated.size());
        printResult("nowe haslo WWW przyjete; potwierdz zapis przez settings status", result);
        return (result == ESP_OK) ? 0 : 1;
    }
    if ((argc == 2) && (std::strcmp(argv[1], "save") == 0)) {
        settings_service_.requestSave();
        std::printf("OK: zapis ustawien zlecony; wynik sprawdz przez settings status\n");
        return 0;
    }
    if ((argc == 2) && (std::strcmp(argv[1], "reload") == 0)) {
        settings_service_.requestReload();
        std::printf("OK: ponowny odczyt AT24C08N zlecony\n");
        return 0;
    }
    if (std::strcmp(argv[1], "clear") == 0) {
        if ((argc != 3) || (std::strcmp(argv[2], "CONFIRM") != 0)) {
            std::printf("Uzycie: settings clear CONFIRM\n");
            return 1;
        }
        const esp_err_t result = settings_service_.requestClear();
        printResult("Wi-Fi, MQTT, kalibracja INA i przypisania temperatury wyczyszczone; zapis "
                    "pustego rekordu zlecony",
                    result);
        return (result == ESP_OK) ? 0 : 1;
    }
    if (std::strcmp(argv[1], "ina-cal") == 0) {
        if (argc != 3) {
            std::printf("Uzycie: settings ina-cal <-100000..100000>\n");
            return 1;
        }
        errno = 0;
        char* end = nullptr;
        const long parsed = std::strtol(argv[2], &end, 10);
        if ((errno != 0) || (end == argv[2]) || (end == nullptr) || (*end != '\0') ||
            (parsed < persistent_settings::kMinimumInaCurrentCorrectionPpm) ||
            (parsed > persistent_settings::kMaximumInaCurrentCorrectionPpm)) {
            std::printf("BLAD: korekcja musi byc w zakresie -100000..100000 ppm\n");
            return 1;
        }
        const esp_err_t result =
            settings_service_.setInaCurrentCorrectionPpm(static_cast<std::int32_t>(parsed));
        printResult("korekcja pradu INA ustawiona; zapis nastapi automatycznie", result);
        return (result == ESP_OK) ? 0 : 1;
    }

    std::printf("Nieznane polecenie settings: %s\n", argv[1]);
    printSettingsHelp();
    return 1;
}
#endif

#if LED_CONTROLLER_INA228_ENABLED
int ServiceConsole::handleIna228Command(const int argc, char** const argv) noexcept {
    if ((argc < 2) || (std::strcmp(argv[1], "help") == 0)) {
        printIna228Help();
        return 0;
    }
    if ((argc == 2) && (std::strcmp(argv[1], "status") == 0)) {
        printIna228Status();
        return 0;
    }

    std::printf("Nieznane polecenie INA228\n");
    printIna228Help();
    return 1;
}
#endif

#if LED_CONTROLLER_DS18B20_ENABLED
int ServiceConsole::handleTemperatureCommand(const int argc, char** const argv) noexcept {
    if ((argc < 2) || (std::strcmp(argv[1], "help") == 0)) {
        printTemperatureHelp();
        return 0;
    }
    if ((argc == 2) && (std::strcmp(argv[1], "scan") == 0)) {
        led_controller_.requestTemperatureScan();
        std::printf("OK: skan 1-Wire zlecony; wynik sprawdz przez temp status\n");
        return 0;
    }
    if ((argc == 2) && (std::strcmp(argv[1], "status") == 0)) {
        printTemperatureStatus();
        return 0;
    }
    if (std::strcmp(argv[1], "assign") == 0) {
        if (argc != 4) {
            std::printf("Uzycie: temp assign <radiator|obudowa> <numer ze skanu>\n");
            return 1;
        }
        TemperatureRole role = TemperatureRole::radiator;
        if (!parseTemperatureRole(argv[2], role)) {
            std::printf("BLAD: rola to radiator albo obudowa\n");
            return 1;
        }
        errno = 0;
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(argv[3], &end, 10);
        if ((errno != 0) || (end == argv[3]) || (end == nullptr) || (*end != '\0') ||
            (parsed == 0U) || (parsed > board_config::kDs18b20MaximumDeviceCount)) {
            std::printf("BLAD: podaj numer czujnika pokazany przez temp status\n");
            return 1;
        }
        const esp_err_t result =
            led_controller_.assignTemperatureRole(role, static_cast<std::size_t>(parsed));
        if (result == ESP_ERR_INVALID_STATE) {
            std::printf("BLAD: ten czujnik jest juz przypisany do drugiej roli\n");
            return 1;
        }
        if (result == ESP_ERR_NOT_FOUND) {
            std::printf("BLAD: brak takiego numeru; wykonaj temp scan i temp status\n");
            return 1;
        }
        printResult("rola czujnika ustawiona", result);
#if LED_CONTROLLER_AT24C08_ENABLED
        if (result == ESP_OK) {
            settings_service_.notifyConfigurationChanged();
        }
#endif
        return (result == ESP_OK) ? 0 : 1;
    }
    if (std::strcmp(argv[1], "clear") == 0) {
        if (argc != 3) {
            std::printf("Uzycie: temp clear <radiator|obudowa|all>\n");
            return 1;
        }
        esp_err_t result = ESP_OK;
        if (std::strcmp(argv[2], "all") == 0) {
            result = led_controller_.clearTemperatureAssignments();
        } else {
            TemperatureRole role = TemperatureRole::radiator;
            if (!parseTemperatureRole(argv[2], role)) {
                std::printf("BLAD: rola to radiator, obudowa albo all\n");
                return 1;
            }
            result = led_controller_.clearTemperatureRole(role);
        }
        printResult("przypisanie czujnika wyczyszczone", result);
#if LED_CONTROLLER_AT24C08_ENABLED
        if (result == ESP_OK) {
            settings_service_.notifyConfigurationChanged();
        }
#endif
        return (result == ESP_OK) ? 0 : 1;
    }

    std::printf("Nieznane polecenie temperatury\n");
    printTemperatureHelp();
    return 1;
}
#endif

int ServiceConsole::handleStaticIp(const int argc, char** const argv) noexcept {
    if (argc != 8) {
        std::printf("Uzycie: wifi static --ip A.B.C.D --mask A.B.C.D --gw A.B.C.D\n");
        return 1;
    }

    const char* const ip_text = optionValue(argc, argv, "--ip");
    const char* const mask_text = optionValue(argc, argv, "--mask");
    const char* const gateway_text = optionValue(argc, argv, "--gw");
    esp_ip4_addr_t ip{};
    esp_ip4_addr_t mask{};
    esp_ip4_addr_t gateway{};
    if (!parseIp(ip_text, ip) || !parseIp(mask_text, mask) || !parseIp(gateway_text, gateway)) {
        std::printf("BLAD: niepoprawny adres IPv4, maska albo brama\n");
        return 1;
    }

    const esp_err_t result = wifi_service_.useStatic(ip, mask, gateway);
    printResult("statyczne IPv4 ustawione w RAM", result);
    return (result == ESP_OK) ? 0 : 1;
}

int ServiceConsole::handleDns(const int argc, char** const argv) noexcept {
    if ((argc != 4) && (argc != 6)) {
        std::printf("Uzycie: wifi dns --primary A.B.C.D [--secondary A.B.C.D]\n");
        return 1;
    }

    const char* const primary_text = optionValue(argc, argv, "--primary");
    const char* const secondary_text = optionValue(argc, argv, "--secondary");
    esp_ip4_addr_t primary{};
    esp_ip4_addr_t secondary{};
    if (!parseIp(primary_text, primary) ||
        ((secondary_text != nullptr) && !parseIp(secondary_text, secondary))) {
        std::printf("BLAD: niepoprawny adres serwera DNS\n");
        return 1;
    }

    const esp_err_t result = wifi_service_.setDns(primary, secondary);
    printResult("DNS ustawiony w RAM", result);
    return (result == ESP_OK) ? 0 : 1;
}

int ServiceConsole::handlePassword() noexcept {
    std::array<char, WifiService::kMaxPasswordLength + 1U> password{};
    if (readSecret(password.data(), password.size(), "Haslo Wi-Fi (puste dla sieci otwartej): ") !=
        0) {
        return 1;
    }

    const esp_err_t result = wifi_service_.setPassword(password.data());
    secureZero(password.data(), password.size());
    if (result == ESP_ERR_INVALID_ARG) {
        std::printf("BLAD: haslo musi byc puste albo miec 8-%u bajtow\n",
                    static_cast<unsigned>(WifiService::kMaxPasswordLength));
        return 1;
    }
    printResult("haslo ustawione w RAM i nie zostalo wyswietlone", result);
    return (result == ESP_OK) ? 0 : 1;
}

int ServiceConsole::handleMqttPassword() noexcept {
    std::array<char, MqttService::kMaxPasswordLength + 1U> password{};
    if (readSecret(password.data(), password.size(), "Haslo MQTT: ") != 0) {
        return 1;
    }

    const esp_err_t result = mqtt_service_.setPassword(password.data());
    secureZero(password.data(), password.size());
    printResult("haslo MQTT ustawione w RAM i nie zostalo wyswietlone", result);
    return (result == ESP_OK) ? 0 : 1;
}

int ServiceConsole::readSecret(char* const output, const std::size_t output_size,
                               const char* const prompt) noexcept {
    if ((output == nullptr) || (output_size < 2U) || (prompt == nullptr)) {
        return 1;
    }

    std::size_t length = 0;
    bool too_long = false;
    output[0] = '\0';
    std::printf("%s", prompt);
    std::fflush(stdout);

    for (;;) {
        std::uint8_t character = 0;
        const int read = uart_read_bytes(static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM),
                                         &character, 1, portMAX_DELAY);
        if (read != 1) {
            secureZero(output, output_size);
            std::printf("\nBLAD: odczyt UART\n");
            return 1;
        }

        if ((character == '\r') || (character == '\n')) {
            break;
        }
        if (character == 0x03U) {
            secureZero(output, output_size);
            std::printf("\nAnulowano\n");
            return 1;
        }
        if ((character == 0x08U) || (character == 0x7FU)) {
            if (length > 0U) {
                --length;
                output[length] = '\0';
            }
            continue;
        }
        if (character < 0x20U) {
            continue;
        }
        if (length >= (output_size - 1U)) {
            too_long = true;
            continue;
        }
        output[length] = static_cast<char>(character);
        ++length;
        output[length] = '\0';
    }

    std::printf("\n");
    if (too_long) {
        secureZero(output, output_size);
        std::printf("BLAD: sekret moze miec najwyzej %u bajtow\n",
                    static_cast<unsigned>(output_size - 1U));
        return 1;
    }
    return 0;
}

void ServiceConsole::printStatus() const noexcept {
    const WifiService::Status status = wifi_service_.status();
    std::printf("Stan: %s\n", stateName(status.state));
    std::printf("SSID: %s\n", shownOrDash(status.ssid.data()));
    std::printf("Haslo: %s\n", status.password_set ? "ustawione" : "brak / siec otwarta");
    std::printf("IPv4 mode: %s\n", status.dhcp ? "DHCP" : "statyczny");
    std::printf("IPv4: %s\n", shownOrDash(status.ip.data()));
    std::printf("Maska: %s\n", shownOrDash(status.netmask.data()));
    std::printf("Brama: %s\n", shownOrDash(status.gateway.data()));
    std::printf("DNS1: %s\n", shownOrDash(status.dns_primary.data()));
    std::printf("DNS2: %s\n", shownOrDash(status.dns_secondary.data()));
    if (status.state == WifiService::State::connected) {
        std::printf("RSSI: %d dBm\n", static_cast<int>(status.rssi_dbm));
    } else {
        std::printf("RSSI: -\n");
    }
    std::printf("NTP: %s\n",
                status.time_synchronized ? status.local_time.data() : "brak synchronizacji");
    std::printf("NTP server: pool.ntp.org\n");
    std::printf("Strefa: Europe/Warsaw (CET/CEST wg daty)\n");
    std::printf("Zmiany oczekujace na apply: %s\n", status.pending_changes ? "tak" : "nie");
#if LED_CONTROLLER_AT24C08_ENABLED
    const SettingsService::Status storage = settings_service_.status();
    std::printf("Storage: AT24C08N AES-GCM (%s)\n",
                storage.eeprom_present ? "aktywny" : "oczekiwanie na uklad");
#else
    std::printf("Storage: RAM only\n");
#endif
    if (status.disconnect_reason != 0U) {
        std::printf("Ostatni powod rozlaczenia: %u\n",
                    static_cast<unsigned>(status.disconnect_reason));
    }
}

void ServiceConsole::printMqttStatus() const noexcept {
    const MqttService::Status status = mqtt_service_.status();
    std::printf("Stan: %s\n", mqttStateName(status.state));
    std::printf("Broker URI: %s\n", shownOrDash(status.uri.data()));
    std::printf("Uzytkownik: %s\n", shownOrDash(status.username.data()));
    std::printf("Haslo: %s\n", status.password_set ? "ustawione" : "brak");
    std::printf("Topic prefix: %s\n", shownOrDash(status.topic_prefix.data()));
    std::printf("Discovery prefix: %s\n", shownOrDash(status.discovery_prefix.data()));
    std::printf("Device ID: %s\n", shownOrDash(status.device_id.data()));
    std::printf("Polaczenia: %" PRIu32 "\n", status.connect_count);
    std::printf("Komendy przyjete/odrzucone: %" PRIu32 "/%" PRIu32 "\n", status.accepted_commands,
                status.rejected_commands);
    std::printf("Zalegle publikacje: %s\n", status.publish_backlog ? "tak" : "nie");
    std::printf("Zmiany oczekujace na apply: %s\n", status.pending_changes ? "tak" : "nie");
#if LED_CONTROLLER_AT24C08_ENABLED
    const SettingsService::Status storage = settings_service_.status();
    std::printf("Storage: AT24C08N AES-GCM (%s)\n",
                storage.eeprom_present ? "aktywny" : "oczekiwanie na uklad");
#else
    std::printf("Storage: RAM only\n");
#endif
    if ((status.last_error_type != 0) || (status.last_connect_return_code != 0)) {
        std::printf("Ostatni blad MQTT: type=%" PRId32 ", connect=%" PRId32 "\n",
                    status.last_error_type, status.last_connect_return_code);
    }
}

#if LED_CONTROLLER_AT24C08_ENABLED
void ServiceConsole::printSettingsStatus() const noexcept {
    const SettingsService::Status status = settings_service_.status();
    std::printf("AT24C08N: %s\n", status.eeprom_present ? "wykryty" : "nieobecny");
    std::printf("Klucz AES-256 w NVS: %s\n", status.key_ready ? "gotowy" : "blad");
    std::printf("Klucz utworzony przy tym starcie: %s\n", status.key_created ? "tak" : "nie");
    std::printf("Ochrona klucza Flash Encryption: %s\n",
                status.key_protected_by_flash_encryption ? "wlaczona" : "wylaczona");
    std::printf("Rekord: %s", status.record_found ? "poprawny" : "brak");
    if (status.record_found) {
        std::printf(", sekwencja %" PRIu32 ", payload %u B", status.active_sequence,
                    static_cast<unsigned>(status.payload_size));
    }
    std::printf("\n");
    std::printf("Konfiguracja z rekordu zastosowana: %s\n", status.record_applied ? "tak" : "nie");
    std::printf("Zapis: %s\n", status.save_in_progress
                                   ? "w toku"
                                   : (status.save_pending ? "oczekuje" : "bez zmian"));
    std::printf("Korekcja pradu INA: %" PRId32 " ppm\n", status.ina_current_correction_ppm);
    std::printf("Przypisane role DS18B20: %u/2\n",
                static_cast<unsigned>(status.temperature_assignments));
    std::printf("Jasnosc i stany LED: nie sa zapisywane\n");
    std::printf("Haslo WWW: %s\n", status.web_password_set ? "ustawione" : "wymaga zmiany z admin");
    std::printf("Ostatni wynik: %s\n", esp_err_to_name(status.last_error));
}
#endif

#if LED_CONTROLLER_DS18B20_ENABLED
void ServiceConsole::printTemperatureStatus() const noexcept {
    TemperatureTelemetry telemetry{};
    if (!led_controller_.tryGetTemperatureTelemetry(telemetry)) {
        std::printf("DS18B20: status chwilowo zajety\n");
        return;
    }

    std::printf("GPIO: 4, pull-up: zewnetrzny 4,7 kOhm do 3,3 V\n");
    std::printf("Magistrala: %s\n", telemetry.bus_ready ? "gotowa" : "niedostepna");
    std::printf("Skan: %s\n", telemetry.scan_pending ? "oczekuje / trwa" : "zakonczony");
    std::printf("Wykryte czujniki: %u\n", static_cast<unsigned>(telemetry.sensor_count));
    for (std::size_t index = 0; index < telemetry.sensor_count; ++index) {
        const TemperatureSensorTelemetry& sensor = telemetry.sensors[index];
        std::printf("  [%u] ROM ", static_cast<unsigned>(index + 1U));
        printRom(sensor.rom_id);
        std::printf("  ");
        if (sensor.valid) {
            printTemperature(sensor.temperature_millicelsius);
        } else {
            std::printf("pomiar niedostepny");
        }
        const char* separator = "  rola: ";
        for (std::size_t role = 0; role < telemetry.roles.size(); ++role) {
            if (telemetry.roles[role].configured &&
                (telemetry.roles[role].rom_id == sensor.rom_id)) {
                std::printf("%s%s", separator,
                            temperatureRoleName(static_cast<TemperatureRole>(role)));
                separator = ", ";
            }
        }
        std::printf("\n");
    }

    for (std::size_t role = 0; role < telemetry.roles.size(); ++role) {
        const TemperatureRoleTelemetry& assignment = telemetry.roles[role];
        std::printf("%s: ", temperatureRoleName(static_cast<TemperatureRole>(role)));
        if (!assignment.configured) {
            std::printf("nieprzypisany\n");
        } else if (assignment.present && assignment.valid) {
            printTemperature(assignment.temperature_millicelsius);
            std::printf("\n");
        } else {
            std::printf("przypisany, czujnik nieobecny lub pomiar bledny\n");
        }
    }
    if (telemetry.last_error != ESP_OK) {
        std::printf("Ostatni wynik: %s\n",
                    esp_err_to_name(static_cast<esp_err_t>(telemetry.last_error)));
    }
#if LED_CONTROLLER_AT24C08_ENABLED
    const SettingsService::Status storage = settings_service_.status();
    std::printf("Przypisania: AT24C08N AES-GCM (%s)\n",
                storage.eeprom_present ? "aktywny" : "oczekiwanie na uklad; teraz RAM");
#else
    std::printf("Przypisania: RAM only\n");
#endif
}
#endif

#if LED_CONTROLLER_INA228_ENABLED
void ServiceConsole::printIna228Status() const noexcept {
    Ina228Telemetry telemetry{};
    if (!led_controller_.tryGetIna228Telemetry(telemetry)) {
        std::printf("INA228: status chwilowo zajety\n");
        return;
    }

    std::printf("Adres I2C: 0x40 (A0=A1=GND)\n");
    std::printf("Bocznik nominalny: 10 mOhm\n");
    std::printf("CURRENT_LSB: %" PRIu32 " uA\n", ina228_scaling::kCurrentLsbMicroamps);
    std::printf("SHUNT_CAL: %u\n", static_cast<unsigned>(ina228_scaling::kNominalShuntCalibration));
    std::printf("Uklad: %s\n", telemetry.present ? "wykryty" : "nieobecny");
    std::printf("Pomiar: %s\n", telemetry.valid ? "poprawny" : "niedostepny");
    if (telemetry.last_error != ESP_OK) {
        std::printf("Ostatni wynik: %s\n",
                    esp_err_to_name(static_cast<esp_err_t>(telemetry.last_error)));
    }
    if (!telemetry.valid) {
        return;
    }

    std::printf("Device ID: 0x%04X\n", static_cast<unsigned>(telemetry.device_id));
    std::printf("Prad: %" PRId32 " mA\n", telemetry.current_milliamps);
    std::printf("Napiecie szyny: %" PRIu32 " mV\n", telemetry.bus_voltage_millivolts);
    std::printf("Spadek na boczniku: %" PRId32 " uV\n", telemetry.shunt_microvolts);
    std::printf("Moc: %" PRIu32 " mW\n", telemetry.power_milliwatts);
    std::printf("Energia: %" PRIu32 ".%03" PRIu32 " Wh\n",
                telemetry.energy_milliwatt_hours / 1'000U,
                telemetry.energy_milliwatt_hours % 1'000U);
    const std::int64_t temperature = telemetry.die_temperature_millicelsius;
    const std::uint64_t magnitude =
        static_cast<std::uint64_t>((temperature < 0) ? -temperature : temperature);
    std::printf("Temperatura ukladu: %s%" PRIu64 ".%03" PRIu64 " C\n", (temperature < 0) ? "-" : "",
                magnitude / 1'000U, magnitude % 1'000U);
    std::printf("DIAG_ALRT: 0x%04X\n", static_cast<unsigned>(telemetry.diagnostic_flags));
}
#endif

#if LED_CONTROLLER_DS18B20_ENABLED
void ServiceConsole::printTemperatureHelp() noexcept {
    std::printf("Polecenia:\n");
    std::printf("  temp scan\n");
    std::printf("  temp status\n");
    std::printf("  temp assign radiator <numer>\n");
    std::printf("  temp assign obudowa <numer>\n");
    std::printf("  temp clear <radiator|obudowa|all>\n");
    std::printf("Numer pochodzi z temp status; adresu ROM nie trzeba przepisywac.\n");
}
#endif

void ServiceConsole::printWifiHelp() noexcept {
    std::printf("Polecenia:\n");
    std::printf("  wifi scan\n");
    std::printf("  wifi ssid \"Nazwa sieci\"\n");
    std::printf("  wifi password\n");
    std::printf("  wifi mode dhcp\n");
    std::printf("  wifi static --ip A.B.C.D --mask A.B.C.D --gw A.B.C.D\n");
    std::printf("  wifi dns --primary A.B.C.D [--secondary A.B.C.D]\n");
    std::printf("  wifi apply\n");
    std::printf("  wifi status\n");
    std::printf("  wifi disconnect\n");
    std::printf("  wifi clear\n");
}

void ServiceConsole::printMqttHelp() noexcept {
    std::printf("Polecenia:\n");
    std::printf("  mqtt uri mqtt://A.B.C.D:1883\n");
    std::printf("  mqtt username \"Nazwa uzytkownika\"\n");
    std::printf("  mqtt password\n");
    std::printf("  mqtt topic \"%s\"\n", device_identity::kTechnicalSlug);
    std::printf("  mqtt discovery homeassistant\n");
    std::printf("  mqtt apply\n");
    std::printf("  mqtt status\n");
    std::printf("  mqtt disconnect\n");
    std::printf("  mqtt clear\n");
}

#if LED_CONTROLLER_AT24C08_ENABLED
void ServiceConsole::printSettingsHelp() noexcept {
    std::printf("Polecenia ustawien:\n");
    std::printf("  settings status\n");
    std::printf("  settings save\n");
    std::printf("  settings reload\n");
    std::printf("  settings clear CONFIRM\n");
    std::printf("  settings ina-cal <-100000..100000>\n");
    std::printf("  settings web-password\n");
    std::printf("Po poprawnym wifi apply lub mqtt apply zapis nastepuje po 5 s.\n");
    std::printf("Jasnosc i stany kanalow LED nigdy nie trafiaja do EEPROM-u.\n");
}
#endif

#if LED_CONTROLLER_INA228_ENABLED
void ServiceConsole::printIna228Help() noexcept {
    std::printf("Polecenia:\n");
    std::printf("  ina status\n");
#if LED_CONTROLLER_AT24C08_ENABLED
    std::printf("Korekcja: settings ina-cal <-100000..100000>.\n");
#else
    std::printf("Kalibracja uzywa stalej nominalnej; trwaly zapis korekty jest wylaczony.\n");
#endif
}
#endif

const char* ServiceConsole::optionValue(const int argc, char** const argv,
                                        const char* const option) noexcept {
    for (int index = 2; index + 1 < argc; index += 2) {
        if (std::strcmp(argv[index], option) == 0) {
            return argv[index + 1];
        }
    }
    return nullptr;
}

bool ServiceConsole::parseIp(const char* const text, esp_ip4_addr_t& address) noexcept {
    return (text != nullptr) && (esp_netif_str_to_ip4(text, &address) == ESP_OK);
}

const char* ServiceConsole::stateName(const WifiService::State state) noexcept {
    switch (state) {
    case WifiService::State::unavailable:
        return "niedostepne";
    case WifiService::State::unconfigured:
        return "brak konfiguracji";
    case WifiService::State::ready:
        return "konfiguracja w RAM, przed apply";
    case WifiService::State::connecting:
        return "laczenie";
    case WifiService::State::connected:
        return "polaczone";
    case WifiService::State::disconnected:
        return "rozlaczone";
    }
    return "nieznany";
}

const char* ServiceConsole::mqttStateName(const MqttService::State state) noexcept {
    switch (state) {
    case MqttService::State::unavailable:
        return "niedostepne";
    case MqttService::State::unconfigured:
        return "brak konfiguracji";
    case MqttService::State::ready:
        return "konfiguracja w RAM, przed apply";
    case MqttService::State::connecting:
        return "laczenie";
    case MqttService::State::connected:
        return "polaczone";
    case MqttService::State::disconnected:
        return "rozlaczone / reconnect";
    case MqttService::State::authentication_error:
        return "blad uwierzytelnienia";
    }
    return "nieznany";
}

void ServiceConsole::secureZero(void* const data, const std::size_t size) noexcept {
    auto* output = static_cast<volatile std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        output[index] = 0;
    }
}
