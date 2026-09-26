# Walidacja v0.8.0 — 2026-09-24

## Status

Etap implementacji i kontroli komputerowej zakończony. Firmware nie został wgrany.
HIL oraz uruchomienie automatyzacji w HA użytkownika pozostają niewykonane.
Kopia nie zawiera historii .git; opis historycznych wyników v0.7.2 pochodzi z PLANS.md.

## Wyniki

- Pełny `scripts/check.sh`: PASS, 2026-09-24, log `.pio/check-final.log`.
- ESP32 DOIT DevKit V1 / PlatformIO espressif32 7.0.1 / ESP-IDF 6.0.1: kompilacja PASS,
  bez ostrzeżeń w końcowym logu. RAM 71 496 / 327 680 B, flash 1 238 245 / 1 900 544 B.
- W tym obrazie włączone: AT24C08, IR NEC, INA228, DS18B20, DS1307 NVRAM.
- 55 scenariuszy logiki: PASS. Mapowanie KEY, grupy, priorytety PIR, przepełnienie czasu,
  dwa rekordy NVRAM, przerwany zapis, discovery, publikacja MQTT, tryby i przycisk LCD.
- Osobny zestaw adapterów: PASS. Rzeczywiste LcdDisplay/LedChannel z atrapami transportu:
  dwa skany 80 znaków, adresy DDRAM, znak 0xDF, wykrywanie różnicy, błędy obu faz odczytu,
  opuszczenie E/RW, brak podświetlenia podczas inicjalizacji OFF, NACK/timeout,
  stabilna jasność przy ON/OFF i wartości pośrednie przy zmianie jasności.
- Testy hostowe kompilowane z `-Wall -Wextra -Werror -fno-exceptions -fno-rtti`.
- Formatowanie 33 plików C/C++: zgodne z repozytoryjnym .clang-format.
- Kontrola ścisłego UTF-8 bez BOM i składni skryptów Python: PASS.
- Blueprint HA sprawdzono przez przegląd względem dokumentacji HA; brak uruchomienia w HA.
  Próba lokalnego parsera YAML: NOT RUN (PyYAML nie jest zainstalowany).
- Nie wykonywano osobnego końcowego buildu OTA ani wariantu z wyłączonymi opcjami sprzętowymi.
  Profil OTA nadal dziedziczy bazowy sdkconfig przez dotychczasowy skrypt.

Firmware: `.pio/build/esp32doit-devkit-v1/firmware.bin`.
SHA-256: `de793d18c5163df3c196b5b8c97b88b8fd801945942a351c9c3049a112d6a8cd`.
Jest to obraz aplikacji; sposób pierwszej migracji partycji opisano w historycznym planie OTA.

## Wykonane komendy

```powershell
# PlatformIO/Python muszą być dostępne w PATH; lokalny certyfikat znajduje się w ignorowanym .pio.
$env:PATH = 'C:\Users\Wiki\.platformio\penv\Scripts;' + $env:PATH
$env:HOST_TEST_COMPILER = 'bcc64'
$env:PIP_CERT = 'C:\Users\Wiki\led-korytarz-v0.1\.pio\system-ca.pem'
$env:REQUESTS_CA_BUNDLE = $env:PIP_CERT
& 'C:/Program Files/Git/bin/bash.exe' ./scripts/check.sh
```

Skrypt uruchomił `pio run -e esp32doit-devkit-v1`, `python scripts/check_utf8.py`
i `python scripts/test_host_bcc.py`. Osobno wykonano clang-format i porównanie wyniku
formatowania z zapisanymi plikami oraz ast.parse dla skryptów Python.
Pierwsza próba check.sh zatrzymała się na brakującym iconv; zastąpiono tę kontrolę równoważnym
ścisłym sprawdzeniem UTF-8 w Pythonie. Kompilacja wymagała dostępu do narzędzi .platformio
poza katalogiem projektu. Nie wyłączano walidacji certyfikatów TLS.

## Pliki etapu

- `src/LocalControlConfig.*`, `LocalControlDiscovery.*`, `PirAutomation.hpp`,
  `BacklightPolicy.hpp`, `LocalConfigRecord.*`, `Ds1307Nvram.*`: nowe modele i obsługa.
- `src/LedController.*`, `LedChannel.*`, `ControlCommand.hpp`: integracja sterowania.
- `src/MqttService.*`, `MqttPublishTracker.hpp`: discovery, komendy, publikacja różnicowa.
- `src/LcdDisplay.*`, `LcdService.*`: odczyt DDRAM, recovery, podświetlenie i NVRAM.
- `src/BoardConfig.hpp`, `FeatureConfig.hpp`, `src/CMakeLists.txt`, `src/Kconfig.projbuild`,
  `CMakeLists.txt`, `platformio.ini`, `sdkconfig.defaults`: konfiguracja kompilacji.
- Lokalny `sdkconfig.esp32doit-devkit-v1`: pełny wariant z DS1307 NVRAM.
- `test/host/test_main.cpp`, `test_adapters.cpp`, `stubs/`, `CMakeLists.txt`: testy.
- `scripts/check.sh`, `check_utf8.py`, `test_host_bcc.py`: odtwarzalna kontrola laptopa.
- `README_PL.md`, `CODEX_HANDOFF.md`, `PLANS.md`, `docs/*.md`,
  `docs/home-assistant/lcd_sun.yaml`: kontrakty, instrukcje, stan pracy i blueprint.

## Pozostaje na sprzęcie

Wykonać listy w [LOCAL_CONTROL.md](LOCAL_CONTROL.md) i [LCD_CONTROL.md](LCD_CONTROL.md),
zwłaszcza odczyt rzeczywistego LCD, odłączanie urządzeń I2C, baterię i retencję DS1307,
priorytety KEY/PIR/LCD oraz discovery i automatyzację słońca w HA.
Nie potwierdzono jeszcze, że konkretny moduł LCD obsługuje odczyt RW i nie daje
fałszywych różnic. Nie można uznać błędu z odłączaniem LCD za sprzętowo zweryfikowany.
