# Sterowanie lokalne i konfiguracja HA — v0.8.0

## Ustawienia urządzenia w Home Assistant

MQTT Device Discovery dodaje siedem encji kategorii `config` do istniejącego urządzenia:

| Encja | Wartości | Domyślnie |
| --- | --- | --- |
| KEY1–KEY5 — przypisanie | Wyłączony, kanał 1–5, kanały 1+2, wszystkie | KEYn -> kanał n |
| PIR1 — czas wyłączenia | 0–3600 s | 0 (automatyka wyłączona) |
| PIR2 — czas wyłączenia | 0–3600 s | 0 (automatyka wyłączona) |

Opcje MQTT są zapisane w ASCII (`Wylaczony`, `Kanal 1`, `Kanaly 1+2`, `Wszystkie`).
Konfiguracja dla korytarza L: KEY1 = `Kanaly 1+2`, KEY2 = `Wylaczony`.
PIR1 zawsze obsługuje kanał 1, PIR2 kanał 2. Ich mapowanie nie zależy od przypisania KEY.
Po aktualizacji dotychczasowy układ działa jak wcześniej, dopóki użytkownik nie zmieni ustawień.

Poziomy jasności ustawia się oddzielnie dla obu świateł w HA. ON/OFF nie zmienia zapamiętanego
w RAM poziomu danego kanału. Firmware nie zapisuje jasności ani ON/OFF do żadnej pamięci trwałej;
po resecie wyjścia są OFF, a poziom kolejnego włączenia wynosi 100%, dopóki HA go nie zmieni.
Komenda `{"state":"OFF","brightness":102}` ustawia 40% bez zapalania światła.
Osobne profile jasności dzień/noc ani osobne encje jasności domyślnej nie są częścią tej zmiany.

## Przycisk grupowy i priorytety

- Krótkie naciśnięcie: jeśli wszystkie przypisane, sprawne kanały są ON, wyłącza grupę;
  w pozostałych przypadkach włącza grupę. Nie wykonuje dwóch niezależnych toggle.
- Przytrzymanie od 500 ms reguluje grupę, zachowując proporcje początkowych jasności
  z zaokrągleniem do 1% i ograniczeniem 1–100%. Najjaśniejszy sprawny kanał prowadzi regulację.
- Mapa rozpoczętej regulacji jest zamrożona do zwolnienia przycisku; zmiana przypisania
  w HA dotyczy następnego gestu. Drugi przycisk z nakładającym się przypisaniem nie przejmuje regulacji.
- Ręczna komenda KEY, HA lub IR przejmuje dany kanał od PIR. Timer PIR nie gasi światła
  pozostawionego ręcznie ON. Po ręcznym OFF aktualne HIGH jest ignorowane do powrotu do LOW.
- Podczas przytrzymania komendy HA/IR tego kanału są odraczane, zgodnie z dotychczasowym zachowaniem.
- Błąd kanału jest zatrzaskiwany; pozostałe sprawne kanały nadal działają.

## PIR

HIGH utrzymuje odpowiedni kanał ON; po ustaniu aktywności odliczany jest ustawiony czas.
Ponowne HIGH podtrzymuje światło i zaczyna kolejny okres oczekiwania na LOW.
Zmiana czasu dotyczy również trwającego odliczania; ustawienie 0 anuluje automatykę i gasi tylko
światło będące nadal pod kontrolą PIR. Nie gasi światła przejętego ręcznie.
Stan HIGH już przy starcie jest obsługiwany po włączeniu/odtworzeniu konfiguracji PIR.

Na wejściu NC ruch i przerwanie przewodu są elektrycznie nierozróżnialne. Stałe HIGH,
także od przerwanego przewodu, podtrzymuje światło bez końca przy włączonej automatyce.
Można je wyłączyć ręcznie; ponowne uzbrojenie wymaga LOW. Nie zaimplementowano osobnego
alarmu uszkodzenia przewodu ani automatycznego limitu czasu HIGH.

## Trwałość — wyłącznie DS1307 NVRAM

`CONFIG_LED_CONTROLLER_DS1307_NVRAM=y` włącza opcjonalny moduł. Domyślnie jest `n` (DNP).
Lokalny pełny `sdkconfig.esp32doit-devkit-v1` ma `y`; profil OTA korzysta z tego samego pliku.
Konfiguracja KEY/PIR/LCD **nie trafia do AT24C08N ani NVS**. Dotychczasowe zapisy Wi-Fi/MQTT
do AT24C08N pozostają niezależne.

DS1307 ma 56 B RAM podtrzymywanej baterią, adresy 0x08–0x3F. Rejestry czasu i SQW
0x00–0x07 pozostają nietknięte. Format zajmuje całą NVRAM i nie może współdzielić jej
z innym programem:

| Pole slotu | Bajty |
| --- | --- |
| Magic LCF, wersja 1 | 0–3 |
| Sekwencja little-endian | 4–7 |
| Pięć masek KEY | 8–12 |
| Dwa czasy uint16 little-endian | 13–16 |
| Czas przycisku LCD uint16 LE | 17–18 |
| Tryb LCD (auto/OFF/ON = 0/1/2) | 19 |
| Zarezerwowane, zero | 20–22 |
| CRC32 bajtów 0–22 | 23–26 |
| Commit 0xA5 | 27 |

Dwa sloty po 28 B zaczynają się pod 0x08 i 0x24. Zapis unieważnia slot docelowy,
zapisuje zawartość, ustawia commit na końcu i odczytuje cały slot do weryfikacji.
Poprzedni slot pozostaje nietknięty. Zmiany są scalane przez 2 s od otrzymania najnowszej
migawki kontrolera (migawki do usługi I2C trafiają najwyżej raz na sekundę).
Nie ma zapisów przy ruchu PIR, ON/OFF ani krokach ściemniania.

Brak układu lub błąd zapisu nie blokuje sterowania; konfiguracja pozostaje w RAM, ponowienie
następuje po 30 s. Niepoprawne CRC/bateria bez podtrzymania oznacza użycie wartości domyślnych.
Po pojawieniu się pamięci nowsze ustawienia wprowadzone już w RAM mają pierwszeństwo nad starym rekordem.
Nie ma jeszcze encji HA potwierdzającej fizyczne zakończenie zapisu — potwierdza je log UART.

Źródło kontraktu pamięci: [DS1307, Analog Devices](https://www.analog.com/media/en/technical-documentation/data-sheets/DS1307.pdf).

## MQTT i prezentacja PWM

Tematy względem `<topic-prefix>/<device-id>`:

- `config/key/N/set` i `config/key/N/state`: tekst jednej z opcji wyboru;
- `config/pir_delay/N/set` i `config/pir_delay/N/state`: całkowita liczba sekund.

Komendy nie są retained, stan jest retained QoS 1. Fragmenty, nieznane opcje, liczby poza
zakresem i retained commands są odrzucane. Zmiany wykonuje wyłącznie kolejka kontrolera.
HA otrzymuje potwierdzenie po zastosowaniu konfiguracji, nie optymistycznie.
Reconnect i birth HA odtwarzają discovery i stany; nie wykonują ponownie komend.

Zwykłe ON/OFF nadal korzysta z płynnego PWM, ale LCD pokazuje jasność docelową bez procentów
pośrednich fade. Rzeczywista regulacja jasności nadal odświeża wskazania. MQTT już wcześniej
publikowało docelową jasność; zachowano publikację różnicową, bez wiadomości dla każdego kroku fade.
Pozostała telemetria nie jest ograniczana przez tę zmianę.

Discovery używa oficjalnych schematów [MQTT select](https://www.home-assistant.io/integrations/select.mqtt/)
i [MQTT number](https://www.home-assistant.io/integrations/number.mqtt/).

## Testy sprzętowe do wykonania

1. Sprawdzić dziesięć ustawień w HA i potwierdzenie stanu po zmianie; powrót mapy 1:1.
2. Ustawić różne jasności, włączyć jeden kanał PIR-em, nacisnąć KEY1; oba mają się zapalić.
3. Sprawdzić hold grupy, zmianę mapy podczas hold i dwa przyciski z nakładającą się mapą.
4. Sprawdzić oddzielne czasy, podtrzymanie ruchu, ręczne ON/OFF i brak HA/Wi-Fi.
5. Sprawdzić HIGH od startu i przerwę przewodu NC; sprawdzić czas 0.
6. Z DS1307 i baterią zmienić ustawienia, poczekać na log zapisu, odłączyć zasilanie i odtworzyć.
7. Przerwać zapis/odłączyć DS1307, potwierdzić pracę w RAM oraz odzyskanie bez wpływu na LCD/EEPROM/INA.
8. Potwierdzić brak zapisu do rejestrów zegara i do EEPROM-u przy zmianach KEY/PIR.
9. Obserwować LCD i MQTT podczas ON/OFF oraz regulacji: bez pośrednich procentów przy ON/OFF.

Podświetlenie, odczyt LCD i automatyzacja HA: [LCD_CONTROL.md](LCD_CONTROL.md).
