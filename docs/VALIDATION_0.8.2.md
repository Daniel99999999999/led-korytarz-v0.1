# Walidacja v0.8.2 — 2026-09-25

Zakres: automatyczna diagnostyka konfiguracji przy zastosowaniu rekordu EEPROM oraz
sieci po GOT_IP, pełniejsze błędy MQTT i odroczenie pierwszego startu MQTT do uzyskania IP.
Nie zmieniono GPIO ani zapisanej konfiguracji. Hasła nie są drukowane.

Zmodyfikowane pliki: CMakeLists.txt (wersja), src/MqttService.cpp/.hpp,
src/WifiService.cpp/.hpp, src/main.cpp, src/SettingsService.cpp,
PLANS.md, CODEX_HANDOFF.md, README_PL.md, docs/ARCHITECTURE.md i ten raport.

Wykonano `HOST_TEST_COMPILER=bcc64 ./scripts/check.sh` przez Git Bash, z PlatformIO
w PATH. Skrypt wykonał `pio run -e esp32doit-devkit-v1`, testy hostowe oraz kontrolę UTF-8
 i reguł projektu. Zmienione źródła sformatowano clang-format.

Wynik: PASS, brak ostrzeżeń kompilatora, 58 testów hostowych PASS, adaptery PASS
(0 błędów). RAM: 80 232 B; flash: 1 245 453 B. Log: `.pio/check-startup-final.log`.
Istniejące testy hostowe nie symulują cyklu Wi-Fi/MQTT — poniższe próby są nadal wymagane.

## Do wykonania na sterowniku

1. Wgrać zwykłym Upload przez USB, bez Erase Flash (zachować klucz NVS do EEPROM).
2. Otworzyć monitor 115200, nacisnąć EN i zebrać cały log od startu przez około 30 s.
3. Potwierdzić wersję 0.8.2, odtworzenie EEPROM, WIFI CONFIG, MQTT CONFIG, NTP oraz
   NETWORK RUNTIME. Pierwsze esp_mqtt_client_start musi wystąpić dopiero po GOT_IP.
4. Przekazać log z MQTT target, wynikiem startu i polami błędu; nie podawać haseł.
5. Sprawdzić start bez dostępnego AP, późniejszy powrót AP i uzyskanie IP; następnie
   utratę/powrót Wi-Fi po połączeniu MQTT. Oczekiwany jeden klient i skuteczny reconnect.
6. Gdy dostępna będzie konsola RX: sprawdzić mqtt clear/disconnect przed uzyskaniem IP
   i ponowne mqtt apply; nie powinien startować anulowany klient.
7. W czasie awarii sieci sprawdzić lokalne przyciski i PWM.

Nie wykonano wgrania ani tych testów sprzętowych. Usunięcie próby startu bez IP nie dowodzi
usunięcia późniejszych timeoutów TCP. Pola błędu interpretować zgodnie z error_type;
nie wszystkie są miarodajne dla każdego typu błędu. Tag esp-tls nie oznacza użycia TLS
przy URI mqtt://. Skonfigurowane adresy w trybie DHCP nie zastępują wartości runtime.

Dodatkowy log rozdziela created/started/connected; błąd zawiera rzeczywisty, oczyszczony
adres URI oraz sock_errno_description (tekst strerror, nie symbol stałej errno).
