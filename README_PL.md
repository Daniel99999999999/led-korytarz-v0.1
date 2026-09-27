# Sterownik LED - firmware v0.7.2

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


Czysty C++17 na ESP-IDF, budowany przez PlatformIO dla DOIT ESP32 DevKit V1. Projekt nie korzysta z Arduino ani ESPHome.

## Zakres wersji 0.7.2

- pięć niezależnych kanałów PWM LEDC;
- PWM około 19 531,25 Hz, 12 bitów, duty 0–4095;
- pięć przycisków monostabilnych zwieranych do GND;
- programowa eliminacja drgań styków 25 ms;
- krótkie naciśnięcie przełączające kanał po zwolnieniu;
- przytrzymanie od 500 ms regulujące jasność 1–100%;
- programowy, nieblokujący fade 250 ms przy przełączaniu;
- start z pięcioma wyjściami PWM ustawionymi LOW i duty 0;
- wbudowana LED diagnostyczna: okres 1 s podczas normalnej pracy, światło stałe po resecie watchdog;
- task watchdog 5 s dla zadania sterującego i zadań idle obu rdzeni;
- LCD 20x4 HD44780 przez PCF8574AT, odświeżany najwyżej raz na sekundę;
- osobne zadanie LCD izolujące timeouty I²C od sterowania PWM;
- niezależne odzyskiwanie klientów wspólnego I²C bez wzajemnego unieważniania uchwytów;
- przycisk podświetlenia LCD na GPIO19 z zewnętrznym 10 kOhm i wewnętrznym pull-upem;
- Wi-Fi STA konfigurowane z konsoli UART, opcjonalnie odtwarzane z szyfrowanego AT24C08N;
- DHCP albo statyczne IPv4 oraz synchronizacja czasu z `pool.ntp.org`;
- czas Warszawy z automatycznym wyborem CET/CEST na podstawie daty;
- klient MQTT konfigurowany z konsoli, opcjonalnie odtwarzany z szyfrowanego AT24C08N;
- pięć neutralnych encji `light` i dwie encje PIR `binary_sensor` publikowane jednym komunikatem Home Assistant MQTT Device Discovery;
- retained availability, stan kanałów i wejść PIR oraz synchronizacja po reconnect z QoS 1;
- publikowanie po MQTT tylko zmienionych pól, z ponawianiem zaległych danych bez zalewania logu;
- dwuslotowe OTA po HTTPS, status w Home Assistant i automatyczny rollback nieudanego obrazu;
- scentralizowana tożsamość `Sterownik LED`, producent `Daniel Parzniewski` i identyfikator `sterownik_led_<MAC>`;
- statyczna kolejka komend zapewniająca, że tylko `LedController` steruje LEDC;
- kompletny, warunkowo kompilowany magazyn ustawień AT24C08N z AES-256-GCM i dwoma slotami;
- opcjonalna telemetria INA228 oraz trwała korekcja wskazania prądu;
- opcjonalna, nieblokująca obsługa do ośmiu DS18B20 z rolami radiatora i obudowy;
- opcjonalny odbiornik pilota NEC Extended na GPIO36;
- dwa obsługiwane wejścia PIR active-HIGH bez wewnętrznych rezystorów podciągających.

Nowa architektura nie używa wspólnego sygnału `ENABLE`. Tor sprzętowy każdego kanału musi niezależnie wymuszać OFF podczas resetu i przed uruchomieniem aplikacji.

## Pinout

| Funkcja | GPIO |
|---|---:|
| PWM 1 | 25 |
| PWM 2 | 26 |
| PWM 3 | 27 |
| PWM 4 | 32 |
| PWM 5 | 33 |
| Wbudowana LED diagnostyczna | 2 |
| Przycisk 1 | 13 |
| Przycisk 2 | 14 |
| Przycisk 3 | 16 |
| Przycisk 4 | 17 |
| Przycisk 5 | 18 |
| Przycisk podświetlenia LCD | 19 |
| DS18B20 DQ, opcja warunkowa | 4 |
| Wspólne I²C: LCD, opcjonalny EEPROM i INA228, SDA | 21 |
| Wspólne I²C: LCD, opcjonalny EEPROM i INA228, SCL | 22 |
| INA228 `/ALERT`, przyszła opcja | 23 |
| PIR, odnoga 1 | 34 |
| PIR, odnoga 2 | 35 |

GPIO0, GPIO5, GPIO12 i GPIO15 są zakazane. GPIO2 jest pinem strapującym zarezerwowanym wyłącznie dla wbudowanej LED diagnostycznej; nie należy podłączać do niego dodatkowego obciążenia.

Wszystkie przyciski są aktywne stanem LOW i korzystają z wewnętrznych pull-upów ESP32. Przycisk podświetlenia LCD ma dodatkowo sprzętowy pull-up 10 kOhm do 3,3 V; oba podciągania pracują równolegle.

## LCD

Firmware obsługuje LCD 2004 zgodny z HD44780 przez PCF8574AT. Podany adres `0x7E` jest 8-bitowym adresem zapisu; API ESP-IDF używa odpowiadającego mu adresu 7-bitowego `0x3F`. Magistrala pracuje z częstotliwością 100 kHz przez GPIO21/GPIO22, bez wewnętrznych pull-upów ESP32, za dwukierunkowym translatorem BSS138 3,3/5 V.

Ekran pokazuje stan i jasność pięciu kanałów. Trzeci wiersz nie zawiera już stałej częstotliwości PWM; jego prawa część przełącza co 2 s wyłącznie włączone strony pomiarowe: temperaturę radiatora `R:`, temperaturę obudowy `O:` i całkowity prąd INA228 `I:`. Temperatura zawiera pojedynczy znak stopnia z ROM-u HD44780 (`0xDF`) bezpośrednio przed `C`. Nieprzypisany czujnik pomija stronę, a przypisany, lecz nieobecny pokazuje `BRAK`. Przy prawidłowej pracy czwarty wiersz przełącza co 2 s: `LOKALNIE: OK`, uptime, RSSI z SSID, adres IPv4, stan MQTT oraz datę i czas Warszawy. `LOKALNIE: OK` oznacza sprawne autonomiczne sterowanie, a nie poprawność każdej usługi sieciowej. Strona MQTT pokazuje `MQTT: KOLEJKA PELNA`, dopóki zaległe publikacje nie zostaną wysłane. Strona połączonej sieci zaczyna się bezpośrednio od poziomu sygnału, na przykład `-34dBm NazwaSSID`; zbędny prefiks `WIFI` nie zajmuje miejsca przeznaczonego na SSID. Przed synchronizacją widoczny jest komunikat `CZAS: BRAK NTP`. Podczas aktywnego fade LCD próbuje raz na sekundę jasność wynikającą z aktualnego duty, zamiast od razu stale pokazywać cel. Błąd lokalny oraz reset watchdog pozostają wyświetlane stale i zatrzymują rotację. Pierwsza migawka jest wysyłana po zakończeniu inicjalizacji, kolejne najwyżej raz na sekundę. Każda ramka jawnie ustawia adresy `0x00`, `0x40`, `0x14`, `0x54` i ponownie zapisuje dokładnie 20 znaków każdego wiersza.

Operacje I²C wykonuje osobne zadanie o niższym priorytecie i mają timeout 20 ms. NACK lub brak konkretnego adresu wyłącza tylko odpowiadający mu PCF8574AT, AT24C08N albo INA228. Stan jest logowany raz i ponawiany lokalnie; inne urządzenia na wspólnych przewodach nadal pracują, a GPIO2 zachowuje heartbeat.

Timeout oznaczający zajętą albo zablokowaną magistralę uruchamia reset wyłącznie automatu sprzętowego I2C0. Firmware nie usuwa wtedy kontrolera ani uchwytów innych klientów. GPIO2 świeci stale do pierwszej poprawnej transakcji, natomiast PWM, przyciski i task watchdog pozostają aktywne. Po powrocie samego LCD zadanie co 2 s sonduje PCF8574AT, wykonuje pełne czyszczenie, podwójną inicjalizację HD44780, przywraca żądany stan podświetlenia i zapisuje dwie pełne ramki 4x20.

## Szyfrowane ustawienia

Projekt zawiera kompletną, opcjonalną obsługę Atmel AT24C08N z oznaczeniem `SU27`, zasilanego z 3,3 V. Pamięć korzysta z tej samej magistrali I2C0 GPIO21/GPIO22, 100 kHz, co PCF8574AT, ale musi być podłączona po stronie 3,3 V translatora BSS138. Korzysta ze wspólnych pull-upów tej strony i wymaga własnego kondensatora 100 nF.

Pamięć 1 KiB jest dzielona na dwa sloty po 512 B. `EncryptedSettingsStore` zapisuje wyłącznie rekordy AES-256-GCM z 12-bajtowym nonce i 16-bajtowym tagiem. Nowy slot otrzymuje znacznik commit dopiero po zapisaniu wszystkich stron po 16 B, a następnie jest odczytywany i uwierzytelniany. Maksymalny plaintext jednego rekordu wynosi 464 B.

`CONFIG_LED_CONTROLLER_AT24C08=n` pozostaje bezpieczną wartością domyślną repozytorium i usuwa z buildu cały stos pamięci. Pełny wariant docelowej PCB używa `y`: firmware tworzy losowy klucz AES-256 urządzenia w wewnętrznym NVS, wykrywa AT24C08N co 30 s, odczytuje ustawienia przy starcie i zapisuje je automatycznie 5 s po poprawnym `wifi apply` albo `mqtt apply`. Brak układu lub błąd magazynu nie blokuje przycisków, PWM, LCD ani sieci skonfigurowanej w RAM.

Rekord obejmuje zastosowane ustawienia Wi-Fi, MQTT, korekcję prądu INA228 oraz ROM-y przypisane do ról temperatury. **Jasność oraz stany ON/OFF kanałów LED nie są zapisywane.** Dostępne polecenia diagnostyczne to `settings status`, `settings save`, `settings reload`, `settings clear CONFIRM` i `settings ina-cal`. Nie ma klucza domyślnego, plaintextowego fallbacku ani sekretu w repozytorium. Bez Flash Encryption klucz w NVS chroni przed prostym odczytem samego zewnętrznego EEPROM-u, ale nie przed łącznym odczytem flash ESP32 i AT24C08N. Szczegóły opisuje `docs/SECURE_SETTINGS.md`.

## Czujniki DS18B20

`CONFIG_LED_CONTROLLER_DS18B20=n` jest ustawieniem domyślnym dla wariantu DNP. Wariant `y` uruchamia na GPIO4 magistralę 1-Wire z wyłączonym wewnętrznym pull-upem; wymagane jest zasilanie trzyprzewodowe 3,3 V i zewnętrzny rezystor 4,7 kOhm między DQ i 3,3 V. Firmware wykrywa do ośmiu czujników, rozpoczyna wspólną konwersję co 2 s, czeka 800 ms według ticka FreeRTOS i odczytuje urządzenia kolejno, bez `delay_ms` ani `vTaskDelay()`.

Dwie role użytkowe to `radiator` i `obudowa`. Nie trzeba przepisywać 64-bitowych adresów:

```text
temp scan
temp status
temp assign radiator 1
temp assign obudowa 2
```

Numer jest tylko pozycją na aktualnej liście skanu. Firmware zapisuje właściwy ROM czujnika, dlatego kolejność urządzeń po restarcie nie ma znaczenia. `temp clear radiator`, `temp clear obudowa` albo `temp clear all` usuwa przypisanie. Przy aktywnym AT24C08N role są częścią zaszyfrowanego rekordu; bez pamięci pozostają w RAM do resetu. Home Assistant otrzymuje encje `Temperatura radiatora` i `Temperatura obudowy` z osobną dostępnością. Bieżący etap jest wyłącznie telemetrią i nie wyłącza kanałów po przekroczeniu temperatury.

DS1307 pozostaje nieobsadzoną opcją DNP i nie jest obecnie kompilowany ani obsługiwany. Czas lokalny pochodzi z NTP; RTC zostanie dodany warunkowo dopiero wtedy, gdy lokalna automatyka będzie wymagała rzeczywistej godziny bez Wi-Fi.

## Testowe Wi-Fi i NTP

Po uruchomieniu lokalnego kontrolera firmware startuje Wi-Fi STA oraz konsolę na UART0, 115200 bit/s. ROM bootloader nadal programuje się przez standardowe USB na GPIO1/GPIO3. Aplikacja zachowuje TX konsoli na GPIO1, ale przełącza RX na GPIO39/VN, aby zewnętrzna przejściówka nie walczyła z TX wbudowanego CP2102 na GPIO3. `TXD` przejściówki prowadzi do GPIO39 przez 1 kOhm, GPIO39 ma pull-up 10 kOhm do 3,3 V, `RXD` przejściówki łączy się z GPIO1, a VCC pozostaje niepodłączone. Bezpośrednie USB po starcie nadal pokazuje logi, ale nie przekazuje poleceń do aplikacji. Firmware nie używa komend AT. Sam sterownik Wi-Fi nadal używa `WIFI_STORAGE_RAM`, `CONFIG_ESP_WIFI_NVS_ENABLED=n` i `nvs_enable=0`, więc nie tworzy drugiej kopii danych sieciowych. Przy `AT24C08=n` reset usuwa konfigurację. Przy `AT24C08=y` i obecnej pamięci zastosowane SSID, hasło, IPv4 i DNS są odtwarzane z jednego zaszyfrowanego rekordu; wewnętrzny NVS przechowuje klucz urządzenia oraz dane kalibracji PHY.

Na obecnym module otwarcie lub zamknięcie COM4 przez terminal może przełączyć linie DTR/RTS układu USB-UART i wywołać `POWERON_RESET`. `platformio.ini` żąda nieaktywnych DTR/RTS dla monitora PlatformIO, ale nie steruje zachowaniem innych programów, na przykład PuTTY. W wariancie bez AT24C08 taki reset nadal usuwa konfigurację RAM; po poprawnym zapisie EEPROM konfiguracja ma zostać automatycznie odtworzona.

ESP Console REPL oczekuje pojedynczego `CR` po naciśnięciu Enter. Domyślny monitor PlatformIO wysyła `CRLF`, co powoduje drugi pusty Enter, podwójny prompt i natychmiastowe przyjęcie pustego hasła przez `wifi password` albo `mqtt password`. Projekt ustawia `monitor_eol = CR`; po zmianie pliku trzeba zamknąć i ponownie uruchomić monitor. Inne terminale również należy ustawić tak, aby Enter wysyłał tylko `CR`.

Wewnętrzny tag ESP-IDF `i2c.master` jest wyciszony przez wspólnego właściciela magistrali. Sterownik nadal zwraca wszystkie błędy, natomiast firmware raportuje pierwszą awarię jednym komunikatem `lcd_service`, zatrzaskuje alarm diagnostyczny i zachowuje cykliczne próby odzyskania. Zapobiega to zalewaniu UART komunikatami `probe device timeout` po odłączeniu LCD albo innego urządzenia I2C.

Minimalna sesja z DHCP:

```text
wifi ssid "Nazwa_sieci"
wifi password
wifi mode dhcp
wifi apply
wifi status
```

`wifi password` odczytuje hasło bez echa i bez historii poleceń. Dla statycznego adresu użyj:

```text
wifi static --ip 192.168.1.50 --mask 255.255.255.0 --gw 192.168.1.1
wifi dns --primary 192.168.1.1 --secondary 1.1.1.1
wifi apply
```

Dostępne są również `wifi scan`, `wifi disconnect` i `wifi clear`. Po uzyskaniu IP usługa uruchamia SNTP `pool.ntp.org`. Reguła `CET-1CEST,M3.5.0/2,M10.5.0/3` wybiera czas letni w ostatnią niedzielę marca i czas zimowy w ostatnią niedzielę października. Hasło nigdy nie jest pokazywane przez `wifi status` ani logowane.

## Testowe MQTT i Home Assistant

`MqttService` używa oficjalnego ESP-MQTT. Broker, użytkownik, hasło, temat bazowy i prefix discovery pozostają w RAM usługi, a przy `AT24C08=y` ich zastosowana kopia jest zapisywana w tym samym zaszyfrowanym rekordzie co Wi-Fi. Po restarcie konfiguracja jest odtwarzana i klient ponownie się łączy. Przy opcji `n` reset nadal usuwa całą konfigurację MQTT.

Minimalna sesja po wcześniejszym połączeniu Wi-Fi:

```text
mqtt uri mqtt://A.B.C.D:1883
mqtt username "Uzytkownik"
mqtt password
mqtt discovery homeassistant
mqtt apply
mqtt status
```

Domyślny topic prefix to `sterownik-led`; można go jawnie zmienić poleceniem `mqtt topic`. `mqtt password` używa promptu bez echa. URI nie może zawierać danych logowania. Etap testowy obsługuje tylko `mqtt://`; `mqtts://` jest odłożone do decyzji o źródle certyfikatu CA. `mqtt disconnect` zatrzymuje klienta, a `mqtt clear` zeruje konfigurację w RAM.

Identyfikator ma format `sterownik_led_<MAC>`. Discovery jest publikowane retained pod `homeassistant/device/<device-id>/config` i tworzy urządzenie `Sterownik LED` z producentem `Daniel Parzniewski`, modelem `ESP32 5-channel PWM`, pięcioma encjami `Kanał 1`–`Kanał 5` oraz dwiema encjami `Wejście PIR 1` i `Wejście PIR 2`. Nazwy pomieszczeń i obszary przypisuje użytkownik w HA. Tematy kanału mają postać `<topic-prefix>/<device-id>/channel/N/set` oraz `.../state`, a stan PIR jest publikowany jako retained `ON`/`OFF` pod `<topic-prefix>/<device-id>/pir/N/state`. Komendy JSON przyjmują `state` (`ON`/`OFF`), `brightness` (`0–255`) i opcjonalny `transition` w sekundach. Retained command jest odrzucany, aby po reconnect nie wykonać starego polecenia.

Callback MQTT tylko waliduje wiadomość i bez oczekiwania wysyła `ControlCommand` do statycznej kolejki. LEDC nadal modyfikuje wyłącznie zadanie `LedController` o nazwie `led_control`. Długie ręczne przytrzymanie ma pierwszeństwo dla danego kanału, a brak brokera lub błędne hasło nie ustawia alarmu GPIO2 i nie blokuje sterowania lokalnego. Po pełnej synchronizacji klient publikuje wyłącznie pola, których wartość uległa zmianie. Przy pełnym outboxie zapamiętuje pierwsze niewysłane pole, ponawia od niego i drukuje jeden komunikat błędu oraz jeden komunikat odzyskania.

Stare urządzenie z testowego HA na maszynie wirtualnej nie jest migrowane ani usuwane przez firmware. Świeża instalacja Home Assistant i Mosquitto na Raspberry Pi nie odziedziczy go, o ile nie zostanie przywrócona kopia środowiska VM.

## OTA przez HTTPS

Firmware używa dwóch partycji aplikacji `ota_0` i `ota_1`, każda po 1856 KiB. NVS pozostaje pod dotychczasowym adresem `0x9000`, dlatego zwykły pełny upload migracyjny nie zmienia położenia klucza ustawień. Jeżeli na urządzeniu nie było jeszcze tabeli wprowadzonej w `v0.7.1`, trzeba jeden raz wgrać bieżący obraz przez UART wraz z bootloaderem, tablicą partycji i `ota_data_initial.bin`. Kolejne wersje można instalować sieciowo.

Komenda OTA jest zwykłym payloadem **bez flagi retained** i zawiera jeden adres do pliku `firmware.bin`:

```text
topic:   sterownik-led/<device-id>/ota/set
payload: https://serwer.example/firmware.bin
retain:  false
qos:     1
```

Stan jest publikowany retained pod `sterownik-led/<device-id>/ota/status` i pojawia się w Home Assistant jako encja diagnostyczna `Status OTA`. Zawiera stan, wersję bieżącą i kandydującą, postęp oraz ostatni błąd. Firmware przyjmuje wyłącznie `https://`, korzysta z publicznego zestawu CA, odrzuca URL z danymi logowania, retained command, obraz innego projektu i ponowną instalację tej samej wersji. Lokalny serwer z certyfikatem własnym nie zadziała bez późniejszego dodania jego CA do firmware.

Pobieranie odbywa się w osobnym zadaniu. Błąd sieci, TLS, obrazu albo zapisu pozostawia bieżący slot aktywny. Po udanej instalacji nowy obraz ma 10 s na stabilny start lokalnego kontrolera; reset lub watchdog przed zatwierdzeniem powoduje automatyczny powrót do poprzedniego slotu. OTA nie zastępuje Secure Boot: dostęp do brokera nadal należy ograniczyć silnym hasłem i zaufaną siecią.

PlatformIO udostępnia środowisko `esp32doit-devkit-v1-ota`. Dziedziczy ono pełny `sdkconfig.esp32doit-devkit-v1`, dlatego kliknięcie Upload dla profilu OTA buduje ten sam zestaw funkcji co profil kablowy. Lokalna konfiguracja komputera znajduje się w ignorowanym pliku `secrets.ota.json`; wzór pól jest w `secrets.example.ota.json`. Uploader nie wymaga `paho-mqtt`: korzysta wyłącznie z biblioteki standardowej Pythona.

`firmware_url` musi wskazywać plik dostępny przez HTTPS z certyfikatem zaufanym przez firmware. Opcjonalny `publish_path` pozwala przed wysłaniem komendy atomowo skopiować bieżący `firmware.bin` do katalogu obsługiwanego przez lokalny serwer HTTPS. Skrypt pobiera plik z podanego URL-u i porównuje jego rozmiar oraz SHA-256 z aktualnym buildem, więc nie uruchomi OTA dla starego artefaktu. Następnie publikuje komendę QoS 1 przez MQTT i czeka, aż nowa wersja uruchomi się oraz wróci do stanu `idle`.

## PIR

Każde wejście PIR ma zewnętrzny pull-up 10 kOhm do 3,3 V i jest połączone przez rezystor szeregowy ze stykiem NC czujki:

- LOW: stan normalny, styk NC zamknięty;
- HIGH: wykrycie ruchu, otwarcie styku albo przerwanie przewodu.

GPIO34 i GPIO35 nie mają wewnętrznych rezystorów podciągających. Firmware v0.7.0 konfiguruje oba piny jako wejścia bez pull-up i pull-down, próbkuje je co 5 ms i stosuje filtr styku 25 ms. HIGH jest publikowany w Home Assistant jako wykryty ruch (`ON`), a LOW jako stan normalny (`OFF`). Ponieważ ruch i przerwanie przewodu mają ten sam poziom HIGH, firmware nie rozróżnia tych przyczyn.

Ten etap udostępnia wyłącznie telemetrię. PIR nie włącza jeszcze żadnego kanału LED; mapowanie wejść na kanały, czas podtrzymania, tryb nocny i priorytet sterowania ręcznego pozostają do ustalenia. Bez zamontowanej czujki rozwarte wejście z działającym zewnętrznym pull-upem będzie prawidłowo widoczne jako aktywne.

## Budowanie i wgrywanie

```bash
pio run -e esp32doit-devkit-v1
pio run -e esp32doit-devkit-v1 -t upload
pio run -e esp32doit-devkit-v1-ota -t upload
pio device monitor -b 115200
```

Profil `esp32doit-devkit-v1` ma jawnie ustawione `upload_protocol = esptool`, `upload_port = COM4` i `monitor_port = COM4`. Build nie wymaga obecności portu, ale wgrywanie kablowe i monitor zakończą się błędem, dopóki płytka nie pojawi się jako COM4. Profil `esp32doit-devkit-v1-ota` nie używa portu COM; wymaga działających Wi-Fi, MQTT, serwera HTTPS oraz uzupełnionego lokalnego `secrets.ota.json`.

Projekt używa `board_build.partitions = partitions_ota_4mb.csv`: `ota_0` i `ota_1` mają po 1856 KiB. Pierwsze wgranie po przejściu z tabeli `factory` musi zaprogramować bootloader, nową tablicę partycji, początkowe dane OTA i aplikację. Nie używaj samego `firmware.bin` pod starym offsetem.

Pełna kontrola jakości:

```bash
./scripts/check.sh
```

Skrypt buduje firmware ESP32 i uruchamia 45 scenariuszy hostowych logiki debounce, PIR, gestów, przejść duty, LCD, MQTT, walidacji adresu OTA, pilota NEC, skalowania INA228, CRC i konwersji DS18B20, układu szyfrowanych rekordów oraz formatu trwałych ustawień TLV. Na bieżącej stacji Windows testy korzystają z CMake i Visual Studio Build Tools 2022; nie wymagają podłączonej płytki ani elementów toru mocy.

Wszystkie piny i parametry PWM są zebrane w `src/BoardConfig.hpp`.

## Pierwsze uruchomienie

Najpierw uruchom płytkę bez pełnego obciążenia i sprawdź oscyloskopem:

1. brak błysku na PWM1–PWM5 podczas resetu;
2. duty 0 po starcie;
3. częstotliwość około 19 531,25 Hz i zakres duty 0–4095;
4. krótkie naciśnięcie każdego przycisku zmienia tylko odpowiadający kanał;
5. hold od OFF startuje z 1%, a hold od ON zmienia jasność w naprzemiennym kierunku;
6. pełne przejście 1–100% trwa około 4 s i zatrzymuje się na granicy;
7. szybkie ponowne naciśnięcie odwraca fade bez zatrzymania obsługi pozostałych przycisków;
8. LED na GPIO2 miga 500 ms ON / 500 ms OFF i nie utrudnia programowania;
9. kontrolnie wywołany watchdog resetuje układ, pozostawia kanały OFF i zapala GPIO2 na stałe;
10. LCD odpowiada pod adresem 7-bitowym `0x3F`, pokazuje cztery poprawne wiersze i reaguje na GPIO19;
11. zmiany kanałów pojawiają się na LCD nie później niż po około 1 s, a ostatni wiersz co 2 s przechodzi przez status, uptime, Wi-Fi/RSSI, IP, MQTT i czas;
12. dowolna awaria I²C, w tym brak odpowiedzi PCF8574AT albo zablokowanie SDA lub SCL, zapala GPIO2 na stałe, ale nie zatrzymuje PWM, przycisków ani watchdoga;
13. po usunięciu awarii LCD zostaje ponownie zainicjalizowany i wyczyszczony, najnowsza ramka wraca na ekran, a GPIO2 ponownie miga, o ile nie ma innego zatrzaśniętego błędu;
14. po `wifi apply` urządzenie uzyskuje IP, synchronizuje NTP i pokazuje czas Warszawy z poprawnym CET albo CEST;
15. reset usuwa konfigurację Wi-Fi, a brak AP lub NTP nie zatrzymuje przycisków, PWM, LCD ani watchdoga;
16. po `mqtt apply` Home Assistant wykrywa `Sterownik LED`, producenta, wersję, pięć neutralnych encji `light` i dwie encje PIR `binary_sensor`;
17. komendy ON/OFF, jasność i transition działają, a lokalny przycisk aktualizuje retained state w Home Assistant;
18. utrata brokera i reconnect nie zatrzymują przycisków, PWM, LCD, heartbeat ani watchdoga;
19. wejście PIR ma LOW przy zamkniętym NC i HIGH po otwarciu lub przerwaniu przewodu; po filtracji odpowiednia encja HA przechodzi między `OFF` i `ON` bez zmiany PWM.

Fade jest liczony programowo i aktualizowany co 5 ms przez pojedynczy task kontrolera. Firmware zapisuje duty przez `ledc_set_duty()` i `ledc_update_duty()` oraz nie uruchamia sprzętowej usługi fade LEDC. Błąd pojedynczego kanału zatrzymuje go w LOW bez powtarzania logu i bez blokowania pozostałych wejść.
