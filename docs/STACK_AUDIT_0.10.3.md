# Audyt stosu - 2026-10-26, robocze zrodla 0.10.3

Zakres: audyt, bez zmian firmware, GPIO, rozmiarow stosow, wersji i bez OTA.
Analiza kodu i instrukcji `entry a1,N` w obiektach Xtensa aktualnego buildu
`esp32doit-devkit-v1` (debug). N to ramka jednej funkcji, NIE maksymalne
zuzycie calego zadania. Nie obejmuje naraz stosu funkcji wywolywanych,
bibliotek, przerwan ani wszystkich sciezek dynamicznych. Nie potwierdzono
przepelnienia ani bezpiecznego zapasu na urzadzeniu.

## Ustalenia w kolejnosci priorytetu

1. **P1 / ryzyko do pomiaru: skan Wi-Fi w systemowym event loop.**
   `WifiService.cpp:686`: 10 rekordow na stosie; ramka handleScanDone 976 B
   przy stosie event task 2304 B. Wlasne ramki eventHandler 32 B i
   handleWifiEvent 64 B tez pozostaja aktywne. Logowanie dodaje ramke
   DebugLog::capture 304 B oraz formatter i ramki ESP-IDF. Brakuje pomiaru
   zapasu tej sciezki. Zalecenie: wyniki pobierac partiami/jedynczo albo
   przeniesc prace do dedykowanego wlasciciela poza callback; nie dodawac
   nowego stalego zadania tylko dla skanu bez oceny kosztu RAM.

2. **P1 / luka walidacji: brak pelnych minimow stosu pod obciazeniem.**
   uxTaskGetStackHighWaterMark wystepuje tylko przy budowie Discovery
   (mqtt_service) i przed skasowaniem web_start. Nie mierzy osobnego zadania
   biblioteki MQTT, HTTP(S), lokalnego/zdalnego OTA, LCD, LED, IR ani konsoli.
   Sukces kompilacji nie dowodzi bezpieczenstwa. Dodac pomiary wykonywane
   przez zadania wlasne po kosztownych operacjach, z rzadkim eksportem
   liczbowym do diagnostyki. Nie logowac z ISR ani w kazdym obiegu 5 ms.
   Nie odczytywac uchwytow po skasowaniu zadania; web_start zachowac jako
   zapis wyniku przed vTaskDelete. HWM ESP-IDF podaje bajty, nie slowa.

3. **P2 / optymalizacja: zagniezdzone bufory EEPROM w lcd_service.**
   LcdService::taskLoop 864 + SettingsService::service 48 + loadRecord 1168
   + EncryptedSettingsStore::load 1168 + openCommitted 576 = 3824 B samych
   tych ramek, przed PSA/AES i pozostala obsluga. Stos wynosi 6144 B.
   load przechowuje oba sloty (1024 B), caller plaintext i zdekodowane dane,
   codec kolejna kopie szyfrogramu. Mozna rozwazyc odczyt metadanych slotow
   i ponowny odczyt wybranego slotu do jednego bufora, zachowujac fallback
   po bledzie uwierzytelnienia. Nie zmniejszac stosu LCD przed HIL.
   Zmiana musi zachowac zeroizacje sekretow i odpornosc na przerwany zapis.

4. **P2 / optymalizacja: lokalne OTA.**
   upload ma ramke 2400 B (bufor danych 2048 B i deskryptor); HTTP ma 8192 B.
   Mozna zmniejszyc blok transferu albo umiescic scratch w obiekcie wlasciciela
   po zabezpieczeniu pojedynczego uploadu. Przeniesienie do stalego bufora
   samo nie oszczedza RAM: oszczednosc powstaje dopiero przy bezpiecznej
   redukcji stosu, a bufor moze zajmowac RAM takze poza sesja OTA.

5. **P2 / koszt kazdego logu: DebugLog::capture.**
   Ramka 304 B plus vsnprintf. Bufor WWW jest uzupelniany rowniez przy
   wylaczonym debug MQTT, wiec ten koszt pozostaje. Nie usuwac logow
   diagnostycznych bez zamiennika. Rozwazyc mniej formatowania w callbackach
   systemowych. Wspolny statyczny bufor bez ochrony bylby bledem wielowatkowym.

6. **P2 / brak podstaw do redukcji sieciowych stosow.**
   mqtt_service i klient ESP-MQTT to dwa rozne stosy po 6144 B, ale korzystaja
   z jednej stalej kServiceTaskStackSize. Rozdzielic budzety przy przyszlym
   strojeniu. HTTPS ma 12288 B, zdalne OTA 8192 B; TLS wymaga pomiaru takze
   po bledzie/reconnect. web_start 16384 B jest tymczasowy i sam sie usuwa;
   jego redukcja nie poprawi stalego zuzycia po starcie. Generowanie pierwszego
   certyfikatu i odczyt juz istniejacego trzeba mierzyc oddzielnie.

## Budzety stosow

| Zadanie | Bajty | Uwagi |
|---|---:|---|
| main | 3584 | app_main konczy sie; obiekty uslug static |
| led_control | 4096 | stale |
| lcd_service | 6144 | aktywne EEPROM/DS18B20; bez obu funkcji 4096 |
| ir_remote | 4096 | gdy wlaczone |
| mqtt_service | 6144 | wlasna usluga |
| klient ESP-MQTT | 6144 | osobne zadanie biblioteki |
| ota_service | 8192 | utworzone przy starcie, takze bez transferu |
| local_ota | 4096 | zadanie zarzadzajace AP |
| konsola REPL | 4096 | osobne zadanie |
| HTTPS | 12288 | po uruchomieniu serwera |
| lokalny HTTP OTA | 8192 | podczas sesji AP |
| web_start | 16384 | tymczasowy |
| system event | 2304 | m.in. wyniki skanu Wi-Fi |
| esp_timer | 3584 | konfiguracja SDK |
| FreeRTOS timer | 2048 | konfiguracja SDK |
| TCP/IP | 3072 | konfiguracja SDK |
| mDNS | 4096 | konfiguracja SDK |

To nie jest pelna suma RAM: pozostaja Wi-Fi/idle/IPC/ISR, TCB, kolejki,
heap TLS i bufory. Przy pelnym uruchomieniu dziewiec stalych zadan aplikacji
(LED, LCD, IR, dwa MQTT, OTA, local_ota, REPL, HTTPS) rezerwuje 55296 B.

## Co jest juz poprawne

- app_main: ramka 48 B; duze obiekty uslug maja statyczny czas zycia.
- Discovery 24576 B i odpowiedz WWW 8192 B sa polami tych obiektow, nie
  lokalnymi tablicami stosu. cJSON/TLS to odrebny problem heap.
- Generowanie certyfikatu odseparowano od malego stosu main.
- CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y. Watchpoint konca stosu
  i compiler stack protector sa wylaczone. W buildzie diagnostycznym warto
  rozwazyc watchpoint (z uwzglednieniem debuggera); nie gwarantuje wykrycia
  kazdego przejscia poza stos i nie zastepuje pomiaru zapasu.

## Najwieksze ramki aktualnej kompilacji

| Ramka B | Funkcja |
|---|---|
| 2400 | `LocalOtaService::upload(httpd_req*)` |
| 1184 | `SettingsService::setWebPassword(char const*)` |
| 1168 | `SettingsService::loadRecord(unsigned long)` |
| 1168 | `SettingsService::beginSave(unsigned long)` |
| 1168 | `EncryptedSettingsStore::load(std::array<unsigned char, 32u> const&, unsigned long, std::array<unsigned char, 464u>&, unsigned int&, bool&)` |
| 1056 | `WebDiagnosticsService::status(httpd_req*)` |
| 1040 | `EncryptedSettingsStore::serviceSave()` |
| 1024 | `LocalOtaService::page(httpd_req*)` |
| 1024 | `DeviceTlsIdentity::printFingerprint() const` |
| 976 | `WifiService::handleScanDone()` |
| 864 | `LedController::makeLcdSnapshot()` |
| 864 | `LcdService::taskLoop()` |
| 768 | `DeviceTlsIdentity::generate()` |
| 672 | `persistent_settings::decode(unsigned char const*, unsigned int, persistent_settings::Data&)` |
| 672 | `LedController::serviceMqtt()` |
| 624 | `WebDiagnosticsService::authorized(httpd_req*, bool)` |
| 624 | `local_control::appendDiscovery(cJSON*, char const*, char const*)` |
| 592 | `SettingsService::applyLoadedRecord(unsigned long)` |
| 576 | `SecureSettingsCodec::openCommitted(std::array<unsigned char, 32u> const&, std::array<unsigned char, 512u> const&, std::array<unsigned char, 464u>&, SecureSettingsRecord::Metadata&)` |
| 576 | `MqttService::serviceDebug()` |
| 560 | `SecureSettingsCodec::sealStaged(std::array<unsigned char, 32u> const&, unsigned long, unsigned long, std::array<unsigned char, 12u> const&, unsigned char const*, unsigned int, std::array<unsigned char, 512u>&)` |
| 560 | `OtaService::requestUpdate(char const*, unsigned int)` |
| 544 | `OtaService::taskLoop()` |
| 496 | `OtaService::performUpdate(OtaService::Request const&)` |
| 496 | `LocalOtaService::enable()` |

## Weryfikacja na sprzecie przed redukcja stosow

- Zimny start z istniejacym certyfikatem; generowanie nowego na plytce testowej
  (bez kasowania produkcyjnego NVS).
- Skan Wi-Fi z >=10 sieciami, disconnect/reconnect, NTP, logi WWW i UART.
- Odczyt/zapis EEPROM, poprawny i uszkodzony rekord, NACK/timeout I2C.
- MQTT Discovery, birth HA, reconnect brokera i odrzucone komendy.
- HTTPS: logowanie, bledne haslo, ustawienie hasla, status/logi, zerwana sesja.
- OTA HTTPS i AP: poprawny/przerwany upload, bledny obraz, timeout i rollback.
- Rownoczesna praca przyciskow/PIR/IR/LCD z ruchem sieciowym, 30-60 min.

Zapisywac minimum wolnego stosu kazdego zadania oraz minimum wolnego heap
I najwiekszy blok. Najpierw usunac kosztowne ramki, ponowic identyczne testy,
dopiero potem zmniejszac budzety pojedynczo z zapasem. Nie proponuje obecnie
konkretnych mniejszych stosow: brakuje pomiarow HIL.

Zrodlo semantyki HWM i ochrony stosu:
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/performance/ram-usage.html

## Wykonane komendy i wynik

- `./scripts/check.sh` (Git Bash, PATH z PlatformIO, HOST_TEST_COMPILER=bcc64):
  PASS, build ESP32 + testy hostowe + adaptery (0 failures); brak warning:
  w logu. Pierwsza proba sandbox nie mogla zapisac cache PlatformIO;
  powtorzenie z uprawnieniami zakonczylo sie powodzeniem.
- `python scripts/check_utf8.py`: 164 pliki PASS po dodaniu raportu.
- `xtensa-esp32-elf-objdump -d -C .pio/build/esp32doit-devkit-v1/src/*.o`
  (iteracja Python po plikach, parsowanie entry a1,N): ranking 25 ramek powyzej;
  surowy ranking lokalnie .pio/stack-frames.json, log audit-stack-check.log.
- Build: statyczny RAM 118740 B, flash 1384913 B. RAM z raportu linkera NIE
  obejmuje przyszlych dynamicznych alokacji stosow i TLS.
- Zmieniono tylko docs/STACK_AUDIT_0.10.3.md i PLANS.md. Bez testu sprzetowego,
  flashowania, publikacji i zmiany numeru wersji.
