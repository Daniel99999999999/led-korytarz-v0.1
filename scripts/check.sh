#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if ! command -v pio >/dev/null 2>&1; then
    echo "ERROR: Nie znaleziono polecenia 'pio'. Zainstaluj PlatformIO Core lub uruchom skrypt w terminalu PlatformIO." >&2
    exit 127
fi

if grep -ERq 'ledc_set_duty_and_update|ledc_fade_func_install|ledc_set_fade' src; then
    echo "ERROR: Kod kanałów PWM nie może używać API zależnego od usługi fade LEDC." >&2
    exit 1
fi

if grep -Eq 'vTaskDelay|esp_rom_delay_us|ets_delay_us|usleep|sleep\(' src/LcdDisplay.cpp; then
    echo "ERROR: Inicjalizacja LCD nie może używać blokujących opóźnień." >&2
    exit 1
fi

if grep -Eq 'vTaskSuspend' src/LcdService.cpp; then
    echo "ERROR: Zadanie LCD nie może zostać trwale zawieszone po błędzie I2C." >&2
    exit 1
fi

if ! grep -q 'esp_log_level_set(kEspIdfI2cLogTag, ESP_LOG_NONE)' src/I2cBus.cpp; then
    echo "ERROR: Surowy log sterownika i2c.master musi pozostac wyciszony; bledy raportuje LcdService." >&2
    exit 1
fi

if ! grep -q 'i2c_master_bus_reset(handle_)' src/I2cBus.cpp || \
    grep -Eq 'bus_\.reset\(|settings_service_->detachBus\(' src/LcdService.cpp; then
    echo "ERROR: Recovery I2C ma zachowac kontroler i uchwyty niezaleznych klientow." >&2
    exit 1
fi

if grep -Eq 'vTaskDelay|esp_rom_delay_us|ets_delay_us|usleep|sleep\(' \
    src/At24c08.cpp src/EncryptedSettingsStore.cpp src/SettingsService.cpp; then
    echo "ERROR: Zapis ustawień nie może używać blokujących opóźnień." >&2
    exit 1
fi

if grep -Eiq 'brightness|duty|led_(state|brightness)|channel_(state|brightness)' \
    src/PersistentSettings.cpp src/PersistentSettings.hpp; then
    echo "ERROR: Trwały payload nie może zawierać jasności ani stanu kanałów LED." >&2
    exit 1
fi

if grep -Eq 'vTaskDelay|esp_rom_delay_us|ets_delay_us|usleep|sleep\(' src/WifiService.cpp; then
    echo "ERROR: Wi-Fi i reconnect maja uzywac zdarzen oraz timerow, nie blokujacych opoznien." >&2
    exit 1
fi

if ! grep -q 'WIFI_STORAGE_RAM' src/WifiService.cpp || \
    ! grep -Eq 'nvs_enable[[:space:]]*=[[:space:]]*0' src/WifiService.cpp || \
    ! grep -q '^CONFIG_ESP_WIFI_NVS_ENABLED=n$' sdkconfig.defaults; then
    echo "ERROR: Sterownik Wi-Fi nie może tworzyć własnej trwałej kopii konfiguracji w NVS." >&2
    exit 1
fi

if ! grep -Eq '^board_build\.partitions[[:space:]]*=[[:space:]]*partitions_ota_4mb\.csv$' \
    platformio.ini || \
    ! grep -Eq '^ota_0,[[:space:]]*app,[[:space:]]*ota_0,[[:space:]]*0x20000,[[:space:]]*0x1D0000' \
        partitions_ota_4mb.csv || \
    ! grep -Eq '^ota_1,[[:space:]]*app,[[:space:]]*ota_1,[[:space:]]*0x1F0000,[[:space:]]*0x1D0000' \
        partitions_ota_4mb.csv; then
    echo "ERROR: Build wymaga jawnej dwuslotowej tabeli OTA dla flash 4 MB." >&2
    exit 1
fi

if ! grep -q '^\[env:esp32doit-devkit-v1-ota\]$' platformio.ini || \
    ! grep -Eq '^extends[[:space:]]*=[[:space:]]*env:esp32doit-devkit-v1$' platformio.ini || \
    ! grep -Eq '^custom_sdkconfig_source[[:space:]]*=[[:space:]]*sdkconfig\.esp32doit-devkit-v1$' \
        platformio.ini || \
    ! grep -q 'scripts/publish_ota.py.*--image' platformio.ini || \
    ! grep -q '^secrets\.\*$' .gitignore; then
    echo "ERROR: Profil OTA musi uzywac pelnego bazowego sdkconfig i ignorowanych sekretow." >&2
    exit 1
fi

if ! grep -q '^CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y$' sdkconfig.defaults || \
    ! grep -q '^CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y$' sdkconfig.defaults || \
    ! grep -q 'event.retain' src/MqttService.cpp || \
    ! grep -q 'esp_ota_mark_app_valid_cancel_rollback' src/OtaService.cpp; then
    echo "ERROR: OTA wymaga HTTPS, odrzucania retained command i rollbacku." >&2
    exit 1
fi

if grep -Eq 'vTaskDelay|esp_rom_delay_us|ets_delay_us|usleep|sleep\(' \
    src/OtaService.cpp src/OtaService.hpp; then
    echo "ERROR: Zadanie OTA nie moze uzywac blokujacych funkcji opozniajacych." >&2
    exit 1
fi

if ! grep -Eq '^monitor_eol[[:space:]]*=[[:space:]]*CR$' platformio.ini; then
    echo "ERROR: Monitor PlatformIO musi wysylac pojedynczy CR wymagany przez ESP Console REPL." >&2
    exit 1
fi

if ! grep -q 'kServiceConsoleRxPin = GPIO_NUM_39' src/BoardConfig.hpp || \
    ! grep -q 'uart_config.rx_gpio_num = static_cast<int>(board_config::kServiceConsoleRxPin)' \
        src/ServiceConsole.cpp || \
    ! grep -q '^CONFIG_ESP_CONSOLE_UART_DEFAULT=y$' sdkconfig.defaults; then
    echo "ERROR: Aplikacja ma odbierac konsole na GPIO39, zachowujac domyslny UART0 bootloadera." >&2
    exit 1
fi

if grep -Eq 'ledc_' src/MqttService.cpp src/MqttService.hpp; then
    echo "ERROR: MQTT nie moze sterowac LEDC poza kolejka kontrolera." >&2
    exit 1
fi

if grep -q 'root.get(), "availability_topic"' src/MqttDiscoveryPayload.cpp || \
    ! grep -q 'addAvailabilityList(root.get(), device_availability)' src/MqttDiscoveryPayload.cpp; then
    echo "ERROR: Root MQTT discovery ma uzywac listy availability, aby sensory INA228 mogly ja bezpiecznie nadpisac." >&2
    exit 1
fi

if grep -Eq 'ledc_' src/IrReceiverService.cpp src/IrReceiverService.hpp; then
    echo "ERROR: Odbiornik IR nie moze sterowac LEDC poza kolejka kontrolera." >&2
    exit 1
fi

if ! grep -q '^CONFIG_LED_CONTROLLER_IR_NEC=n$' sdkconfig.defaults; then
    echo "ERROR: Odbiornik IR musi pozostac domyslnie wylaczony do czasu montazu." >&2
    exit 1
fi

if ! grep -q '^CONFIG_LED_CONTROLLER_AT24C08=n$' sdkconfig.defaults; then
    echo "ERROR: AT24C08N musi pozostac domyslnie wylaczony do czasu montazu." >&2
    exit 1
fi

if ! grep -q '^CONFIG_LED_CONTROLLER_INA228=n$' sdkconfig.defaults; then
    echo "ERROR: INA228 musi pozostac domyslnie wylaczony do czasu montazu." >&2
    exit 1
fi

if ! grep -q '^CONFIG_LED_CONTROLLER_DS18B20=n$' sdkconfig.defaults; then
    echo "ERROR: DS18B20 musi pozostac domyslnie wylaczony do czasu montazu." >&2
    exit 1
fi

if grep -Eq 'ledc_|vTaskDelay|esp_rom_delay_us|ets_delay_us|usleep|sleep\(' \
    src/Ina228.cpp src/Ina228.hpp; then
    echo "ERROR: Telemetria INA228 nie moze sterowac LEDC ani uzywac blokujacych opoznien." >&2
    exit 1
fi

if grep -Eq 'ledc_|vTaskDelay|esp_rom_delay_us|ets_delay_us|usleep|sleep\(' \
    src/Ds18b20Service.cpp src/Ds18b20Service.hpp; then
    echo "ERROR: DS18B20 nie moze sterowac LEDC ani uzywac blokujacych opoznien." >&2
    exit 1
fi

if ! grep -q 'GPIO_PULLUP_DISABLE' src/PirInput.cpp || \
    ! grep -q 'GPIO_PULLDOWN_DISABLE' src/PirInput.cpp || \
    grep -Eq 'GPIO_PULLUP_ENABLE|GPIO_PULLDOWN_ENABLE|ledc_' src/PirInput.cpp src/PirInput.hpp; then
    echo "ERROR: Wejscia PIR GPIO34/35 nie moga wlaczac wewnetrznych pull-up/down ani sterowac LEDC." >&2
    exit 1
fi

if grep -Eq 'vTaskDelay|esp_rom_delay_us|ets_delay_us|usleep|sleep\(' \
    src/PirInput.cpp src/PirInput.hpp; then
    echo "ERROR: Polling PIR nie moze uzywac blokujacych opoznien." >&2
    exit 1
fi

if grep -Eq 'mqtt_service_->status\(' src/LedController.cpp; then
    echo "ERROR: Kontroler LED musi pobierac status MQTT bez oczekiwania przez tryGetStatus()." >&2
    exit 1
fi

# The public OTA repository URL is infrastructure, not the device identity.
if grep -ERi 'led[_-]korytarz|CorridorController|corridor_control|Projekt lokalny' src | grep -Fv 'https://github.com/Daniel99999999999/led-korytarz-firmware/releases'; then
    echo "ERROR: Firmware nie moze zawierac starej tozsamosci ani kodu migracyjnego." >&2
    exit 1
fi

python scripts/check_utf8.py
python scripts/test_release_artifact.py

echo "==> Build ESP32 DOIT DevKit V1"
pio run -e esp32doit-devkit-v1

if [[ -f test/host/CMakeLists.txt ]]; then
    echo "==> Testy hostowe CMake/CTest"

    if command -v cmake >/dev/null 2>&1; then
        cmake_bin="$(command -v cmake)"
    elif [[ -x "$HOME/.platformio/packages/tool-cmake/bin/cmake.exe" ]]; then
        cmake_bin="$HOME/.platformio/packages/tool-cmake/bin/cmake.exe"
    elif [[ -x "$HOME/.platformio/packages/tool-cmake/bin/cmake" ]]; then
        cmake_bin="$HOME/.platformio/packages/tool-cmake/bin/cmake"
    else
        echo "ERROR: Nie znaleziono CMake wymaganego przez testy hostowe." >&2
        exit 127
    fi

    cmake_dir="$(dirname "$cmake_bin")"
    if [[ -x "$cmake_dir/ctest.exe" ]]; then
        ctest_bin="$cmake_dir/ctest.exe"
    elif [[ -x "$cmake_dir/ctest" ]]; then
        ctest_bin="$cmake_dir/ctest"
    elif command -v ctest >/dev/null 2>&1; then
        ctest_bin="$(command -v ctest)"
    else
        echo "ERROR: Nie znaleziono CTest wymaganego przez testy hostowe." >&2
        exit 127
    fi

    if [[ "${HOST_TEST_COMPILER:-}" == "bcc64" ]]; then
        python scripts/test_host_bcc.py
    else
        cmake_args=(-S test/host -B .pio/host-tests)
        case "$(uname -s)" in
            MINGW* | MSYS* | CYGWIN*)
                cmake_args+=(-G "Visual Studio 17 2022" -A x64)
                ;;
        esac
    
        "$cmake_bin" "${cmake_args[@]}"
        "$cmake_bin" --build .pio/host-tests --config Debug
        "$ctest_bin" --test-dir .pio/host-tests -C Debug --output-on-failure
    fi

fi

if grep -Eq '^\[env:native\]' platformio.ini; then
    echo "==> Testy PlatformIO native"
    pio test -e native
elif [[ ! -f test/host/CMakeLists.txt ]]; then
    echo "==> INFO: Środowisko [env:native] nie istnieje; testy hostowe jeszcze nie są skonfigurowane."
fi

echo "==> Kontrola zakończona powodzeniem"
