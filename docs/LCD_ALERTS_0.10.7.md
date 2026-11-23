# Rotacja alarmow LCD - rozszerzenie 0.10.7

Czwarta linia pokazuje kolejne aktywne komunikaty co 3 s. Przy stalym zestawie
kazdy pojawi sie raz na pelny obieg; dodanie innego nie restartuje rotacji.
Znikniecie aktualnie wyswietlanego alarmu od razu wybiera nastepny. Jesli nie ma
alarmow, wracaja strony informacyjne i chwilowe komunikaty pilota. Stan normalny
ma tekst `STATUS: BEZ ALARMOW` (brak wykrytych alarmow, nie pelny autotest sprzetu).
Ekran lokalnego hotspotu OTA nadal przejmuje caly LCD, aby mozna bylo odczytac
haslo i dane dostepowe. Wylaczone podswietlenie nie jest automatycznie wlaczane.

## Zrodla

| Komunikaty | Warunek |
| --- | --- |
| STEROWANIE: BLAD | Dotychczasowy ogolny status fault kontrolera/LCD |
| RESET: WATCHDOG / BROWNOUT / PANIC / NIEZNANY | Powod ostatniego resetu; do nastepnego restartu |
| DS1307: BRAK ACK | Blad etapu probe_0x68 |
| DS1307: BLAD ODCZYTU | Blad podlaczenia adaptera, wskaznika lub odczytu |
| NVRAM: BLAD ZAPISU | Blad invalidate/write/commit/verify |
| NVRAM: BRAK REKORDU | Odczyt byl poprawny, brak poprawnego rekordu (takze pierwsze uzycie) |
| NVRAM: TYLKO RAM | Ustawienia czekaja na zapis co najmniej 10 s |
| EEPROM: BLAD / NIEZAPISANE | last_error, rozroznienie wedlug pending/in_progress |
| EEPROM: BRAK REKORDU | Pamiec gotowa, brak znalezionego rekordu |
| KONFIG: NIEAKTYWNA | Rekord EEPROM znaleziony, lecz konfiguracja niezastosowana |
| INA228: BRAK POMIARU | Funkcja wlaczona, brak dostepnego pomiaru |
| TEMP RAD / TEMP OBU: BRAK | Tylko przypisany czujnik, bez poprawnego pomiaru |
| WIFI / MQTT: BRAK KONFIG | Brak konfiguracji lub oczekiwanie na apply |
| WIFI / MQTT: ROZLACZONE | Brak polaczenia; wtorny blad MQTT ukryty przy braku Wi-Fi |
| MQTT: BLAD LOGOWANIA / ZALEGLE DANE | Istniejacy status auth/backlog |
| CZAS: BRAK NTP | Wi-Fi polaczone, brak synchronizacji |
| RAM: MALO PAMIECI | Aktualny wewnetrzny heap 8-bit <8192 B; kasowanie >=12288 B |
| STOS: MINIMUM <512B | Znane historyczne minimum ktoregos monitorowanego zadania <512 B |
| OTA: BLAD INSTALACJI | Stan failed lub unavailable uslugi OTA |
| OTA: BLAD KONTROLI | Nieudane ostatnie automatyczne sprawdzenie wydania |
| OTA: AKTUALIZACJA | queued/downloading/verifying/rebooting |

EEPROM, czujniki, siec i NTP dostaja 30 s na inicjalizacje. Brak przypisanych
czujnikow temperatury nie jest alarmem. Nie ma progow wysokiej temperatury,
pradu ani napiecia: ich dobor nie byl rozstrzygniety. Alarm heap dotyczy probki
pobranej przy odswiezeniu LCD, nie gwarantuje wykrycia kazdego krotkiego spadku.
Alarm stosu jest historyczny, a nie pomiarem chwilowego uzycia. Brak pomiaru
stosu nie jest zamieniany na alarm ani traktowany jako dowod poprawnosci.
Nie kazdy reset jest awaria: zwykly start po zasilaniu i programowy restart
nie dodaja komunikatu. Nie ma pomiaru/oceny baterii DS1307; PCB nie ma ukladu
pomiarowego, zgodnie z decyzja uzytkownika temat pozostaje odlozony.

## Implementacja i testy

Maska 32-bit, stale napisy <=20 znakow, brak alokacji heap i dodatkowych zadan.
LcdService zbiera stany przy renderowaniu (okolo 1 Hz), bez dodatkowych operacji
I2C. Status OTA pobiera bez czekania na mutex; przy zajetosci zachowuje poprzedni.
Kontroler LED nie wykonuje dodatkowych blokujacych odczytow statusow.
Testy hostowe obejmuja rotacje wielu i wszystkich komunikatow, okres 3 s,
usuniecie biezacego alarmu, powrot do pustego zestawu, zawiniecie licznika czasu
oraz dopasowanie napisow do 20 znakow.

HIL pozostaje: podlaczyc LCD, wywolac jednoczesnie blad DS1307 i zanik sieci,
sprawdzic pelny obieg i usuniecie alarmow po odzyskaniu komunikacji. Sprawdzic
brak alarmu nieprzypisanych DS18B20, dostepnosc ekranu AP OTA oraz HWM zadania LCD.
Nie opublikowano ani nie wgrano firmware w ramach tej zmiany.

## Wyniki lokalne 2026-11-23

`HOST_TEST_COMPILER=bcc64 ./scripts/check.sh`: PASS, testy hostowe i adaptery.
Po doprecyzowaniu napisow i rozroznieniu brakujacej konfiguracji:
`pio run -e esp32doit-devkit-v1`: PASS bez ostrzezen. Testy hostowe obejmowaly
koncowy zestaw napisow. `python scripts/check_utf8.py`: 178 plikow PASS;
`git diff --check`: PASS. RAM statyczny 122648 B, flash 1403197 B.
Ramka collectAlerts z objdump: 112 B; nie jest to calkowity szczyt stosu LCD.
Logi: .pio/check-lcd-alerts-0107.log i .pio/build-lcd-alerts-final.log.
Testy na fizycznym LCD i zapasu stosu na ESP32 pozostaja niewykonane.
