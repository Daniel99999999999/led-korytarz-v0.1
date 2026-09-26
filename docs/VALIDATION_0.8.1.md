# Walidacja v0.8.1 — 2026-09-24

Etap implementacji zakończony. Nie wykonano wgrania ani testu w HA/na ESP32.

- Bazowy i końcowy `HOST_TEST_COMPILER=bcc64 ./scripts/check.sh`: PASS.
- Końcowy log `.pio/check-debug-final.log`: firmware build PASS, brak ostrzeżeń.
- RAM 80 224 / 327 680 B; flash 1 242 409 / 1 900 544 B.
- 58 scenariuszy logiki PASS: w tym sesja debug, odnowienie, overflow czasu, przepełnienie
  bufora, kasowanie po OFF, skracanie wpisów i schemat Discovery.
- Adaptery PASS: poprzednie testy LCD/PWM oraz rzeczywisty DebugLog z atrapami ESP-IDF.
  Sprawdzono UART, ANSI, limit czasu, skracanie, pomijanie ISR/eksportera, przyczynę resetu
  oraz 4000 wpisów z czterech wątków równolegle do odbioru i przełączania sesji.
- Kompilator hostowy BCC wymaga `-tM` dla biblioteki atomików; wyjątki i RTTI nadal wyłączone.
  Windows używa _beginthreadex w teście, inne platformy std::thread.
- Kontrola UTF-8, formatowania zmienionych C/C++ i składni Python PASS.
- Nie wykonywano osobnego buildu OTA ani wariantu minimalnego; użyto pełnej konfiguracji PCB.
- Firmware SHA-256: `776346089a654d69e974c84355b823ae6ecd6f3aeac08ec179b58295d7d89f58`.

Komendy: `pio run -e esp32doit-devkit-v1`, `python scripts/test_host_bcc.py`,
`HOST_TEST_COMPILER=bcc64 ./scripts/check.sh`, `python scripts/check_utf8.py`, clang-format.
Na Windows użyto Git Bash oraz PlatformIO Python w PATH. Narzędzia .platformio wymagają
uprawnienia do zapisu poza workspace; pierwsza próba bez niego była zablokowana.

Zmiany: src/DebugBuffer.hpp, DebugLog.*, DebugDiscovery.*, MqttService.*, main.cpp,
src/CMakeLists.txt, CMakeLists.txt (0.8.1), test/host/test_main.cpp, test_adapters.cpp,
stubs/esp_log.h, esp_timer.h, esp_system.h, freertos/task.h, test/host/CMakeLists.txt,
scripts/test_host_bcc.py, docs/DEBUG_MQTT.md, docs/ARCHITECTURE.md, README_PL.md,
CODEX_HANDOFF.md i PLANS.md. GPIO, EEPROM/NVS i format NVRAM nie zmieniły się.

HIL pozostaje do wykonania według [DEBUG_MQTT.md](DEBUG_MQTT.md): encje i podgląd HA,
10-minutowy timeout offline, restart z OFF, flood logów i lokalne sterowanie.
Nie zdiagnozowano jeszcze przyczyny zgłaszanego watchdog. Brak aktywnego połączenia MQTT
uniemożliwia włączenie debug z HA i odbiór strumienia. Logi nie przeżywają restartu.
