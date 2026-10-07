# PWM kanałów 4/5 i lokalne OTA — v0.9.0

## Ustawienia w Home Assistant

Po połączeniu MQTT discovery doda po pięć encji konfiguracji dla kanałów 4 i 5:

| Ustawienie | Zakres / domyślna wartość |
| --- | --- |
| Częstotliwość PWM | 100–19531 Hz, krok 1 Hz; domyślnie 19531 |
| Minimalne wypełnienie | 0–100%; domyślnie 0 (brak dodatkowego progu) |
| Minimalny impuls ON | lista czasów w mikrosekundach; domyślnie 0 |
| Minimalny impuls OFF | ta sama lista; domyślnie 0 |
| Tryb wyjścia | PWM / ON/OFF; domyślnie PWM |

Lista czasów: 0, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 3000,
4000, 5000, 10000 us. Zero wyłącza dodatkowe ograniczenie. Dyskretna lista pozwala
zachować dwie kopie konfiguracji w 56 B NVRAM DS1307.

Suwak jasności nadal zadaje dotychczasową wartość procentową. Wyjście PWM jest ograniczane
od dołu przez większy z progów minimalnego wypełnienia i czasu ON, od góry przez czas OFF.
Nie jest to przeskalowanie całej skali suwaka; poniżej/ponad granicą jasność tworzy plateau.
OFF zawsze daje duty 0. Ograniczenia działają także w czasie fade i przy przyciskach/PIR/IR.
W ON/OFF jest stały HIGH dla ON i LOW dla OFF; fade nie jest wykonywany, a parametry PWM
pozostają zapamiętane do powrotu do PWM. Zmiana konfiguracji może krótko wygasić dany kanał.
Rozdzielczość pozostaje 12-bitowa; częstotliwość i czasy podlegają kwantyzacji sprzętowej.

Sprzeczna kombinacja jest odrzucana przez kontroler, a poprzednie ustawienia pozostają aktywne.
Komunikat trafia do logu. Przy dużych czasach impulsów najpierw zmniejszyć częstotliwość;
przed zwiększeniem częstotliwości zmniejszyć czasy ON/OFF. Żadna lista nie stanowi profilu
zalecanego dla żarówek Diall ani potwierdzenia ich kompatybilności z PWM zasilania.

Kanały 1–3 zachowują timer 0 i dotychczasowe parametry. Kanał 4 używa timera 1,
a kanał 5 timera 2; źródło APB, bez zmiany GPIO i polaryzacji. Tylko zadanie kontrolera
modyfikuje LEDC. Błąd ustawienia sprzętu zatrzaskuje dany kanał w stanie OFF do restartu.
Przy błędzie sterownik próbuje zatrzymać LEDC i przełączyć pin na GPIO LOW. Gdy operacje
GPIO zgłoszą błąd, stan fizycznego wyjścia wymaga pomiaru; próba awaryjnego LOW nie
zastępuje zabezpieczeń elektrycznych.
Dalsze komendy PWM do kanału z zatrzaśniętą awarią są odrzucane.

MQTT: `<base>/config/pwm/{4|5}/{frequency|minimum|on_us|off_us|mode}/set`.
Potwierdzenia na identycznej ścieżce zakończonej `/state`. Komendy bez retain;
stan potwierdzony retain, publikowany różnicowo. `on_us`/`off_us` przyjmują czas z listy,
`mode` przyjmuje dokładnie `PWM` albo `ON/OFF`.
Licznik przyjętych komend PWM rośnie dopiero po decyzji kontrolera. Sprzeczna konfiguracja
lub błąd zastosowania zwiększa licznik odrzuconych komend; samo dodanie do kolejki nie
jest jeszcze potwierdzeniem.

## Pamięć

Wyłącznie RAM ESP32 + NVRAM DS1307, razem z KEY/PIR/LCD; bez zapisu tych ustawień do
EEPROM AT24C08N i NVS. Bez DS1307 zmiany znikają po restarcie.
Format v2 zachowuje dwa sloty po 28 B, CRC i commit; payload zajmuje 115 bitów.
Czytnik obsługuje v1 i v2. Z v1 odtwarza KEY/PIR/LCD i dodaje domyślne PWM4/5.
Zapis v2 następuje przy kolejnej zmianie konfiguracji. Przerwanie zapisu pozostawia
poprzedni kompletny slot. Firmware 0.8.x nie odczyta v2; po nadpisaniu obu slotów
powrót do starego firmware oznacza brak odtworzenia konfiguracji lokalnej.
Wi-Fi/MQTT w AT24C08N nie są zmieniane.

## Lokalna aktualizacja bez MQTT, RPi i internetu

1. Po uruchomieniu sterownika przytrzymać dedykowany przycisk LCD (GPIO19) przez 10 s.
   Przycisk trzymany już podczas bootu trzeba najpierw puścić. KEY1–KEY5 pozostają bez zmian.
2. LCD pokaże nazwę sieci `LED-OTA-xxxxxx`, losowe 12-znakowe hasło i adres strony.
   Podświetlenie będzie włączone na czas trybu serwisowego. Krótsze przytrzymanie
   przełącza podświetlenie po puszczeniu; pełny gest 10 s nie przełącza podświetlenia.
3. Połączyć laptop z siecią sterownika. Pozostać połączonym mimo komunikatu o braku internetu.
4. Otworzyć **HTTP**, nie HTTPS, pod adresem z LCD (zwykle http://192.168.4.1).
   Przy kolizji podsieci STA używany jest http://10.42.0.1. Jeśli obie podsieci kolidują,
   uruchomienie hotspotu zostaje odrzucone i zalogowane.
5. Wybrać `firmware.bin` z Build PlatformIO i kliknąć Aktualizuj. Nie używać plików
   bootloader/partitions ani połączonego obrazu całej pamięci flash.
6. Po poprawnym zapisie następuje restart. Sterownik wraca do normalnej pracy z zachowaną
   konfiguracją domowego Wi-Fi i brokera. Hasło następnej sesji będzie inne.

Hotspot wyłącza się po 10 minutach; trwający upload może dokończyć pracę (limit przesyłania
120 s, timeout odbioru 5 s). Kolejny gest 10 s odnawia okno sesji. AP+STA zachowuje
połączenie domowe, jeśli jest dostępne; radio współdzieli kanał. AP obsługuje jednego klienta.
Nie jest uruchamiany automatycznie przy awarii domowego Wi-Fi.

Strona jest dostępna wyłącznie przez adres interfejsu AP, nie przez adres STA. Hasło WPA2
jest losowe, drukowane tylko na LCD. Upload wymaga dodatkowego losowego tokenu sesji
z lokalnej strony (bez CORS); zapis i instalacja wykorzystują nieaktywny slot OTA.
Sprawdzany jest nagłówek, projekt, inna wersja, rozmiar i integralność obrazu przez ESP-IDF.
Błąd/przerwany upload nie przełącza partycji startowej. Lokalna i zdalna aktualizacja
mają wspólną blokadę. Dotychczasowy rollback po nieudanym starcie pozostaje aktywny.

Działający LCD jest potrzebny do odczytania hasła; przy jego usterce pozostaje USB
lub istniejąca ścieżka MQTT/HTTPS. Nie zapisujemy hasła AP w EEPROM, NVS ani logach MQTT.
Pierwsze wgranie 0.9.0 wymaga USB albo dotychczasowego zdalnego OTA — 0.8.2 nie ma hotspotu.

## Walidacja sprzętowa do wykonania

- Oscyloskop: niezależność częstotliwości 4/5 od 1–3, duty 0 przy starcie/błędzie,
  czasy ON/OFF i fade przy granicach, ON/OFF bez modulacji.
- HA: 10 nowych encji, komendy odrzucone przy sprzecznych ograniczeniach, reconnect.
- DS1307: odtworzenie v1, zapis v2, retencja po zaniku zasilania i przerwany zapis.
- AP z działającym i niedostępnym STA; gest 10 s i zwykły klik LCD; timeout 10 min.
- Laptop/telefon: odczyt strony, upload nowej wersji; błędny plik/projekt/ta sama wersja,
  przerwany transfer, odmowa przez interfejs STA i równoległa próba OTA MQTT.
- Przyciski/PWM podczas transferu; rollback po przerwanym pierwszym starcie nowego obrazu.

Źródła API: [LEDC ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32/api-reference/peripherals/ledc.html),
[OTA ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32/api-reference/system/ota.html).
