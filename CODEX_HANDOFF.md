# Handoff dla Codex

## Aktualny stan (2026-10-12)

Urzadzenie i publiczny Release nadal sa `0.10.3`. Lokalny kod ma robocza
poprawke po diagnozie `docs/CODEX_MQTT_HTTPS_0.10.3_DIAGNOZA_I_ZADANIE.md`:
28 SUBSCRIBE pojedynczo po SUBACK, oddzielny birth/Discovery z backoff,
retained migawka niezalezna od awarii JSON, diagnostyka przyczyny/RAM/outboxu
i PUBACK. Po udanym buildzie Discovery jest buforowane do kolejnego `apply`.
Pelny payload 39 encji jest testowany hostowo: 16 211 B domyslnie, 20 261 B
przy maksymalnym prefiksie (bufor 24 576 B). cJSON zadaje szczytowo 51 998 B
w 1494 alokacjach na hoscie; **heap ESP32 nadal wymaga pomiaru**. Dodano
cztery etapy logu heap, timeout PUBACK 35 s, licznik TLS i ograniczony widok
logow w przegladarce. HIL 30-60 min jeszcze nie ma. `PROJECT_VER` pozostaje
`0.10.3`, a roboczego obrazu nie publikowano, nie wysylano OTA i nie flashowano.
`scripts/check.sh` PASS (163 pliki UTF-8, testy hostowe i build); osobny
`pio run -e esp32doit-devkit-v1` PASS. Statyczny RAM 118 736 B, flash
1 384 825 B. Wyniki i procedura HIL sa w
`docs/VALIDATION_MQTT_DISCOVERY_0.10.3.md`.
Nie oznaczac tego patcha tagiem `v0.10.3` ani `v0.10.4` przed walidacja.
Plan i pozostale testy: `PLANS.md`, `docs/WEB_DIAGNOSTICS.md` oraz
`docs/VALIDATION_MQTT_DISCOVERY_0.10.3.md`.

Biezaca wersja zrodel i urzadzenia to `0.10.3`. `0.10.1` na urzadzeniu
wpadla w bootloop
(`stack overflow in task main`). W `0.10.2` uzytkownik potwierdzil dzialanie
HTTPS, zmiany hasla, Wi-Fi, MQTT i NTP bez widocznego restartu. `0.10.3`
usuwa cykliczne ostrzezenie biblioteki przy pustej magistrali 1-Wire
oraz ogranicza jednoczesne sesje HTTPS po bledzie alokacji MbedTLS -0x008D.
Lokalny skrypt `scripts/publish_ota.py` moze opublikowac wydanie przez Git
Credential Manager bez sekretu `RELEASE_REPO_TOKEN`; workflow tagu weryfikuje
wtedy juz opublikowany Release. Samodzielna publikacja przez Actions nadal
wymaga wasko ograniczonego sekretu. Release `v0.10.3` opublikowano i
zweryfikowano; GitHub Actions zakonczyl sie sukcesem. OTA z 0.10.2 przeszlo
po resecie EN; status MQTT: `idle`, wersja `0.10.3`, `ESP_OK`. Pierwsza proba
bez resetu ujawnila `mbedtls_ssl_setup -0x008D` w 0.10.2. Nie wgrywac
starego obrazu `0.10.1` ani nie
zakladac, ze log 0.10.2 potwierdzil margines stosu lub dluzsza stabilnosc.

## v0.9.1 - obraz do OTA przez GitHub

Wersja `PROJECT_VER` zostala podniesiona do `0.9.1` dla poprawki czerwonego POWER pilota.
Obraz OTA nalezy zbudowac z profilu `esp32doit-devkit-v1-ota` i opublikowac jako
`firmware.bin` w publicznym GitHub Release z tagiem `v0.9.1`. Sam build nie publikuje
Release ani nie aktualizuje ESP32. Przed komenda OTA uploader wymaga lokalnego
`secrets.ota.json` oraz zgodnosci SHA-256 artefaktu HTTPS z aktualnym buildem.
Nie wykonano jeszcze publikacji ani testu sprzetowego tej wersji.

## 2026-10-07 — czerwony POWER pilota

Czerwony POWER (NEC `0x45`) przełącza tylko wybrany kanał 1–5, zachowując jego jasność.
Przy celu `WSZYSTKIE` nie zmienia żadnego kanału i prosi na LCD o wybór kanału.
Przycisk RGB ON/OFF (`0x47`) nadal przełącza wybrany cel, w tym wszystkie pięć kanałów.
Usunięto dawny mechanizm wspólnego POWER OFF i odtwarzania całej grupy.
`check.sh` przeszedł (build ESP32 i testy hostowe). Zmiana nie dotyczy GPIO, polaryzacji
ani zapisanych ustawień; HIL pozostaje do wykonania.

## v0.9.0 — PWM4/5 i hotspot lokalnego OTA

Dodano 10 ustawień HA dla kanałów 4/5, osobne timery, walidację ograniczeń i rekord
DS1307 v2 z odczytem v1. Przytrzymanie przycisku LCD przez 10 s uruchamia hotspot
WPA2 ze stroną uploadu firmware i danymi dostępu na LCD; sesja trwa 10 min.
Nie zmienia konfiguracji STA/MQTT ani GPIO. Zdalne OTA nadal działa.
[Instrukcja, zakresy, pamięć i lista testów sprzętowych](docs/PWM_OTA_0.9.0.md).
Kod wymaga testów na ESP32; nie wykonano wgrania ani publikacji wydania.
Wyniki kompilacji i testów: PLANS.md.



## Diagnostyka sieci v0.8.2 — zakres końcowy

Po odtworzeniu konfiguracji logowane są Wi-Fi (SSID, skonfigurowane IP/maska/brama/DNS),
NTP/TZ i MQTT (bez hasła i bez userinfo/path/query URI), keepalive oraz wersja protokołu.
Po GOT_IP pojawiają się rzeczywiste IP/maska/brama/DNS, BSSID i RSSI.
Klient MQTT jest przygotowywany podczas apply, ale startuje dopiero po GOT_IP i ustawieniu DNS,
w zadaniu MQTT (sprawdzenie co najwyżej 200 ms). Event loop nie uruchamia operacji MQTT.
Nie powstaje drugi klient po reconnect Wi-Fi; działa dotychczasowy reconnect ESP-MQTT.
Błąd samego startu jest ponawiany co 5 s, tylko przy gotowej sieci.
MQTT_EVENT_ERROR wypisuje typ, kody TLS, flagi certyfikatu, odpowiedź brokera i socket errno.
Kompilacja i testy PASS; nowa logika sieciowa wymaga testów na ESP32.
[Walidacja, pliki i instrukcja zbierania logu](docs/VALIDATION_0.8.2.md).

## v0.8.2 — diagnostyka startowa, 2026-09-25

Automatyczny log przy zastosowaniu MQTT pokazuje broker (tylko authority, bez userinfo/path/query),
użytkownika, obecność hasła, topic/discovery/client ID i wynik przygotowania klienta; wynik startu jest logowany osobno po GOT_IP.
Po uzyskaniu IP wypisywane są runtime DNS-y, maska, brama, tryb adresowania i stan NTP.
EEPROM raportuje flagi configured oraz etap ewentualnego błędu restore/apply.
Nie zapisuje nowych ustawień i nie zmienia GPIO. Do odczytu wystarcza USB, 115200, reset EN.
W logu użytkownika potwierdzono uruchomienie 0.8.1 i poprawne odtworzenie EEPROM.
Wersja 0.8.2 wymaga wgrania i zebrania nowego logu; nie naprawia jeszcze timeoutu brokera.
Wyniki: [PLANS.md](PLANS.md).



## v0.8.1 — diagnostyka MQTT, 2026-09-24

Dodano przełącznik Debug - logi MQTT, limit sesji 10 minut, bufor RAM 32×256 B
oraz encje przyczyny restartu, uptime i pominiętych wpisów. Domyślnie OFF po każdym
restarcie, niezależnie od DS1307. Nie zapisuje logów do pamięci trwałej ani RTC.
[Instrukcja podglądu w HA i ograniczenia](docs/DEBUG_MQTT.md).
Stan kontroli: [PLANS.md](PLANS.md). Nie wykonano wgrania ani HIL v0.8.1.
Poniższe wyniki i opisy v0.8.0/v0.7.2 pozostają historyczne.



## Aktualizacja v0.8.0 — 2026-09-24

Bieżący kod rozszerza v0.7.2 o mapowanie KEY w HA, niezależną automatykę PIR1/2,
ustawienia w NVRAM DS1307, stabilne wskazania PWM przy ON/OFF, kontrolę DDRAM LCD
oraz podświetlenie dzień/noc z HA i czasowy priorytet przycisku także przy Stale OFF.
Domyślnie KEY mają mapę 1:1, czasy PIR wynoszą 0. Dla korytarza ustawić w HA
KEY1 = Kanaly 1+2, KEY2 = Wylaczony, czasy PIR > 0 i oddzielne jasności kanałów.
Nowe ustawienia nie zapisują się do EEPROM ani NVS. Jasność i ON/OFF pozostają w RAM.
DS1307 obsługuje wyłącznie NVRAM, bez rejestrów czasu.

Instrukcje: [sterowanie](docs/LOCAL_CONTROL.md), [LCD i automatyzacja HA](docs/LCD_CONTROL.md).
Walidacja i dalsze testy: [PLANS.md](PLANS.md), [raport](docs/VALIDATION_0.8.0.md).
PlatformIO espressif32 7.0.1 używa ESP-IDF 6.0.1. Na tym laptopie testy hostowe:
`python scripts/test_host_bcc.py`. Pełna kontrola Bash: `HOST_TEST_COMPILER=bcc64 ./scripts/check.sh`
z PlatformIO i Pythonem w PATH. Nie wykonano wgrania ani HIL v0.8.0.
Poniższy opis v0.7.2 jest historyczną bazą; brak automatyki PIR/DS1307 dotyczy tej bazy.


## Cel

Kontynuuj rozwój pięciokanałowego sterownika LED na klasycznym ESP32. Aktualny target to DOIT ESP32 DevKit V1, a projekt używa natywnego ESP-IDF, C++17 i PlatformIO. Nie używaj Arduino ani ESPHome.

## Stan zastany

Wersja robocza `v0.7.2` zawiera:

- pięć niezależnych kanałów PWM LEDC;
- pięć przycisków monostabilnych zwieranych do GND;
- debounce przez polling;
- czysty, testowany hostowo `ButtonDebouncer`;
- czysty, testowany hostowo `ButtonGesture`;
- czysty, testowany hostowo `ChannelState`;
- czysty, testowany hostowo `DutyTransition`;
- czysty, testowany hostowo `StatusIndicator`;
- programowy, nieblokujący fade 250 ms;
- short press przełączający kanał po zwolnieniu;
- long press od 500 ms regulujący jasność 1–100% w około 4 s;
- wbudowaną LED diagnostyczną na GPIO2: okres 1 s normalnie, stałe świecenie po resecie watchdog;
- task watchdog 5 s dla zadania sterującego oraz idle obu rdzeni;
- LCD 20x4 HD44780 przez PCF8574AT pod adresem 7-bitowym `0x3F`;
- dashboard LCD odświeżany najwyżej raz na sekundę i osobne zadanie dla operacji I²C;
- niezależne wykrywanie obecności klientów I²C oraz reset sprzętowego automatu bez usuwania pozostałych uchwytów po timeoutach;
- przycisk podświetlenia na GPIO19, aktywny LOW, z zewnętrznym 10 kOhm i wewnętrznym pull-upem;
- wspólnego właściciela I2C0 na GPIO21/GPIO22 dla LCD, INA228 i AT24C08N;
- trzeci wiersz LCD bez pola PWM, temperaturę ze znakiem HD44780 `0xDF` oraz rotację status/uptime/Wi-Fi/IP/MQTT/czas co 2 s;
- Wi-Fi STA konfigurowane z konsoli UART0 bez komend AT;
- konfigurację SSID, hasła, DHCP/statycznego IPv4 i DNS w RAM, opcjonalnie odtwarzaną z AT24C08N;
- reconnect timerem co 5 s i okresowy odczyt RSSI w dBm;
- SNTP `pool.ntp.org` oraz czas Warszawy z automatycznym CET/CEST;
- statyczną kolejkę dziesięciu `ControlCommand`, której jedynym konsumentem jest `LedController`;
- klienta ESP-MQTT z konfiguracją UART opcjonalnie odtwarzaną z AT24C08N;
- ścisły parser JSON `state`, `brightness` i `transition` testowany także na hoście;
- retained availability, stan pięciu kanałów i dwóch wejść PIR, synchronizację po reconnect i QoS 1;
- jeden komunikat Home Assistant MQTT Device Discovery tworzący pięć neutralnie nazwanych encji `light` i dwa `binary_sensor` PIR;
- scentralizowaną tożsamość `Sterownik LED`, producenta `Daniel Parzniewski` i identyfikator `sterownik_led_<MAC>`;
- dwa aktywne stanem HIGH wejścia PIR typu NC z zewnętrznym pull-up 10 kOhm, filtrem styku 25 ms i bez lokalnej automatyki LED.
- czysty, testowany hostowo dekoder NEC Extended i automat mapowania pilota;
- opcjonalny odbiornik RMT RX na GPIO36, domyślnie usunięty z buildu przez `CONFIG_LED_CONTROLLER_IR_NEC=n`;
- wybór pilotem roli BIAŁE/RGB, celu kanał 1–5/WSZYSTKIE, POWER, toggle, jasność, tryb nocny 10%, timer 30 min i fade 0/500/2000 ms;
- dwusekundowe komunikaty pilota w czwartym wierszu LCD, z alarmem mającym wyższy priorytet.
- opcjonalny sterownik INA228 pod adresem `0x40`, kompilowany przez `CONFIG_LED_CONTROLLER_INA228`;
- nominalne skalowanie dla bocznika 10 mOhm: `CURRENT_LSB=25 uA`, `SHUNT_CAL=3277`, szeroki zakres VSHUNT;
- telemetrię prądu, napięcia, mocy, energii i temperatury układu na LCD, w konsoli oraz pięciu encjach Home Assistant;
- odrębną dostępność INA228: brak układu nie wyłącza pięciu świateł, PIR, LCD ani sterowania lokalnego;
- opcjonalny, kompletny magazyn ustawień AT24C08N z providerem klucza, payloadem TLV i automatycznym zapisem;
- trwałą korekcję wskazania prądu INA228 w ppm, bez trwałego zapisu energii;
- opcjonalną magistralę 1-Wire na GPIO4 dla maksymalnie ośmiu DS18B20;
- role temperatury `radiator` i `obudowa`, wybierane krótkim indeksem ze skanu i identyfikowane trwałym ROM-em;
- dwie encje temperatury Home Assistant oraz rotację `R:`/`O:`/`I:` w trzecim wierszu LCD;
- jawny kontrakt, że jasność i stany ON/OFF kanałów LED nie trafiają do EEPROM-u;
- publikację MQTT tylko zmienionych stanów, pełną synchronizację po reconnect lub birth HA oraz kontrolowane ponawianie po zapełnieniu outboxu;
- rozróżnienie na LCD stanu lokalnego `LOKALNIE: OK` od stanu sieciowego, w tym `MQTT: KOLEJKA PELNA`;
- HTTPS OTA sterowane osobnym tematem MQTT, retained status OTA i encję diagnostyczną Home Assistant;
- dwie partycje aplikacji OTA po 1856 KiB oraz rollback nowego obrazu, jeśli nie przepracuje 10 s po uruchomieniu lokalnego kontrolera.
- osobny profil PlatformIO `esp32doit-devkit-v1-ota`, który używa pełnego bazowego `sdkconfig`, sprawdza SHA-256 pliku na serwerze HTTPS i uruchamia OTA przez MQTT.

Stos AT24C08N aktywowany przez `CONFIG_LED_CONTROLLER_AT24C08=y` zawiera:

- adapter Atmel AT24C08N-SU27 jako klient wspólnego I2C0 GPIO21/GPIO22 po stronie 3,3 V translatora;
- dwa sloty po 512 B, strony po 16 B i ACK polling bez stałego opóźnienia;
- rekordy AES-256-GCM przez publiczne API PSA Crypto ESP-IDF 6;
- zapis z unieważnieniem celu, znacznikiem commit na końcu i pełną weryfikacją odczytu;
- losowy klucz AES-256 urządzenia przechowywany w namespace wewnętrznego NVS;
- wersjonowany payload TLV dla zastosowanych Wi-Fi, MQTT, korekcji INA228 i dwóch ROM-ów DS18B20;
- `SettingsService` obsługiwany przez zadanie `LcdService`, jedynego właściciela I2C0;
- automatyczny zapis scalany przez 5 s oraz komendy konsoli `settings ...`;
- brak klucza domyślnego, plaintextowego fallbacku i sekretów w repozytorium.

AT24C08N na PCB został wykryty pod adresami `0x50`-`0x53`. HIL potwierdził zapis zaszyfrowanego rekordu AES-GCM, odtworzenie statycznej konfiguracji Wi-Fi/MQTT i roli DS18B20 po restarcie. `CONFIG_LED_CONTROLLER_AT24C08=n` domyślnie usuwa siedem modułów pamięci z komponentu firmware, natomiast lokalny ignorowany `sdkconfig.esp32doit-devkit-v1` ma `y` dla pełnego obrazu PCB. Brak pamięci nadal jest dozwolony i sprawdzany co 30 s bez alarmu GPIO2. Po poprawnym `wifi apply` albo `mqtt apply` rekord jest zapisywany po 5 s. Jasność i stany LED pozostają wyłącznie w RAM. Nie włączaj automatycznie Flash Encryption ani Secure Boot na płytce deweloperskiej; wymagają osobnego, świadomego procesu provisioningu. Bez Flash Encryption klucz w NVS nie chroni przed łącznym odczytem wewnętrznego flash i EEPROM-u.

Odbiornik IR jest obecnie podłączony testowo. Docelowy tor używa GPIO36, odbiornika TSOP38438 lub zgodnego 38 kHz zasilanego z 3,3 V, zewnętrznego pull-up 10 kOhm, rezystora sygnałowego 1 kOhm oraz filtracji zasilania 100 Ohm + 100 nF + 4,7 uF. GPIO36/VP nie ma wewnętrznego pull-up ani pull-down. `CONFIG_LED_CONTROLLER_IR_NEC=n` pozostaje ustawieniem domyślnym, nie konfiguruje GPIO36 i usuwa źródła RMT z komponentu; lokalny ignorowany `sdkconfig.esp32doit-devkit-v1` ma na czas HIL wartość `y`. Po uruchomieniu zwykłe komendy są ignorowane do naciśnięcia `WHITE MODE`; `RGB MODE` przekazuje pilota przyszłemu sterownikowi RGB+CCT. Oba selektory pozostają zawsze aktywne. ISR RMT kopiuje tylko metadane do statycznej kolejki, a jedynym właścicielem LEDC nadal jest `LedController`.

INA228 na PCB został wykryty pod adresem `0x40`, ponieważ A0/A1 są zwarte do GND; odczyt identyfikatora i pomiar zerowy bez zasilonej szyny +24 V przeszły HIL. Układ współdzieli I2C0 po stronie 3,3 V i mierzy wspólną szynę `+24 V` za bocznikiem high-side. `CONFIG_LED_CONTROLLER_INA228=n` pozostaje ustawieniem domyślnym i usuwa `Ina228.cpp` oraz integrację; lokalny ignorowany `sdkconfig.esp32doit-devkit-v1` ma `y` dla pełnego obrazu PCB. Brak odpowiedzi jest stanem dozwolonym, ponawianym co 30 s. Firmware wymaga bocznika 10 mOhm, `CURRENT_LSB=25 uA` i `SHUNT_CAL=3277`; wartość rzeczywiście obsadzoną należy potwierdzić przed testem z obciążeniem. Korekcja ppm jest zapisywana przy włączonym AT24C08N; `/ALERT` na GPIO23, progi ochronne i trwałe sumowanie energii nie są zaimplementowane.

GPIO4 obsługuje opcjonalną magistralę DS18B20 w połączeniu trzyprzewodowym z zewnętrznym pull-up DQ 4,7 kOhm do 3,3 V. `CONFIG_LED_CONTROLLER_DS18B20=n` pozostaje ustawieniem DNP, a lokalny pełny obraz ma `y`. Usługa używa `espressif/onewire_bus` 1.1.1, wykrywa do ośmiu czujników, rozpoczyna wspólną konwersję co 2 s, czeka 800 ms według ticka i odczytuje po jednym scratchpadzie na obieg bez blokujących opóźnień. `temp scan` i `temp status` pokazują krótkie indeksy; `temp assign radiator N` oraz `temp assign obudowa N` przypisują rzeczywisty ROM. Przy aktywnym AT24C08N oba ROM-y są częścią zaszyfrowanego rekordu, bez pamięci istnieją tylko w RAM. Brak czujnika nie ustawia GPIO2 ani nie blokuje sterowania. Telemetria nie realizuje jeszcze progów ani wyłączania kanałów.

DS1307 pozostaje nieobsadzonym DNP. Firmware RTC nie jest jeszcze zaimplementowany; po ewentualnym dodaniu ma być osobną opcją kompilacji. NTP pozostaje nadrzędnym źródłem czasu, a RTC jest uzasadniony tylko dla lokalnego harmonogramu wymagającego rzeczywistej godziny bez Wi-Fi.

Pinout nie zawiera wspólnego sygnału `ENABLE`. Firmware ustawia wszystkie piny PWM LOW przed konfiguracją LEDC, a następnie konfiguruje pięć kanałów z duty 0. Stan podczas resetu i przed `app_main()` musi być zabezpieczony sprzętowo w torze mocy.

Wejścia PIR mają stan normalny LOW. Ruch, otwarcie styku NC albo przerwanie przewodu daje HIGH. GPIO34 i GPIO35 są wejściowe i nie mają wewnętrznych rezystorów podciągających; `PirInput` jawnie wyłącza pull-up i pull-down. Kontroler próbkuje je co 5 ms i przyjmuje zmianę po 25 ms. To tylko filtr styku. HIGH/LOW trafia do HA jako retained `ON`/`OFF`, ale w tej wersji nie zmienia żadnego kanału LED.

GPIO2 jest świadomym wyjątkiem od wcześniejszej listy pinów zakazanych. Jest przeznaczone wyłącznie dla wbudowanej LED diagnostycznej i jako pin strapujący nie może dostać dodatkowego zewnętrznego obciążenia. Firmware konfiguruje je dopiero po `app_main()`.

## Pilot IR

Pilot wysyła bajty `[0x87, 0x5A, CMD, ~CMD]` LSB-first. Nie używaj pełnych kodów `0xE15A...` jako kontraktu logiki; są jedynie innym sposobem prezentacji bitów. `NecDecoder` przyjmuje impulsy niezależne od ESP-IDF, `IrRemoteMapper` sprawdza adres i komplement, a `IrReceiverService` jest wyłącznie adapterem RMT. NEC Repeat jest ważny tylko do 250 ms od poprzedniej ramki jasności góra/dół, ale zaczyna wykonywać kolejne kroki dopiero po 500 ms ciągłego przytrzymania. Wcześniejsze ramki utrzymują ciągłość bez zmiany jasności, więc krótki klik daje jeden krok 5%.

Mapowanie: POWER `0x45`, toggle celu `0x47`, noc `0x40`, jasność + `0x16`, poprzedni cel `0x09`, timer `0x5A`, następny cel `0x07`, jasność − `0x42`, zarezerwowane sceny `0x43/0x44`, wybór RGB `0x15`, cykl fade `0x4A`, wybór białych LED `0x1C`. Początkowy cel to kanał 1, a czasy fade to 0/500/2000 ms z wartością początkową 500 ms. Stan wyboru, trybu nocnego i timerów jest tylko w RAM. Zmiany kanałów są publikowane do MQTT przez istniejącą migawkę, bez osobnej ścieżki HA.

## Fade na klasycznym ESP32

Klasyczny ESP32 nie obsługuje `ledc_fade_stop()`, dlatego firmware nie używa sprzętowego fade LEDC. `DutyTransition` wylicza duty programowo, a jedyny task kontrolera zapisuje je sekwencją `ledc_set_duty()` i `ledc_update_duty()`.

Nie używaj tu `ledc_set_duty_and_update()`: w ESP-IDF 6.0.1 ten helper wymaga zainstalowanej usługi fade i bez niej zwraca `ESP_FAIL`. Hotfix v0.2.1 usuwa wywołanie helpera po teście HIL, w którym powtarzany błąd logowania zagłodził zadania idle i wywołał reset watchdog. Błąd aktualizacji LEDC jest teraz zatrzaskiwany, kanał przechodzi do LOW, a kontroler nie ponawia wadliwej operacji w każdej iteracji.

## LCD

`LcdService` ma własne zadanie FreeRTOS o niższym priorytecie. Kontroler publikuje do jednoelementowej kolejki tylko najnowszą migawkę, maksymalnie raz na 1000 ms, i nigdy nie wykonuje transmisji I²C. `LcdDisplay` inicjalizuje HD44780 za pomocą maszyny stanów opartej na ticku, bez `vTaskDelay()` i aktywnego oczekiwania. Każda ramka jawnie adresuje i zapisuje wszystkie cztery wiersze, aby naprawiać ewentualne zakłócenie DDRAM. Każda transmisja ma timeout 20 ms.

Brak lub NACK pojedynczego adresu I²C jest stanem lokalnym klienta: PCF8574AT, AT24C08N i INA228 są ponawiane niezależnie, nie unieważniają nawzajem swoich uchwytów i nie zapalają GPIO2. `ESP_ERR_TIMEOUT` oznacza awarię wspólnej magistrali; `LcdService` wykonuje wtedy `i2c_master_bus_reset()` bez usuwania I2C0, a GPIO2 świeci stale do pierwszej poprawnej transakcji. Sterowanie PWM, przyciski i watchdog pozostają aktywne w obu przypadkach.

Po powrocie PCF8574AT `LcdService` wykonuje pełne czyszczenie, dwa rozdzielone przebiegi inicjalizacji HD44780, przywraca żądany stan podświetlenia i zapisuje dwie pełne ramki. Wielokrotnie ponawiane sondowanie nie zalewa UART-u: brak i odzyskanie LCD są logowane po jednym razie. Temperatury używają pojedynczego znaku `0xDF` z ROM-u HD44780 bezpośrednio przed literą `C`.

Mapa PCF8574AT P0=RS, P1=RW, P2=E, P3=podświetlenie, P4-P7=D4-D7, polaryzacja podświetlenia i przycisk GPIO19 zostały potwierdzone HIL. Adres `0x7E` jest adresem 8-bitowym zapisu, dlatego nowe API master I²C ESP-IDF otrzymuje `0x3F`. Pełne odrysowanie 4x20 raz na sekundę utrzymuje poprawną treść również po zmianie GPIO13. Test odłączenia SCL potwierdził izolację awarii i działanie sterowania lokalnego.

V0.3.3 wykrywa także reset ekspandera przez odczyt stanu PCF8574AT, wykonuje dwa rozdzielone przebiegi inicjalizacji HD44780 i wymaga dwóch pełnych ramek przed skasowaniem błędu. UART potwierdził wielokrotne odzyskanie po odłączaniu bez resetu MCU. Sprzętową przyczyną niepoprawnych znaków był potwierdzony zimny lut DB6 LCD; połączenie zostało poprawione. V0.3.4 przenosi własność magistrali do `I2cBus`, pozostawiając `LcdDisplay` i przyszły `At24c08` jako klientów jednego I2C0.

## Wi-Fi i NTP

`WifiService::initialize()` przed startem kontrolera przygotowuje tylko statyczny mutex, timery i regułę strefy czasowej. Pełny stos ESP-NETIF/Wi-Fi/SNTP jest uruchamiany dopiero po `LedController::start()`. Błąd sieci nie przechodzi przez `ESP_ERROR_CHECK`, nie zatrzaskuje GPIO2 i nie blokuje sterowania lokalnego.

Wersja testowa ma `CONFIG_ESP_WIFI_NVS_ENABLED=n`, ustawia `wifi_init_config_t.nvs_enable=0` oraz `WIFI_STORAGE_RAM`. Wewnętrzny NVS jest inicjalizowany dla kalibracji PHY i klucza szyfrowania ustawień. Bez AT24C08N reset usuwa SSID, hasło, IPv4 i DNS; z aktywną pamięcią zastosowana konfiguracja jest odtwarzana z zaszyfrowanego rekordu. `ServiceConsole` działa na UART0/COM4 115200 i rejestruje jedno polecenie `wifi` z podkomendami `scan`, `ssid`, `password`, `mode dhcp`, `static`, `dns`, `apply`, `status`, `disconnect` i `clear`. Hasło jest czytane bez echa bezpośrednio z UART, poza historią REPL, a bufory tymczasowe są zerowane. Monitor PlatformIO musi używać `monitor_eol = CR`, ponieważ UART REPL ESP-IDF oczekuje pojedynczego `CR`; domyślne `CRLF` daje drugi pusty prompt i przekazuje zaległe `LF` do promptu hasła.

Po `IP_EVENT_STA_GOT_IP` uruchamiany jest `pool.ntp.org`. `TZ=CET-1CEST,M3.5.0/2,M10.5.0/3` odpowiada Warszawie i ostatnim niedzielom marca oraz października. Czwarty wiersz LCD rotuje co 2 s status, uptime, RSSI/SSID, IP i lokalną datę z czasem. Połączona sieć jest formatowana jako `-NNdBm SSID`, bez prefiksu `WIFI`, aby pozostawić więcej miejsca na nazwę. Brak synchronizacji pokazuje `CZAS: BRAK NTP`; alarm LCD lub watchdog nadal blokuje rotację.

Hotfix `0.5.1-test` usuwa samozakleszczenie wykryte przy pierwszym połączeniu z rzeczywistym AP. Handler `WIFI_EVENT_STA_CONNECTED` trzymał nierekurencyjny mutex i wywoływał `refreshLinkDetails()`, które próbowało przejąć go ponownie. Ówczesny task `corridor_control` blokował się następnie na odczycie statusu do LCD i po około 6 s następował Task WDT. Bieżący `LedController` używa `tryGetStatus()` z zerowym czasem oczekiwania oraz ostatniej poprawnej kopii dla LCD; awaria lub kontencja mutexu Wi-Fi nie może zatrzymać pętli lokalnej.

## MQTT i Home Assistant

`MqttService` jest przygotowywany przed kontrolerem, ale jego zadanie startuje dopiero po `LedController::start()`. Bez konfiguracji pozostaje bezczynne. Etap testowy obsługuje tylko `mqtt://`, ponieważ źródło CA dla `mqtts://` nie zostało jeszcze wybrane. Konfiguracja robocza `uri`, `username`, `password`, `topic` i `discovery` znajduje się w RAM, a przy `AT24C08=y` zastosowana kopia jest szyfrowana w EEPROM-ie. `mqtt password` czyta sekret bez echa, a `mqtt status` nie pokazuje go.

Oficjalny klient ESP-MQTT używa MQTT 3.1.1, reconnectu 5 s, timeoutu 3 s, keepalive 30 s, LWT `offline`, retained QoS 1 i identyfikatora `sterownik_led_<MAC>`. Domyślny topic prefix to `sterownik-led`. Po połączeniu subskrybuje pięć tematów komend, temat OTA i `<discovery-prefix>/status`, publikuje `online`, jeden payload device discovery oraz pełną migawkę. W normalnej pracy wysyła tylko pola, których wartość się zmieniła. Po reconnect albo birth HA unieważnia lokalny tracker i ponownie wysyła pełny retained stan. Nieudane pole jest ponawiane co 500 ms bez ponownego kolejkowania pól już przyjętych przez outbox.

W `v0.6.1-test` rotacja czwartego wiersza zawiera stronę stanu MQTT. Kontroler pobiera status MQTT przez nieblokujące `tryGetStatus()` i zachowuje ostatnią poprawną kopię. Podczas aktywnego fade LCD prezentuje raz na sekundę jasność wyliczoną z rzeczywiście przyłożonego duty; stan publikowany do Home Assistant pozostaje wartością zadaną, aby nie generować serii komunikatów podczas przejścia.

Tożsamość discovery jest zdefiniowana w `DeviceIdentity.hpp`: nazwa `Sterownik LED`, producent `Daniel Parzniewski`, model `ESP32 5-channel PWM`, origin `sterownik-led-firmware`, encje `Kanał 1`–`Kanał 5`, `Wejście PIR 1`, `Wejście PIR 2` oraz `unique_id` wyprowadzony z nowego identyfikatora. PIR-y mają platformę `binary_sensor`, klasę `motion` i tematy `<topic-prefix>/<device-id>/pir/N/state`. Nazwy pomieszczeń przypisuje użytkownik w Home Assistant. Stara instalacja `led_korytarz_<MAC>` istnieje wyłącznie w tymczasowym HA na maszynie wirtualnej i nie jest migrowana ani usuwana przez firmware; świeża instalacja HA/Mosquitto na Raspberry Pi nie odziedziczy jej bez przywrócenia kopii.

Callback MQTT odrzuca fragmenty, retained commands, zły JSON, nieznane lub powtórzone pola i błędne zakresy. Poprawną komendę wkłada bez oczekiwania do kolejki kontrolera i nigdy nie wywołuje LEDC. Długie przytrzymanie przycisku odracza komendę tego samego kanału. Błąd brokera, uwierzytelnienia lub kolejki jest logowany, ale nie zatrzaskuje GPIO2 i nie zatrzymuje sterowania lokalnego.

Zależności są przypięte w `src/idf_component.yml`: `espressif/mqtt` 1.0.0 i `espressif/cjson` 1.7.19~2 dla ESP-IDF 6.0.x. PlatformIO używa `board_build.partitions = partitions_ota_4mb.csv`: NVS pozostaje w `0x9000..0xEFFF`, a `ota_0` i `ota_1` mają po 1856 KiB. Pierwsza migracja z tabeli `factory` wymaga pełnego uploadu UART obejmującego bootloader, nową tablicę, `ota_data_initial.bin` i aplikację. Nie wykonuj `erase_flash`, jeżeli ma zostać zachowany klucz AES w NVS. Następne aktualizacje przyjmuje temat `<topic-prefix>/<device-id>/ota/set` z surowym adresem `https://`; retained command jest odrzucana. Publiczny bundle CA nie ufa lokalnym certyfikatom self-signed, a bez Secure Boot projekt i wersja obrazu są kontrolą zgodności, nie kryptograficznym podpisem producenta.

## Zadanie startowe

1. Uruchom `./scripts/check.sh`.
2. Napraw najpierw błędy kompilacji i ostrzeżenia.
3. Nie zmieniaj GPIO ani polaryzacji bez aktualizacji `src/BoardConfig.hpp` i `docs/HARDWARE_CONTRACT.md`.
4. Nie używaj GPIO0, GPIO5, GPIO12 ani GPIO15. GPIO2 pozostaw wyłącznie dla LED diagnostycznej.
5. Nie zmieniaj kontraktu LCD ani PIR bez aktualizacji dokumentacji i ponownego testu HIL; nie dodawaj mapowania PIR na kanały bez rozstrzygnięcia reguł automatyki.
6. Zachowaj lokalne sterowanie niezależne od Wi-Fi, MQTT, NVS i OTA.

`scripts/check.sh` buduje firmware oraz uruchamia testy CMake/CTest. Na bieżącej stacji Windows testy hostowe korzystają z Visual Studio Build Tools 2022, ponieważ PlatformIO `native` nie ma dostępnego systemowego GCC.

Aktualny zestaw zawiera 45 scenariuszy hostowych. Oprócz wcześniejszych przypadków obejmuje tracker publikacji różnicowej MQTT, znaczenie lokalnego statusu LCD, ostrzeżenie o pełnym outboxie oraz walidację URL OTA. Testy hostowe nie zastępują ESP-MQTT, HTTPS OTA, rollbacku bootloadera, PSA AES-GCM, RMT, LEDC, GPIO, I2C ani 1-Wire. AT24C08N, jeden DS18B20 i komunikacja z INA228 mają częściowy HIL opisany na końcu pliku; rzeczywiste OTA nadal ma status `NOT RUN`.

Upload i monitor są przypięte do COM4, z protokołem `esptool` i monitorem 115200 bit/s. `platformio.ini` ustawia `monitor_dtr=0` i `monitor_rts=0`, lecz na obecnym konwerterze USB-UART otwarcie lub zamknięcie portu przez terminal zewnętrzny nadal może wywołać `POWERON_RESET`. Przy `AT24C08=n` ustawienia Wi-Fi/MQTT nadal znikają po takim resecie; przy `y` i poprawnie zapisanej pamięci mają zostać odtworzone. Sam build działa bez podłączonej płytki; przed uploadem sprawdź, czy COM4 jest rzeczywiście obecny.

V0.3.4 została wgrana na COM4. Ośmiosekundowy log startowy potwierdził pięć kanałów OFF, GPIO19 HIGH, PCF8574AT `0x3F`, podwójną inicjalizację LCD zakończoną po około 800 ms oraz brak błędu I2C lub resetu. Test HIL potwierdził prawidłowy trzeci wiersz bez pola PWM i rotację status/uptime co 2 s.

Hotfix z 2026-08-16 wycisza surowy tag ESP-IDF `i2c.master`, który przy niedostępnej magistrali drukował setki powtarzanych timeoutów i sklejał tekst w UART. Nie zmienia to obsługi błędów: wszystkie wyniki `esp_err_t` trafiają do `LcdService`, który loguje pierwszy błąd raz, utrzymuje LED alarmową i nadal próbuje odzyskać magistralę co 2 s. HIL ciszy UART po odłączeniu LCD pozostaje do wykonania po wgraniu nowego obrazu.

Ten sam hotfix przenosi wyłącznie aplikacyjny RX konsoli UART0 z GPIO3 na wolny GPIO39/VN; TX pozostaje na GPIO1. Powodem jest konflikt dwóch wyjść push-pull na GPIO3: TX wbudowanego CP2102 i TX zewnętrznej przejściówki używanej przy zasilaniu PCB z `EXT`. GPIO39 wymaga pull-upu 10 kOhm do 3,3 V, a `TXD` adaptera jest prowadzony przez 1 kOhm. ROM bootloader nadal używa standardowego GPIO3, więc upload przez wbudowane USB pozostaje możliwy; po starcie aplikacji bezpośrednie USB jest tylko wyjściem logów.

Końcowy `scripts/check.sh` po obu poprawkach ma status PASS: 96 plików UTF-8 bez BOM, build bez ostrzeżeń i 43/43 scenariusze hostowe. Pełny wariant `AT24C08+IR+INA228+DS18B20` używa 58 188 B RAM oraz 1 103 561 B flash. HIL GPIO39 oraz ciszy konsoli przy niezasilonej magistrali ma status `NOT RUN`.

V0.5.0-test z Wi-Fi/NTP została zbudowana, wgrana na COM4 i uruchomiona. Pełna kontrola potwierdziła 65 plików UTF-8 bez BOM, build bez ostrzeżeń i 24 testy hostowe. Końcowy obraz używa 38 944 B RAM oraz 901 565 B flash, czyli 86,0% partycji aplikacji 1 MiB.

Automatyczny reset do bootloadera na tej płytce dwukrotnie nie zadziałał (`Invalid head of packet (0x65)`); upload działa po ręcznej sekwencji: trzymać BOOT, krótko nacisnąć EN i puścić BOOT po rozpoczęciu połączenia. Końcowy 90-sekundowy HIL potwierdził metadane `0.5.0-test`, pięć kanałów OFF, GPIO19 HIGH, poprawny LCD, wyłączone NVS konfiguracji Wi-Fi, NVS kalibracji PHY bez błędu, działającą konsolę RAM-only i skan 12 sieci. Nie wystąpił reset watchdog ani błąd I2C. Połączenie z prywatnym AP, DHCP/DNS/NTP i widok czasu na LCD nadal wymagają testu bez przekazywania sekretów do repozytorium lub logu.

Pierwszy test połączenia z prywatnym AP uzyskał asocjację, DHCP i RSSI, ale ujawnił opisane wyżej zakleszczenie przed potwierdzeniem NTP. Hotfix `0.5.1-test` został następnie wgrany i przeszedł HIL: DHCP, DNS, RSSI, `pool.ntp.org`, czas Warszawy CEST oraz poprawny LCD działały, a po ponad 90 s GPIO13 nadal sterował kanałem, GPIO2 migał i nie wystąpił Task WDT.

Ostatni zatwierdzony `scripts/check.sh` dla `0.6.0-test` miał status PASS: 71 plików UTF-8 bez BOM, wszystkie 26 scenariuszy hostowych i build bez ostrzeżeń. Obraz używał 47 264 B RAM i 1 035 345 B flash z partycji 1 536 000 B. Pełny obraz, bootloader i tablica partycji zostały poprawnie wgrane na COM4 i zweryfikowane przez esptool.

Końcowy HIL MQTT/Home Assistant ma status PASS. Potwierdzono stan `polaczone` z brokerem `192.168.0.115:1883`, sterowanie kanałami 1, 2 i 5 oraz jasnością kanału 1 z interfejsu HA, OFF, programowy `transition`, aktualizację LCD i zwrotną synchronizację zmiany wykonanej lokalnie na kanale 1. Po EOF próby reconnect następowały co około 5 s, lokalne ściemnianie działało podczas awarii, a klient wrócił po uruchomieniu brokera i wykonał następną komendę HA bez resetu ESP32. Końcowy status pokazał 2 połączenia, 13 przyjętych i 0 odrzuconych komend; GPIO2 nadal migało co sekundę.

`0.6.1-test` została wgrana i skonfigurowana w jednej sesji terminala. HIL potwierdził kartę `Sterownik LED`, producenta, wersję oraz pięć neutralnych encji. Brak encji PIR w tym obrazie był prawidłowy. Ponowne potwierdzenie próbek rzeczywistej jasności LCD podczas długiego przejścia pozostaje do wykonania razem z HIL następnej wersji.

Pełny `scripts/check.sh` dla `0.6.1-test` ma status PASS: 72 pliki UTF-8 bez BOM, wszystkie 28 scenariuszy hostowych i build bez ostrzeżeń. Obraz używa 47 264 B RAM i 1 036 181 B flash z partycji 1 536 000 B. Kontrola napisów ELF potwierdziła wersję, `Sterownik LED`, producenta `Daniel Parzniewski`, `sterownik_led` i `sterownik-led-firmware`; stara tożsamość nie występuje w firmware.

Pełny `scripts/check.sh` dla `0.6.2-test` ma status PASS: 74 pliki UTF-8 bez BOM, wszystkie 29 scenariuszy hostowych i build bez ostrzeżeń. Obraz używa 47 648 B RAM oraz 1 037 793 B flash z partycji 1 536 000 B. Kontrola ELF potwierdziła wersję, obie nazwy PIR i log discovery. Kod dodaje `PirInput[2]`, wspólną migawkę MQTT oraz dwie encje HA; HIL obu GPIO pozostaje do wykonania.

`0.6.2-test` została wgrana na COM4 2026-07-30. PlatformIO zapisało bootloader, tablicę partycji i aplikację, a esptool potwierdził skróty wszystkich trzech obrazów. Ponowna konfiguracja Wi-Fi/MQTT w RAM, potwierdzenie dwóch encji PIR i HIL GPIO34/GPIO35 pozostają do wykonania.

Po dodaniu do `BoardConfig.hpp` wyłącznie kompilacyjnych rezerwacji GPIO4/GPIO23 wykonano ponowny build bez zmiany rozmiaru obrazu i z kompletem testów PASS. Zbędna druga próba uploadu nie weszła w bootloader (`Invalid head of packet 0x49`) i nie rozpoczęła zapisu. Płytka zachowała pierwszy poprawnie zweryfikowany `v0.6.2-test`; stałe rezerwacyjne nie konfigurują GPIO i nie zmieniają działania bieżącego firmware'u.

`0.6.3-test` dodaje opcjonalnego pilota IR. CTest ma 35 scenariuszy PASS. Kontrolny build `CONFIG_LED_CONTROLLER_IR_NEC=y` zakończył się bez ostrzeżeń i używał 49 360 B RAM oraz 1 052 137 B flash. Końcowy domyślny obraz `n` używa 47 832 B RAM i 1 038 029 B flash. Na Windows wykonano ręczny odpowiednik `scripts/check.sh`: 81 plików UTF-8 bez BOM oraz kontrole statyczne mają status PASS; sam skrypt nie wystartował, ponieważ Bash nie jest dostępny w `PATH`. Pierwszy upload i częściowy HIL wykonano 2026-08-05; wynik i poprawka są opisane poniżej.

2026-08-05 naprawiono tor `EN` płytki deweloperskiej. Uszkodzony albo upływowy kondensator `EN-GND` obciążał linię do około 1,4 V; po jego usunięciu rezystancja `EN-GND` wróciła do 10 kOhm, a poziom EN do 3,3 V. Tranzystor automatycznego resetu EN został wcześniej wylutowany, dlatego programowanie odbywa się ręcznie. Do odtworzenia układu pozostaje kondensator 1 uF X7R co najmniej 6,3 V; tranzystora nie montować ponownie bez potrzeby automatycznego DTR/RTS. Ręczny upload bootloadera, tablicy partycji i aplikacji na COM4 zakończył się powodzeniem, a esptool zweryfikował wszystkie skróty.

Pierwszy HIL IR ujawnił cykliczny błąd `rmt_receive`: wartość `signal_range_min_ns=100000` przekraczała limit sprzętowego filtra klasycznego ESP32 wynoszący mniej niż 3187 ns. Próg zmieniono na 2000 ns i zabezpieczono `static_assert`; trwały `ESP_ERR_INVALID_ARG` zatrzymuje tylko zadanie IR, bez wpływu na sterowanie lokalne. Poprawiony wariant został wgrany i potwierdził `WHITE MODE` oraz `RGB ON/OFF` na kanale 1.

Próba jasności wykazała zmianę 8–10% po krótkim kliknięciu mimo deklarowanego kroku 5%. Przyczyną była pierwsza ramka NEC Repeat, która nie była logowana, ale wykonywała drugi krok po około 110 ms. `IrRemoteMapper` utrzymuje teraz ciągłość Repeat, lecz wykonuje powtarzane kroki dopiero po 500 ms przytrzymania. Hotfix używa 49 360 B RAM i 1 052 241 B flash, a 35 scenariuszy hostowych przechodzi; wymaga ponownego uploadu i HIL.

`v0.6.4-test2` dodaje opcjonalną telemetrię INA228. Oba warianty kompilacji zostały sprawdzone bez ostrzeżeń: `INA228=n` używa 51 424 B RAM i 1 052 597 B flash, a pierwszy wariant `INA228=y` używał 52 616 B RAM i 1 059 833 B flash z partycji 1 536 000 B. Testy hostowe obejmują 37 scenariuszy i mają status PASS. Obraz sprzed poprawki discovery został wgrany ręcznie; HIL z rzeczywistym INA228 ma status `NOT RUN`.

`v0.6.5-test` domyka AT24C08N. Dodano losowy klucz AES-256 w NVS, payload TLV Wi-Fi/MQTT/INA, automatyczny zapis po 5 s i komendy `settings`. Jasność i stany kanałów nie są zapisywane. Testy hostowe mają 40/40 PASS. Wariant `AT24C08=n` z IR+INA używa 52 616 B RAM i 1 060 197 B flash, pełny wariant `AT24C08+IR+INA` 54 412 B RAM i 1 077 533 B flash; oba zbudowano bez ostrzeżeń. EEPROM nie jest zamontowany, więc HIL zapisu i AES-GCM ma status `NOT RUN`.

2026-08-08 test HA po ręcznym uploadzie ujawnił brak pięciu encji telemetrycznych. Przyczyną było połączenie dziedziczonego rootowego `availability_topic` z komponentową listą `availability`, których schemat MQTT nie dopuszcza jednocześnie. Root discovery używa teraz jednoelementowej listy `availability`, a sensory INA228 nadpisują ją listą tematów urządzenia i układu w trybie `all`. Pełny wariant po poprawce buduje się bez ostrzeżeń i używa 52 616 B RAM oraz 1 060 197 B flash; ponowny upload i weryfikacja HA pozostają do wykonania.

`v0.7.0` dodaje warunkową obsługę wielu DS18B20 i dwie role użytkowe bez ręcznego przepisywania ROM-u. Role trafiają do istniejącego payloadu AT24C08N, LCD oraz dwóch encji HA. Wariant `DS18B20=n` został zbudowany bez `Ds18b20Service` i komponentu OneWire; używa 56 512 B RAM oraz 1 078 945 B flash. Końcowy wariant `AT24C08+IR+INA228+DS18B20` buduje się bez ostrzeżeń i używa 58 188 B RAM oraz 1 103 377 B flash. Pełny `scripts/check.sh` ma status PASS: 96 plików UTF-8 bez BOM i 43/43 scenariusze hostowe. HIL potwierdził jednego DS18B20 i przypisanie do roli `obudowa`; test odtworzenia roli po restarcie pozostaje otwarty.

Pierwszy HIL zapisu rzeczywistego AT24C08N ujawnił fałszywe `ESP_ERR_TIMEOUT`: przy obudzeniu zadania na granicy 10 ms kod oceniał deadline przed ostatnią próbą ACK. Hotfix `v0.7.0` najpierw sonduje gotowość EEPROM-u i zgłasza timeout tylko po dalszym NACK. Nie dodaje blokującego opóźnienia ani nie zmienia limitu 10 ms. Ponowny zapis pełnej konfiguracji oraz odtworzenie po restarcie wymagają HIL na nowym obrazie.

Automatyczny skan DS18B20 nadal odbywa się co 30 s, aby obsłużyć odpięcie i hot-plug bez resetu. `v0.7.0` nie drukuje jednak identycznego wyniku każdego skanu: INFO pojawia się po ręcznym `temp scan` albo gdy zmieni się liczba lub zestaw ROM-ów.

Końcowy build `v0.7.0` po hotfixach AT24C08N i logowania DS18B20 zakończył się bez ostrzeżeń. Obraz używa 58 188 B RAM i 1 103 713 B flash z partycji 1 536 000 B, a kontrola ELF potwierdza dokładnie wersję `0.7.0` bez starego dopisku testowego. Testy hostowe mają 43/43 PASS, a 96 plików przechodzi kontrolę UTF-8 bez BOM. HIL nowego obrazu pozostaje do wykonania.

HIL `v0.7.0` na docelowej PCB potwierdził następnie pełne odtworzenie zaszyfrowanej konfiguracji: statyczne Wi-Fi oraz lokalny broker MQTT (adresy instalacji pominięto), rekord AT24C08N sekwencji 4 z payloadem 143 B oraz ROM jednego DS18B20 przypisany do roli `obudowa`. INA228 odpowiedział prawidłowym Device ID i stabilnym pomiarem zerowym bez zasilonej szyny +24 V. Test INA pod napięciem i obciążeniem nadal ma status `NOT RUN`.

`v0.7.1` usuwa burzę publikacji MQTT widoczną po zapełnieniu outboxu. Każdy z 17 retained stanów ma osobny fingerprint, więc normalna praca wysyła tylko zmiany; reconnect i birth HA nadal wymuszają pełną synchronizację. Pierwszy błąd kolejki oraz odzyskanie są logowane jednokrotnie, a LCD i `mqtt status` pokazują zaległość. Ogólny napis LCD został zmieniony z mylącego `STATUS: OK` na `LOKALNIE: OK`, ponieważ awaria MQTT nie jest awarią lokalnego PWM, przycisków ani watchdoga.

Ta sama wersja dodaje HTTPS OTA w osobnym zadaniu oraz tabelę `partitions_ota_4mb.csv` z dwoma slotami po 1856 KiB. Końcowy build pełnego wariantu zakończył się bez ostrzeżeń i użył 59 504 B RAM oraz 1 228 037 B flash z dostępnych 1 900 544 B. Metadane aplikacji potwierdzają projekt `led_controller`, wersję `0.7.1` i poprawny SHA-256. Testy hostowe mają 45/45 PASS, a 112 plików przechodzi kontrolę UTF-8 bez BOM. Pierwszy pełny upload migracyjny UART, rzeczywiste pobranie HTTPS, ponowne uruchomienie drugiego slotu i wymuszony rollback mają status `NOT RUN`; nie wolno przedstawiać ich jako potwierdzonych sprzętowo.

`v0.7.2` izoluje awarie klientów wspólnego I2C0. NACK PCF8574AT, AT24C08N albo INA228 pozostaje stanem danego adresu; timeout dowolnego klienta resetuje tylko automat sprzętowy przez `i2c_master_bus_reset()` i zachowuje wszystkie uchwyty. Brak LCD jest logowany raz, nie zatrzymuje zapisu EEPROM-u, a po powrocie ekran przechodzi pełne czyszczenie, podwójną inicjalizację i dwie ramki. Temperatury LCD używają znaku `0xDF` przed `C`.

Ta wersja dodaje też profil PlatformIO `esp32doit-devkit-v1-ota` oraz uploader MQTT/HTTPS. Profil używa dokładnie bazowego `sdkconfig.esp32doit-devkit-v1`, a lokalne dane brokera i serwera są wyłącznie w ignorowanym `secrets.ota.json`. Uploader opcjonalnie publikuje plik lokalnie, sprawdza zdalny rozmiar i SHA-256, wysyła URL z QoS 1 i czeka na uruchomienie oraz zatwierdzenie nowej wersji. Oba profile zbudowano bez ostrzeżeń; deskryptory potwierdzają `led_controller`/`0.7.2`, testy hostowe mają 45/45 PASS, a 120 plików UTF-8 bez BOM. Rzeczywisty HIL odłączania klientów oraz transfer HTTPS OTA nadal mają status `NOT RUN`.

## 2026-10-08: v0.10.0 (kod, bez HIL)

Nowy panel diagnostyczny jest dostepny tylko w buildzie z AT24C08N. Serwer HTTPS
na porcie 443 czyta statusy i osobny bufor logow, ma wylacznie skany Wi-Fi/1-Wire
oraz obowiazkowa zmiane poczatkowego hasla admin/admin. Solony weryfikator jest
w zaszyfrowanym rekordzie EEPROM; certyfikat P-256 i klucz TLS powstaja lokalnie
i sa przechowywane w NVS. Lokalny hotspot OTA zachowuje wlasny port 80.
Szczegoly i testy HIL: [docs/WEB_DIAGNOSTICS.md](docs/WEB_DIAGNOSTICS.md).
Kompilacja i testy hostowe przechodza; nowy obraz nie zostal jeszcze wgrany ani
przetestowany na ESP32. Ostatni log sprzetowy wskazywal firmware v0.8.2.

## Definition of done

- build kończy się powodzeniem bez nowych ostrzeżeń;
- testy automatyczne przechodzą, jeżeli istnieją;
- dokumentacja jest zgodna z kodem;
- testy sprzętowe są wyraźnie oddzielone od kompilacji;
- repozytorium nie zawiera sekretów ani artefaktów builda.

## 2026-10-08: v0.10.1 (kod, bez HIL)

Po uruchomieniu REPL logi ESP_LOG przestaja byc wypisywane asynchronicznie na
UART. `logs status/on/off` steruje tylko emisja UART; odpowiedzi konsoli i
bufory logow WWW/MQTT pozostaja aktywne. Odcisk certyfikatu HTTPS nadal jest
drukowany w fazie startowej, przed uruchomieniem promptu.
Zmiana hasla w lokalnym pliku `secrets.mosquitto.passwd` nie odswiezyla juz
dzialajacego procesu Mosquitto. Po restarcie lokalnego brokera na adresie
192.168.1.130:1883 polaczenie TCP z ESP32 utrzymalo sie co najmniej 12 s.
Nie odczytano jeszcze `mqtt status` z ESP po restarcie brokera. Bez wgrania
nowego obrazu nie deklarowac HIL konsoli ani HTTPS v0.10.1.
`scripts/check.sh` oraz build profilu OTA zakonczone powodzeniem; testy hostowe
CTest 2/2 PASS, bez nowych ostrzezen kompilatora.

## 2026-10-08: v0.10.2 (kod, bez HIL)

Log `putty.log` z urzadzenia pokazal powtarzalny `stack overflow in task main`
po starcie `0.10.1`, przed uruchomieniem panelu HTTPS. Backtrace byl uszkodzony,
wiec dokladna funkcja MbedTLS nie jest potwierdzona. `0.10.2` startuje konsole
przed panelem i przenosi przygotowanie certyfikatu/serwera do jednorazowego
zadania `web_start` ze stosem 16 KiB; blad zadania nie blokuje lokalnego PWM.
`PROJECT_VER` podniesiono do 0.10.2. `check.sh` przeszedl: 6 testow skryptow,
2/2 testy hostowe i build bazowy. Pelny profil OTA zbudowal obraz
`led_controller/0.10.2` (1 378 416 B). HIL startu i panelu pozostaje otwarty;
nie publikowano Release ani nie wgrywano ESP32.

Publikacja przez tag `vX.Y.Z` jest przygotowana w GitHub Actions; wymaga
jednorazowego sekretu `RELEASE_REPO_TOKEN` ograniczonego do publicznego repo
wydan. Workflow buduje i publikuje `firmware.bin`, `release.json` i SHA256SUMS,
ale nie uruchamia OTA. Lokalny uploader ma jawny tryb `artifact_source=release`
dla obrazu z CI; stary tryb local nadal wymaga identycznego SHA lokalnego i HTTPS.
Instrukcja: [docs/GITHUB.md](docs/GITHUB.md). Pierwszy przebieg Actions i HIL OTA
nie zostaly jeszcze wykonane.

## 2026-10-08: v0.10.3 (kod, bez HIL)

Log z 0.10.2 potwierdzil prace HTTPS, zmiane hasla, Wi-Fi, MQTT i NTP.
Kod `-0x7780` MbedTLS oznacza alarm od klienta podczas uzgadniania TLS;
udane pozniejsze logowanie nie wskazuje awarii serwera. Przy braku DS18B20
biblioteka wypisywala ostrzezenie co 30 s. Skan najpierw sprawdza impuls
obecnosci bez logowania, a dopiero potem uruchamia iterator ROM. Inne bledy
1-Wire sa nadal raportowane. Test na ESP32 z pustym GPIO4 oraz z podlaczonym
DS18B20 pozostaje do wykonania.

Dalszy log 0.10.2 pokazal `mbedtls_ssl_setup -0x008D`, czyli
`PSA_ERROR_INSUFFICIENT_MEMORY` przy alokacji sesji HTTPS. 0.10.3 ogranicza
panel do jednej sesji, wlacza zamykanie LRU oraz podaje w logu `HTTPS RAM`
wolny wewnetrzny RAM i najwiekszy blok przy odczycie statusu co najwyzej raz
na minute. Nie zmieniono globalnych buforow TLS ani kodu OTA. Pelny
`check.sh` i build profilu OTA PASS; dlugi test HTTPS oraz pierwsze OTA
na sprzecie pozostaja do wykonania.
