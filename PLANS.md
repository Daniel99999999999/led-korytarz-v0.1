# Execution Plans

Ten plik jest żywym planem prac. Codex aktualizuje go przy zadaniach obejmujących wiele plików lub etapów. Plan ma być samowystarczalny: osoba czytająca repozytorium powinna rozumieć cel, postęp, decyzje i sposób walidacji bez historii czatu.

## Etap v0.8.2 zakończony w kodzie, oczekuje HIL: diagnostyka startowa bez konsoli RX

- [x] Kontrola bazowa check.sh PASS.
- [x] MQTT: adres brokera bez danych uwierzytelniania URI, użytkownik, obecność hasła,
  topic/discovery/client ID, wynik uruchomienia klienta.
- [x] Wi-Fi po uzyskaniu IP: maska, brama, runtime DNS1/DNS2, tryb adresowania i stan NTP.
- [x] EEPROM: flagi skonfigurowania Wi-Fi/MQTT oraz etap błędu odtwarzania.
- [x] Rozszerzony dump Wi-Fi/NTP/BSSID, parametry MQTT i pola błędu transportu.
- [x] Start klienta po GOT_IP w zadaniu MQTT, poza wspólnym event loop.
- [x] Końcowy check.sh PASS: 58 testów hostowych i adaptery; bez ostrzeżeń.
  RAM 80 232 B, flash 1 245 453 B. [Raport i testy sprzętowe](docs/VALIDATION_0.8.2.md).
- [ ] HIL: odczytać rzeczywisty broker i DNS przez USB po resecie.

Nie zmienia ustawień, formatu EEPROM/NVRAM, GPIO ani przyczyny problemu MQTT.
Baza 0.8.1 została potwierdzona w logu użytkownika; EEPROM odtwarza się poprawnie,
Wi-Fi łączy się, klient MQTT startuje, ale występują timeouty połączenia.

## Plan zakończony w kodzie, oczekuje HIL v0.8.1: diagnostyka MQTT bez DS1307

- [x] Odczyt kontraktów i kontrola bazowego kodu (check.sh).
- [x] Przełącznik debug: ON/OFF w HA, 10 minut, OFF po restarcie, bez retained commands.
- [x] Kopia ESP_LOG przez bufor RAM, bez blokowania producentów, limit 5 wiadomości/s.
- [x] Przyczyna resetu, uptime i licznik pominiętych logów, discovery i instrukcja HA.
- [x] Testy bufora, timeoutu, discovery; końcowy check.sh i formatowanie.
- [ ] HIL: aktywacja HA, utrata MQTT, timeout sesji, restart i praca bez DS1307.

UART pozostaje aktywny. Brak zapisu logów do EEPROM/NVS/DS1307 lub RTC.
Bufor nie przeżywa restartu; panic/bootloader i printf nie są przechwytywane.
Diagnoza watchdog wymaga kolejnych danych; obecny etap dodaje obserwowalność, nie ogłasza naprawy watchdog.

Wynik: check.sh PASS, 58 scenariuszy + adaptery (w tym 4 wątki/4000 logów) PASS.
RAM 80 224 B, flash 1 242 409 B, bez ostrzeżeń. Bez wgrania.
[Raport i komendy](docs/VALIDATION_0.8.1.md), [podgląd w HA](docs/DEBUG_MQTT.md).

## Plan zakończony w kodzie, oczekuje HIL: KEY/PIR i LCD w HA v0.8.0

### Zakres i kontrakt

- Pięć ustawień MQTT select kategorii config: KEY1–KEY5 -> brak, kanał 1–5, kanały 1+2 lub wszystkie.
- Dwa ustawienia MQTT number: PIR1 -> kanał 1, PIR2 -> kanał 2; czas 0–3600 s, 0 wyłącza automatykę.
- Domyślnie zachować mapowanie 1:1 i wyłączoną automatykę PIR, aby aktualizacja nie uruchomiła niezamówionego światła.
- Grupa przełącza się wspólnie: wszystkie ON -> OFF, w pozostałych przypadkach -> ON; każdy kanał zachowuje własną jasność.
- Ręczne włączenie ma pierwszeństwo przed timerem PIR; ręczne OFF blokuje aktualne wykrycie do powrotu PIR do LOW.
- Czas PIR liczony od ustania aktywności, ponowne HIGH podtrzymuje światło. Zgodnie z korektą użytkownika ustawienia przechowywane wyłącznie w opcjonalnej NVRAM DS1307, bez zapisu do EEPROM-u, bez jasności, ON/OFF ani bieżących timerów. Brak DS1307 oznacza pracę w RAM.
- PWM pozostaje obsługiwane wyłącznie przez zadanie kontrolera. Mapowanie nie zmienia GPIO.
- LCD pokazuje docelową jasność podczas ON/OFF; MQTT nadal wysyła tylko zmiany stanu/docelowej jasności, bez kroków fade.

- Trzy dodatkowe encje LCD: tryb, czas przycisku i noc z HA.
- PIR2 budzi LCD; przycisk ma czasowy priorytet także przy Stale OFF.
- Odczyt DDRAM co 125 ms/znak, recovery po niezgodności, przerwa 10 s po błędzie.

### Postęp

- [x] Audyt kodu i dokumentacji; docs/HARDWARE_CONTRACT.md, docs/ARCHITECTURE.md i docs/OPEN_DECISIONS.md nie istnieją w dostarczonej kopii.
- [x] Próba scripts/check.sh: brak iconv w środowisku; osobny build PlatformIO wymaga dostępu do katalogu narzędzi poza workspace.
- [x] Model ustawień, mapowania grup i timerów PIR z testami hostowymi.
- [x] Integracja kolejki kontrolera, MQTT discovery i dwóch rekordów CRC w 56 B NVRAM DS1307.
- [x] Ograniczenie prezentacji pośrednich wartości PWM na LCD, test regresji publikacji MQTT.
- [x] Podświetlenie, odczyt DDRAM, blueprint HA; testy logiki i adapterów.
- [x] Końcowy build, check.sh, formatowanie i dokumentacja.
- [ ] HIL: konfiguracja w HA, grupy przycisków, PIR, restart/offline i ruch MQTT.

### Wynik 2026-09-24

Implementacja zakończona. Pełny check.sh PASS, build bez ostrzeżeń, 55 scenariuszy hostowych
oraz testy adapterów LCD/PWM PASS. RAM 71 496 B, flash 1 238 245 B.
Nie wgrywano firmware; HIL i instalacja blueprintu w HA pozostają do wykonania.
Pełny raport, komendy i zakres plików: [VALIDATION_0.8.0.md](docs/VALIDATION_0.8.0.md).

## Plan oczekujący na HIL: izolacja klientów I2C i profil OTA v0.7.2

### Cel

Odłączenie jednego urządzenia od wspólnego I2C0 nie może unieważniać uchwytów ani zatrzymywać pozostałych klientów. PCF8574AT, AT24C08N i INA228 mają niezależnie wykrywać brak odpowiedzi, ograniczać powtarzane logi i samodzielnie wracać po ponownym podłączeniu. Dodatkowo PlatformIO ma udostępniać osobny profil, który po kliknięciu Upload buduje pełny wariant z tą samą konfiguracją co UART i uruchamia istniejące HTTPS OTA przez MQTT.

### Progress

- [x] Rozróżnić lokalny NACK/brak konkretnego adresu od timeoutu całej magistrali.
- [x] Nie usuwać I2C0 ani uchwytów EEPROM-u i INA228 po awarii samego LCD.
- [x] Resetować wyłącznie sprzętowy automat I2C po timeoutach zgłoszonych przez LCD, AT24C08N albo INA228.
- [x] Ograniczyć komunikaty o braku PCF8574AT do jednego wpisu i jednego wpisu po odzyskaniu.
- [x] Przywracać LCD przez pełne czyszczenie, podwójną inicjalizację i dwie ramki bez zatrzymywania innych klientów.
- [x] Wyświetlać temperaturę ze znakiem stopnia z ROM-u HD44780 (`0xDF`) przed `C`.
- [x] Dodać profil `esp32doit-devkit-v1-ota`, wspólny `sdkconfig` i uploader MQTT/HTTPS bez dodatkowych bibliotek Pythona.
- [x] Zweryfikować build profilu UART i profilu OTA bez nowych ostrzeżeń.
- [x] Zaktualizować kontrakt sprzętowy, architekturę, plan testów, roadmapę i handoff.
- [x] Uruchomić 45 scenariuszy hostowych oraz ścisłą kontrolę UTF-8 bez BOM.
- [ ] Wgrać `v0.7.2` i wykonać HIL odłączania kolejno LCD, AT24C08N i INA228.
- [ ] Wykonać pierwsze rzeczywiste klikane OTA z serwera HTTPS oraz próbę rollbacku.

### Kontrakt

- `ESP_ERR_NOT_FOUND` z sondowania oraz `ESP_ERR_INVALID_RESPONSE`/NACK transmisji dotyczą wyłącznie adresowanego urządzenia i nie ustawiają globalnego alarmu I2C.
- `ESP_ERR_TIMEOUT` oznacza zajętą lub zablokowaną magistralę; firmware resetuje automat I2C bez usuwania kontrolera i uchwytów innych klientów.
- Brak opcjonalnego urządzenia jest logowany raz, a ponowienia odbywają się z dotychczasowymi ograniczonymi interwałami.
- Profil OTA używa dokładnie `sdkconfig.esp32doit-devkit-v1`; nie może przypadkowo zbudować obrazu bez AT24C08N, IR, INA228 lub DS18B20.
- Sekrety komputera dla uploadu OTA pozostają wyłącznie w ignorowanym `secrets.ota.json`.

### Outcomes

- 2026-08-26: profil UART zbudował pełny obraz bez ostrzeżeń: 59 512 B RAM i 1 228 589 B flash; profil OTA: 59 512 B RAM i 1 228 557 B flash z dostępnych 1 900 544 B.
- Oba obrazy mają deskryptor projektu `led_controller`, wersję `0.7.2` i identyczny SHA-256 wygenerowanego `sdkconfig.h`; wszystkie opcje AT24C08N, IR, INA228 i DS18B20 są aktywne.
- Testy hostowe mają 45/45 PASS, oba skrypty Python przechodzą `py_compile`, a 120 plików przechodzi ścisłą kontrolę UTF-8 bez BOM.
- Ścieżka `pio run -e esp32doit-devkit-v1-ota -t upload` dochodzi do własnego uploadera i bezpiecznie zatrzymuje się przed siecią, dopóki nie istnieje lokalny `secrets.ota.json`.
- Bash nie jest dostępny w `PATH`, dlatego wykonano ręczny odpowiednik `scripts/check.sh`; buildy i kontrole statyczne mają PASS. HIL I2C i rzeczywisty transfer OTA pozostają `NOT RUN`.

---

## Plan oczekujący na HIL: publikacja różnicowa MQTT i HTTPS OTA v0.7.1

### Cel

Usunąć powtarzane publikowanie niezmienionych stanów, kontrolowanie odzyskiwać pełny outbox ESP-MQTT oraz umożliwić następne aktualizacje bez przełączania UART-u. OTA ma działać w osobnym zadaniu, przyjmować wyłącznie HTTPS, raportować stan do Home Assistant i zachować lokalne sterowanie niezależnie od sieci. Pierwsza migracja ma zachować istniejący klucz AES w NVS.

### Progress

- [x] Dodać osobny tracker 17 pól retained i publikować w normalnej pracy tylko wartości zmienione.
- [x] Po reconnect i birth Home Assistant wymuszać pełne discovery oraz pełną migawkę.
- [x] Ponawiać pierwsze niewysłane pole co 500 ms, bez ponownego kolejkowania wcześniejszych pól.
- [x] Ograniczyć log pełnego outboxu do pierwszego błędu i jednego komunikatu odzyskania.
- [x] Rozdzielić na LCD `LOKALNIE: OK` od `MQTT: KOLEJKA PELNA` i dodać zaległość do `mqtt status`.
- [x] Zastąpić pojedynczą partycję dwoma slotami OTA po 1856 KiB bez zmiany położenia NVS.
- [x] Dodać osobne zadanie HTTPS OTA, walidację URL, projektu i wersji obrazu oraz rollback po nieudanym pierwszym starcie.
- [x] Dodać temat komendy OTA, retained status JSON i diagnostyczną encję Home Assistant.
- [x] Dodać testy hostowe trackera MQTT, LCD i walidatora URL OTA.
- [x] Zaktualizować kontrakt sprzętowy, architekturę, plan testów i handoff.
- [ ] Wgrać pełny obraz migracyjny UART bez `erase_flash` i potwierdzić zachowanie ustawień AT24/NVS.
- [ ] Wykonać OTA do wersji wyższej niż 0.7.1 z serwera HTTPS z zaufanym certyfikatem.
- [ ] Przerwać transfer oraz wymusić wadliwy start, potwierdzając zachowanie starego slotu i rollback.

### Kontrakt

- NVS pozostaje pod `0x9000`, ma 24 KiB i przechowuje klucz AES-256; migracja nie wykonuje `erase_flash`.
- Pierwszy upload musi zawierać bootloader, `partitions.bin`, `ota_data_initial.bin` i aplikację. Kolejne aktualizacje używają tylko aplikacyjnego `firmware.bin`.
- Komenda OTA jest surowym URL-em `https://` bez flagi retained. URL nie może zawierać userinfo, spacji, znaków sterujących, fragmentu ani schematu HTTP.
- Publiczny bundle CA ufa publicznym urzędom certyfikacji, nie lokalnemu certyfikatowi self-signed.
- Brak Secure Boot oznacza, że kontrola nazwy projektu i wersji nie jest podpisem kryptograficznym producenta.
- Awaria Wi-Fi, MQTT, DNS, TLS, pobierania, zapisu albo rollbacku nie może blokować przycisków, PWM ani watchdoga.

### Outcomes

- 2026-08-17: zdekodowana tabela partycji potwierdziła NVS `0x9000/24 KiB`, `otadata` `0xF000/8 KiB`, `ota_0` `0x20000/1856 KiB` i `ota_1` `0x1F0000/1856 KiB`.
- 2026-08-17: końcowy pełny wariant zbudował się bez ostrzeżeń; używa 59 504 B RAM oraz 1 228 037 B flash z dostępnych 1 900 544 B. Metadane obrazu potwierdzają projekt `led_controller`, wersję `0.7.1` i poprawny SHA-256.
- 2026-08-17: 45/45 scenariuszy hostowych ma status PASS, a 112 plików przechodzi ścisłą kontrolę UTF-8 bez BOM. HIL migracji, HTTPS OTA i rollbacku pozostaje `NOT RUN`.

---

## Plan oczekujący na HIL: telemetria DS18B20 v0.7.0

### Cel

Dodać warunkową, nieblokującą obsługę wielu DS18B20 na GPIO4. Użytkownik ma przypisywać dwa czujniki do ról radiatora i obudowy krótkim numerem ze skanu, bez ręcznego przepisywania ROM-u. Temperatury trafiają na LCD i do Home Assistant, a same przypisania do zaszyfrowanego AT24C08N, gdy pamięć jest aktywna. Brak czujników nie może wpływać na lokalne sterowanie.

### Progress

- [x] Dodać `CONFIG_LED_CONTROLLER_DS18B20=n` i warunkową zależność `espressif/onewire_bus` 1.1.1.
- [x] Dodać magistralę RMT 1-Wire na GPIO4 bez wewnętrznego pull-upu i bez trybu pasożytniczego.
- [x] Zaimplementować skan maksymalnie ośmiu czujników, kontrolę CRC ROM/scratchpada i nieblokującą konwersję co 2 s.
- [x] Dodać role `radiator` i `obudowa` oraz polecenia `temp scan/status/assign/clear`.
- [x] Zapisywać właściwe ROM-y ról w payloadzie AT24C08N, bez zapisu jasności lub stanu LED.
- [x] Dodać rotację `R:`/`O:`/`I:` w trzecim wierszu LCD.
- [x] Dodać dwie encje temperatury MQTT Discovery z osobną dostępnością.
- [x] Dodać testy hostowe modelu temperatury, rotacji LCD i walidacji przypisań.
- [x] Zbudować warianty `DS18B20=n` i `DS18B20=y` bez ostrzeżeń.
- [x] Wykonać HIL jednego DS18B20 i przypisać go do roli `obudowa`.
- [x] Naprawić fałszywy timeout ACK polling AT24C08N ujawniony przez pierwszy zapis roli.
- [x] Zachować automatyczny skan DS18B20 co 30 s, ale wyciszyć niezmienione wyniki okresowe.
- [ ] Wykonać HIL z dwoma rzeczywistymi DS18B20 oraz odtworzeniem ról z AT24C08N.

### Kontrakt

- DQ używa GPIO4 i jednego zewnętrznego pull-upu 4,7 kOhm do 3,3 V; zasilanie jest trzyprzewodowe.
- Numery `[1]..[8]` są tylko interfejsem aktualnego skanu. To 64-bitowy ROM jest właściwą tożsamością i wartością trwałą.
- Jednego ROM-u nie można przypisać jednocześnie do obu ról.
- Konwersja czeka 800 ms według ticka FreeRTOS; nie wolno dodawać `delay_ms`, `vTaskDelay()` ani operacji 1-Wire do zadania sterującego LEDC.
- Brak lub błąd czujnika zmienia wyłącznie dostępność telemetrii. Bieżący etap nie ma progów termicznych ani reakcji ochronnej.
- DS1307 pozostaje DNP i poza zakresem tej wersji.

### Outcomes

- 2026-08-10: wariant `DS18B20=n` z pozostałymi opcjami używa 56 512 B RAM oraz 1 078 945 B flash i nie kompiluje `Ds18b20Service` ani komponentu OneWire.
- 2026-08-10: końcowy pełny wariant `DS18B20=y` używa 58 188 B RAM oraz 1 103 377 B flash z partycji 1 536 000 B.
- 2026-08-10: pełny `scripts/check.sh` ma status PASS: 96 plików UTF-8 bez BOM, build bez ostrzeżeń i wszystkie 43 scenariusze hostowe zaliczone.
- 2026-08-16: dodano wyciszenie wyłącznie wewnętrznego taga `i2c.master`; błędy nadal obsługuje i raportuje jednokrotnie `LcdService`, a recovery pozostaje aktywne. HIL z odłączonym LCD pozostaje do wykonania.
- 2026-08-16: aplikacyjny RX konsoli UART0 przeniesiono z GPIO3 na GPIO39/VN, pozostawiając TX na GPIO1 i standardowe GPIO1/GPIO3 dla ROM bootloadera. HIL z zewnętrznym adapterem, pull-upem 10 kOhm i rezystorem szeregowym 1 kOhm pozostaje do wykonania.
- 2026-08-16: pełny `scripts/check.sh` po hotfixie UART/I2C ma status PASS: 96 plików UTF-8 bez BOM, 43/43 scenariusze hostowe i build bez ostrzeżeń. Pełny obraz używa 58 188 B RAM oraz 1 103 561 B flash.
- 2026-08-16: pierwszy HIL wykrył jeden DS18B20, odczytał stabilne `27.313 C` i przypisał jego ROM do roli `obudowa`; `radiator` pozostał nieprzypisany.
- 2026-08-16: rzeczywisty AT24C08N ujawnił błąd graniczny: zadanie budzone około 10 ms zgłaszało timeout przed ostatnią próbą ACK. `v0.7.0` wykonuje próbę przed oceną deadline; ponowny HIL zapisu i odtworzenia po restarcie pozostaje wymagany.
- 2026-08-16: okresowy skan 1-Wire pozostaje aktywny co 30 s dla hot-plug, lecz INFO jest emitowane tylko po ręcznym skanie albo zmianie zestawu ROM-ów.
- 2026-08-16: końcowy build `v0.7.0` po poprawkach EEPROM/DS18B20 zakończył się bez ostrzeżeń; używa 58 188 B RAM i 1 103 713 B flash. ELF zawiera dokładnie wersję `0.7.0`, testy hostowe mają 43/43 PASS, a 96 plików przechodzi kontrolę UTF-8 bez BOM.

---

## Plan oczekujący na HIL: trwałe ustawienia AT24C08N v0.6.5-test

### Cel

Domknąć opcjonalny magazyn Wi-Fi, MQTT i kalibracji INA228 w zewnętrznym AT24C08N. Dane mają być szyfrowane i uwierzytelniane, zapis odporny na utratę zasilania, a brak pamięci nie może wpływać na sterowanie lokalne. Zgodnie z decyzją użytkownika jasność i stany kanałów LED pozostają wyłącznie w RAM.

### Progress

- [x] Dodać losowy klucz AES-256 urządzenia przechowywany w wewnętrznym NVS.
- [x] Dodać wersjonowany i rygorystycznie walidowany payload TLV Wi-Fi/MQTT/INA mieszczący się w 464 B.
- [x] Nie dodawać do payloadu jasności, ON/OFF, timerów ani stanu pilota.
- [x] Zintegrować odczyt i zapis z zadaniem `LcdService`, jedynym właścicielem I2C0.
- [x] Odtwarzać zastosowaną konfigurację po uruchomieniu usług Wi-Fi i MQTT.
- [x] Scalać automatyczne zapisy przez 5 s oraz udostępnić komendy `settings ...`.
- [x] Dodać trwałą korekcję prądu INA228 w zakresie -100000..100000 ppm.
- [x] Zachować działanie RAM i lokalne przy nieobecnym EEPROM-ie albo błędzie klucza.
- [x] Dodać testy hostowe round-trip, wartości ujemnej, pustego, uszkodzonego i zbyt dużego payloadu.
- [x] Zbudować warianty `AT24C08=n`, `AT24C08=y` oraz pełny `AT24C08+IR+INA228` bez ostrzeżeń.
- [ ] Wykonać HIL po zamontowaniu AT24C08N, w tym odcięcie zasilania podczas zapisu.

### Kontrakt

- EEPROM zawiera wyłącznie rekord AES-256-GCM; nie istnieje fallback plaintextowy.
- Klucz nie trafia do EEPROM-u, repozytorium ani logu. Bez Flash Encryption jego ochrona w wewnętrznym NVS jest ograniczona do trybu deweloperskiego.
- `wifi apply`, `mqtt apply` i zmiana kalibracji zlecają zapis; regulacja LED nigdy go nie zleca.
- Po resecie wszystkie kanały startują OFF i nie odtwarzają poprzedniej jasności.
- Nieobecny AT24C08N jest sprawdzany co 30 s i nie ustawia alarmu GPIO2.
- `settings clear CONFIRM` zapisuje logicznie pusty rekord; nie gwarantuje fizycznego nadpisania obu starych slotów.

### Outcomes

- 2026-08-09: testy hostowe mają `40/40 PASS`.
- 2026-08-09: wariant `AT24C08=n` z IR i INA228 używa 52 616 B RAM oraz 1 060 197 B flash.
- 2026-08-09: pełny wariant `AT24C08=y`, `IR=y`, `INA228=y` używa 54 412 B RAM oraz 1 077 533 B flash.
- 2026-08-09: wariant tylko `AT24C08=y` również zbudowano bez ostrzeżeń; HIL EEPROM ma status `NOT RUN`.

---

## Plan oczekujący na HIL: telemetria INA228 v0.6.4-test2

### Cel

Dodać opcjonalny pomiar wspólnej szyny +24 V bez uzależniania lokalnego sterowania od obecności INA228. Udostępnić prąd na LCD oraz prąd, napięcie, moc, energię i temperaturę układu w Home Assistant. Etap nie realizuje jeszcze ograniczania mocy ani alarmu GPIO23.

### Progress

- [x] Zweryfikować na rzeczywistym schemacie adresy i pull-upy wspólnej magistrali I2C.
- [x] Przyjąć bocznik 10 mOhm, `CURRENT_LSB=25 uA`, szeroki zakres VSHUNT i `SHUNT_CAL=3277`.
- [x] Dodać `CONFIG_LED_CONTROLLER_INA228=n` oraz warunkowe źródło w CMake.
- [x] Dodać identyfikację, konfigurację i odczyt wszystkich rejestrów telemetrii INA228.
- [x] Obsłużyć nieobecny układ oraz błąd odczytu bez zatrzymania LCD, PWM, przycisków i MQTT.
- [x] Pokazywać prąd w prawej części trzeciego wiersza LCD.
- [x] Dodać pięć encji MQTT/HA z osobną dostępnością INA228.
- [x] Poprawić dziedziczenie MQTT availability, aby HA nie otrzymywał jednocześnie `availability` i `availability_topic` dla sensorów INA228.
- [x] Dodać diagnostykę `ina status` bez poleceń zmieniających kalibrację.
- [x] Zbudować warianty `INA228=n` i `INA228=y` bez ostrzeżeń.
- [x] Uruchomić końcowy pełny zestaw testów hostowych i kontrolę UTF-8.
- [ ] Wgrać obraz dopiero po uzgodnieniu obsadzonej wartości R38 i wykonać HIL z INA228.

### Kontrakt

- Adresy 7-bitowe to LCD `0x3F`, INA228 `0x40`, AT24C08N `0x50`–`0x53` i DS1307 `0x68`; nie kolidują.
- R22/R24 podciągają stronę 3,3 V po 4,7 kOhm, a R23/R25 stronę 5 V po 4,7 kOhm; ESP32 nie włącza pull-upów wewnętrznych I2C.
- Bieżące skalowanie jest poprawne wyłącznie dla fizycznego bocznika 10 mOhm. Schemat KiCad nadal ma opis R38 `0R015`, więc BOM i firmware muszą zostać uzgodnione przed HIL.
- INA228 jest wyłącznie telemetrią. `/ALERT`, GPIO23, progi i wyłączanie kanałów pozostają nieaktywne.
- Brak INA228 daje niedostępne encje pomiarowe, ale nie zmienia dostępności świateł ani PIR.
- Rejestr energii nie zapewnia trwałości po resecie. Współczynnik kalibracyjny jest już polem zaszyfrowanego EEPROM-u, natomiast trwała suma energii pozostaje niezaimplementowana.

### Acceptance criteria

- oba warianty kompilacji przechodzą bez ostrzeżeń, a testy hostowe sprawdzają wszystkie przeliczniki;
- `INA228=y` bez układu nie powoduje resetu, stałego GPIO2 ani utraty lokalnego sterowania;
- z układem `ina status` potwierdza ID, `SHUNT_CAL=3277` i poprawne flagi diagnostyczne;
- wskazania prądu i napięcia są porównane z przyrządem wzorcowym w kilku punktach, w tym blisko 9,375 A;
- HA pokazuje pięć sensorów INA228 i oznacza je jako niedostępne po odłączeniu układu bez wyłączania pozostałych encji.

### Decision Log

- 2026-08-05: Dla planowanych 225 W przy 24 V przyjęto 9,375 A i bocznik 10 mOhm; daje to 93,75 mV oraz około 0,88 W straty.
- 2026-08-05: Kalibracja pierwszej wersji jest nominalna. Późniejsza korekta ma być jednym współczynnikiem zapisanym w zaszyfrowanych ustawieniach, nie ręcznym setupem rejestrów przy każdym starcie.
- 2026-08-05: Odczyty INA228 wykonuje właściciel wspólnej magistrali I2C, a kontroler pobiera wyłącznie nieblokującą kopię telemetrii.
- 2026-08-08: Wspólna dostępność discovery jest listą jednoelementową. Sensory INA228 nadpisują ją listą dwóch tematów w trybie `all`, bez konfliktu ze starszym `availability_topic`.

### Outcomes & Retrospective

- 2026-08-05: Wariant `INA228=n` używa 51 424 B RAM i 1 052 597 B flash; wariant `INA228=y` 52 616 B RAM i 1 059 833 B flash z partycji 1 536 000 B. Oba zbudowano bez ostrzeżeń.
- 2026-08-05: Po końcowych korektach formatowania LCD testy hostowe mają `37/37 PASS`, kontrola obejmuje 85 plików UTF-8 bez BOM, a reguły statyczne mają status PASS. Ponowny relink PlatformIO po tych drobnych korektach nie wystartował z powodu limitu narzędzia Codex; wymaga wykonania przed uploadem.
- 2026-08-08: Po poprawce discovery pełny wariant `INA228=y` ponownie zbudowano bez ostrzeżeń: 52 616 B RAM i 1 060 197 B flash. Obraz wymaga ponownego uploadu i kontroli pięciu encji w HA.

---

## Plan oczekujący na HIL: pilot NEC Extended v0.6.3-test

### Cel

Dodać opcjonalny odbiornik pilota na GPIO36 bez naruszania pojedynczej własności LEDC. Ten sam pilot ma wybierać sterownik białych LED albo przyszły sterownik RGB+CCT, a po każdym resecie zwykłe komendy są ignorowane do naciśnięcia selektora `BIAŁE` lub `RGB`.

### Progress

- [x] Zarezerwować GPIO36 i opisać zewnętrzny pull-up oraz tor odbiornika 38 kHz.
- [x] Dodać czysty dekoder NEC Extended dla bajtów `87 5A CMD ~CMD`, LSB-first.
- [x] Obsłużyć ramkę NEC Repeat wyłącznie dla regulacji jasności.
- [x] Dodać automat wyboru roli `BIAŁE`/`RGB`, celu kanał 1–5/WSZYSTKIE i czasu fade.
- [x] Przekazywać komendy z zadania RMT przez istniejącą kolejkę `ControlCommand`.
- [x] Dodać POWER z odtworzeniem stanu, toggle celu, tryb nocny 10% i timer 30 min.
- [x] Pokazywać wybór celu i krótkie komunikaty IR w czwartym wierszu LCD; alarm ma wyższy priorytet.
- [x] Dodać testy hostowe dekodera, selektorów, targetu, fade i reguł Repeat.
- [x] Skompilować wariant `CONFIG_LED_CONTROLLER_IR_NEC=y`.
- [x] Podłączyć testowo odbiornik IR i elementy towarzyszące.
- [x] Włączyć opcję i wgrać `0.6.3-test` na COM4.
- [x] Ponownie wgrać obraz z poprawionym filtrem RMT i potwierdzić odbiór `WHITE MODE`.
- [ ] Wgrać hotfix opóźnienia NEC Repeat i wykonać pełny HIL pilota.

### Kontrakt

- `CONFIG_LED_CONTROLLER_IR_NEC=n` pozostaje ustawieniem domyślnym do czasu montażu odbiornika.
- GPIO36 jest tylko wejściem i nie ma wewnętrznego pull-up; wymagany jest zewnętrzny pull-up 10 kOhm do 3,3 V.
- Callback RMT działa w ISR i kopiuje tylko metadane do statycznej kolejki. Nie loguje, nie dekoduje i nie dotyka LEDC.
- `LedController` pozostaje jedynym właścicielem stanu kanałów i sprzętu LEDC.
- Zmiana IR trafia do wspólnej migawki MQTT, więc Home Assistant otrzymuje stan tak samo jak po przycisku lokalnym.
- Klawisze scen pozostają zarezerwowane i nie zmieniają wyjść.

### Acceptance criteria

- wariant z IR i bez IR buduje się bez ostrzeżeń, a wszystkie testy hostowe przechodzą;
- obcy adres, zły bajt `~CMD`, błędny timing i spóźniony Repeat nie zmieniają kanałów;
- po resecie POWER i jasność nie działają przed wybraniem `BIAŁE`;
- po wybraniu `RGB` biały sterownik ignoruje zwykłe komendy, lecz nadal rozpoznaje oba selektory;
- awaria lub brak odbiornika nie zatrzymuje przycisków, MQTT, LCD ani watchdoga;
- HIL potwierdza wszystkie 13 kodów i brak fałszywych zmian przy świetle otoczenia.

### Decision Log

- 2026-08-03: Pilot używa NEC Extended, adresu bajtowego `0x87 0x5A` i komplementarnej pary `CMD/~CMD`; pełne wartości w formacie yoRadio są wyłącznie diagnostyczne.
- 2026-08-03: `WHITE MODE` wybiera pięciokanałowy sterownik białych LED, a `RGB MODE` przekazuje pilot przyszłemu sterownikowi RGB+CCT.
- 2026-08-03: Początkowym celem po wyborze białego sterownika jest kanał 1; przyciski CCT−/CCT+ przechodzą cyklicznie przez kanały 1–5 i WSZYSTKIE.
- 2026-08-03: Czasy przejścia IR to 0, 500 i 2000 ms; wartość początkowa wynosi 500 ms.

### Outcomes & Retrospective

- 2026-08-03: Testy hostowe wzrosły z 29 do 35 scenariuszy i mają status PASS.
- 2026-08-03: Końcowy wariant domyślny `n` używa 47 832 B RAM i 1 038 029 B flash; kontrolny wariant `y` używa 49 360 B RAM i 1 052 137 B flash. Test sprzętowy ma status `NOT RUN`, ponieważ odbiornik nie jest jeszcze zamontowany.
- 2026-08-03: Domyślny wariant `IR=n` używa 47 836 B RAM i 1 038 017 B flash. Na Windows wykonano ręczny odpowiednik `scripts/check.sh`: 81 plików UTF-8 bez BOM, kontrole statyczne, CTest i build mają status PASS; Bash nie jest dostępny w `PATH`.
- 2026-08-05: Po naprawie toru EN ręczny upload wariantu `IR=y` na COM4 zakończył się poprawnie i esptool zweryfikował wszystkie obrazy. Pierwszy test sprzętowy ujawnił odrzucanie konfiguracji RMT: próg `signal_range_min_ns=100000` przekraczał limit klasycznego ESP32 wynoszący mniej niż 3187 ns.
- 2026-08-05: Próg filtra RMT zmniejszono do 2000 ns, dodano kontrolę kompilacyjną limitu i zatrzymanie wyłącznie zadania IR po trwałym `ESP_ERR_INVALID_ARG`. Poprawiony wariant `IR=y` buduje się bez ostrzeżeń, używa 49 360 B RAM i 1 052 237 B flash; 35 scenariuszy hostowych ma status PASS. Ponowny upload i pełny HIL pozostają do wykonania.
- 2026-08-05: Poprawiony obraz został uruchomiony na sprzęcie. Odbiornik rozpoznał `WHITE MODE` (`0x1C`), a LCD wyświetlił `IR: BIALE AKTYWNE`; tor GPIO36, RMT RX, dekoder NEC Extended i selektor sterownika mają status PASS. Pozostałe przyciski pilota nadal wymagają HIL.
- 2026-08-05: `RGB ON/OFF` poprawnie przełącza kanał 1. Krótki klik jasności wykonywał także pierwszy NEC Repeat po około 110 ms, dając zmianę 8–10% zamiast jednego kroku 5%. Mapper otrzymał próg przytrzymania 500 ms; wcześniejsze Repeat tylko podtrzymują ciągłość transmisji. Hotfix ma 35 scenariuszy hostowych PASS, buduje się bez ostrzeżeń i używa 49 360 B RAM oraz 1 052 241 B flash. Wymaga ponownego uploadu i HIL.

---

## Plan oczekujący na HIL: telemetria PIR v0.6.2-test

### Cel

Uruchomić dwa istniejące wejścia NC na GPIO34/GPIO35 i udostępnić ich odfiltrowany stan jako dwie encje `binary_sensor` w tym samym urządzeniu Home Assistant. Ten etap nie steruje jeszcze kanałami LED na podstawie PIR.

### Progress

- [x] Potwierdzić semantykę fail-high: LOW to stan normalny, HIGH to ruch albo przerwa przewodu.
- [x] Dodać adapter `PirInput` bez wewnętrznego pull-up i pull-down.
- [x] Zastosować 25 ms filtracji styku przy pollingu kontrolera co 5 ms.
- [x] Dołączyć oba stany PIR do jednoelementowej migawki MQTT.
- [x] Rozszerzyć jeden payload MQTT Device Discovery o dwie encje `binary_sensor`.
- [x] Dodać test hostowy aktywnej logiki HIGH i zabezpieczenia statyczne konfiguracji GPIO.
- [x] Uruchomić pełny `./scripts/check.sh` i zapisać wynik.
- [x] Wgrać `0.6.2-test` na COM4.
- [ ] Ponownie skonfigurować Wi-Fi/MQTT w RAM.
- [ ] Potwierdzić w HA jedno urządzenie z pięcioma światłami i dwiema encjami PIR.
- [ ] Wykonać HIL obu wejść przez zwarcie GPIO do GND i późniejsze rozwarcie.

### Kontrakt

- GPIO34/GPIO35 używają wyłącznie zewnętrznych pull-upów 10 kOhm do 3,3 V.
- Stan HIGH jest publikowany jako `ON`, a LOW jako `OFF`; firmware nie potrafi odróżnić ruchu od przerwania przewodu.
- Nazwy encji to `Wejście PIR 1` i `Wejście PIR 2`, klasa urządzenia HA to `motion`.
- Stany są publikowane retained pod `<topic-prefix>/<device-id>/pir/N/state` z QoS 1.
- Błąd Wi-Fi, MQTT lub samego adaptera PIR nie może zatrzymać przycisków, PWM ani watchdoga.
- PIR nie wysyła w tym etapie `ControlCommand` i nie ma przypisania do kanałów, podtrzymania ani trybu nocnego.

### Acceptance criteria

- build nie zawiera nowych ostrzeżeń, a wszystkie testy hostowe przechodzą;
- discovery tworzy dokładnie siedem encji pod jednym urządzeniem: pięć `light` i dwa `binary_sensor`;
- zwarcie odpowiedniego GPIO do GND przez ponad 25 ms daje `OFF`, a rozwarcie daje `ON`;
- zmiana jednego wejścia nie zmienia drugiego wejścia ani żadnego PWM;
- podczas zmian PIR lokalne przyciski, LCD, heartbeat GPIO2 i watchdog pracują nadal.

### Decision Log

- 2026-07-29: Telemetria PIR trafia do HA, ponieważ może być używana również przez inne automatyzacje.
- 2026-07-29: Filtr 25 ms dotyczy wyłącznie drgań styku. Czasy automatyki oświetlenia pozostają nierozstrzygnięte.
- 2026-07-29: Bez zamontowanej czujki rozwarte wejście z pull-upem poprawnie zgłasza stan aktywny HIGH; test HIL może użyć bezpiecznego zwarcia samego GPIO do GND.
- 2026-07-30: Bieżący obraz pozostaje bez INA228. Przyszły sterownik ma być domyślnie wyłączony przez `CONFIG_LED_CONTROLLER_INA228=n` i usuwany z grafu kompilacji do czasu montażu układu.
- 2026-07-30: GPIO4 zarezerwowano dla przyszłego DS18B20. Prawa część trzeciego wiersza LCD ma co 2 s przełączać temperaturę radiatora oraz całkowity prąd INA228 w mA.
- 2026-07-30: Monitor PlatformIO ma wysyłać pojedynczy `CR`, zgodnie z konfiguracją UART REPL w ESP-IDF. Domyślne `CRLF` powodowało drugi pusty prompt oraz natychmiastowe przyjęcie pustego hasła.

### Outcomes & Retrospective

- 2026-07-29: Pełny `./scripts/check.sh` ma status PASS: 74 pliki UTF-8 bez BOM, build bez ostrzeżeń i wszystkie 29 scenariuszy hostowych zaliczone.
- 2026-07-29: Obraz `0.6.2-test` używa 47 648 B RAM i 1 037 793 B flash z partycji 1 536 000 B. Kontrola ELF potwierdziła wersję, obie nazwy PIR i komunikat discovery `5 kanalow LED + 2 wejscia PIR`.
- 2026-07-30: Ponowny build PlatformIO i 29 scenariuszy hostowych zakończyły się powodzeniem. COM4 nie był widoczny w systemie, dlatego upload i HIL nadal pozostają niewykonane.
- 2026-07-30: PlatformIO zapisało na COM4 bootloader, tablicę partycji i `v0.6.2-test`; esptool zweryfikował skróty wszystkich obrazów. Provisioning RAM-only i HIL PIR pozostają do wykonania.
- 2026-07-30: Zbędna druga próba uploadu po dodaniu wyłącznie kompilacyjnych rezerwacji GPIO4/GPIO23 nie weszła w bootloader (`Invalid head of packet 0x49`) i nie rozpoczęła kasowania flasha. Płytka zachowała pierwszy, poprawnie zweryfikowany obraz `v0.6.2-test`; rezerwacje nie konfigurują pinów ani nie zmieniają zachowania firmware'u.
- 2026-07-30: Dodano `monitor_eol = CR` i kontrolę tej wartości w `scripts/check.sh`. Jest to konfiguracja monitora hosta, więc nie wymaga ponownego wgrywania firmware'u.

---

## Zakończony plan: neutralna tożsamość i LCD v0.6.1-test

### Cel

Usunąć roboczą nazwę korytarza z tożsamości pięciokanałowego produktu, zachowując neutralne kanały możliwe do przypisania do pomieszczeń w Home Assistant. Uzupełnić odłożoną diagnostykę MQTT na LCD i pokazywać podczas fade jasność wynikającą z aktualnego duty.

### Progress

- [x] Uzgodnić nazwę `Sterownik LED`, slug `sterownik-led` i identyfikator `sterownik_led_<MAC>`.
- [x] Uzgodnić producenta `Daniel Parzniewski` oraz neutralne nazwy `Kanał 1`–`Kanał 5`.
- [x] Scentralizować pola produktu i encji w `DeviceIdentity.hpp`.
- [x] Zmienić klasę na `LedController`, task/tag na `led_control` i Kconfig na `CONFIG_LED_CONTROLLER_AT24C08`.
- [x] Dodać stronę stanu MQTT do rotacji LCD przez nieblokującą migawkę usługi.
- [x] Pokazywać na LCD aktualną jasność podczas programowego fade bez zalewania MQTT stanami pośrednimi.
- [x] Zbudować wstępnie firmware `0.6.1-test` bez ostrzeżeń.
- [x] Uruchomić pełny `./scripts/check.sh` i zapisać wynik.
- [x] Wgrać firmware na COM4 i wykonać provisioning Wi-Fi/MQTT w jednej sesji terminala.
- [x] Potwierdzić kartę `Sterownik LED`, producenta, wersję i pięć neutralnych encji w Home Assistant.
- [x] Przenieść ponowne potwierdzenie próbek jasności LCD podczas długiego `transition` do HIL następnej wersji.

### Kontrakt

- Wszystkie pola tożsamości produktu mają jedno źródło w `DeviceIdentity.hpp`.
- `unique_id` encji pozostaje stabilnie wyprowadzony z fabrycznego MAC i numeru kanału.
- Nazwy pomieszczeń nie są kodowane w firmware; użytkownik nadaje własne nazwy i obszary w Home Assistant.
- Stan publikowany do HA jest wartością zadaną, a LCD podczas aktywnego przejścia pokazuje próbkę aktualnego duty najwyżej raz na sekundę.
- Pobieranie statusu Wi-Fi i MQTT do LCD jest nieblokujące i nie może opóźnić taska `led_control`.
- Firmware nie zawiera starego identyfikatora ani kodu migracyjnego dla tymczasowej instalacji HA.

### Acceptance criteria

- discovery przedstawia nazwę `Sterownik LED`, producenta `Daniel Parzniewski`, model, wersję oraz pięć encji `Kanał 1`–`Kanał 5`;
- domyślny topic prefix to `sterownik-led`, a konsola używa promptu `sterownik-led>`;
- strona MQTT mieści się w 20 znakach dla każdego stanu;
- długi `transition` pokazuje na LCD wartości pośrednie, podczas gdy HA otrzymuje wartość zadaną;
- testowy HA na VM może zachować stare urządzenie, które nie przejdzie do świeżej instalacji Raspberry Pi bez odtworzenia kopii;
- pełny check przechodzi bez nowych ostrzeżeń.

### Decision Log

- 2026-07-28: Przyjęto neutralny słownik produktu i kanałów; przypisanie kanału do pokoju pozostaje konfiguracją HA.
- 2026-07-28: Producent w MQTT Device Discovery to `Daniel Parzniewski`.
- 2026-07-28: Testowa maszyna wirtualna HA jest środowiskiem jednorazowym. Nie czyścimy starego retained discovery z firmware ani nie implementujemy migracji `led_korytarz_<MAC>`.

### Outcomes & Retrospective

- 2026-07-28: Pełny `./scripts/check.sh` ma status PASS: 72 pliki UTF-8 bez BOM, build bez ostrzeżeń i wszystkie 28 scenariuszy hostowych zaliczone.
- 2026-07-28: Obraz `0.6.1-test` używa 47 264 B RAM i 1 036 181 B flash z partycji 1 536 000 B. Kontrola napisów ELF potwierdziła nową nazwę, producenta i identyfikator oraz brak starej tożsamości.
- 2026-07-28: `scripts/check.sh` odrzuca ponowne wprowadzenie starego identyfikatora, klasy, taska lub producenta do źródeł firmware.
- 2026-07-29: Upload i provisioning RAM-only zakończyły się powodzeniem. Home Assistant pokazał `Sterownik LED`, producenta, wersję oraz pięć neutralnych kanałów; brak encji PIR był zgodny z zakresem `0.6.1-test`.

---

## Zakończony plan: MQTT i Home Assistant v0.6-test

### Cel

Połączyć sterownik z testowym brokerem Mosquitto w Home Assistant bez zapisywania danych dostępowych poza RAM. Home Assistant ma automatycznie utworzyć pięć encji `light`, a wszystkie komendy sieciowe mają przechodzić przez kolejkę jedynego właściciela LEDC.

### Progress

- [x] Potwierdzić osiągalność Home Assistant `192.168.0.115:8123` i brokera MQTT na porcie `1883`.
- [x] Dodać testowalny parser komend JSON `state`, `brightness` i `transition`.
- [x] Dodać statyczną kolejkę `ControlCommand` do kontrolera LED.
- [x] Dodać usługę ESP-MQTT uruchamianą po sterowaniu lokalnym.
- [x] Dodać retained availability/LWT, stan pięciu kanałów i synchronizację po reconnect.
- [x] Dodać Home Assistant MQTT Device Discovery dla pięciu encji `light`.
- [x] Dodać konsolę `mqtt ...` z konfiguracją tylko w RAM i hasłem bez echa.
- [x] Zbudować `0.6.0-test` i potwierdzić partycję aplikacji 1500 KiB.
- [x] Uruchomić pełny `./scripts/check.sh`, w tym testy hostowe.
- [x] Wgrać pełny obraz firmware na COM4 wraz z tablicą partycji.
- [x] Wykonać HIL z rzeczywistym brokerem Home Assistant.
- [x] W `0.6.1-test` dodać do rotacji czwartej linii LCD stronę stanu MQTT oraz prezentować podczas fade rzeczywistą jasność wyliczoną z aktualnego duty.

### Kontrakt etapu testowego

- Broker, użytkownik, hasło i temat bazowy są wprowadzane przez UART i znikają po resecie.
- Test lokalny używa `mqtt://`; `mqtts://` pozostaje wyłączone do decyzji o źródle CA.
- Temat bazowy jest parametrem konsoli, więc etap testowy nie rozstrzyga jego docelowej wartości.
- Identyfikator urządzenia i encji jest stabilnie wyprowadzany z fabrycznego MAC.
- Callback MQTT jedynie waliduje wiadomość i wysyła `ControlCommand` bez oczekiwania; nie wywołuje LEDC.
- Ręczne długie przytrzymanie ma pierwszeństwo dla danego kanału, a jego komenda sieciowa pozostaje w kolejce.
- Stan, discovery oraz `online`/`offline` są retained i używają QoS 1.
- Brak brokera, błędne dane logowania i reconnect nie ustawiają alarmu GPIO2 ani nie blokują PWM, przycisków, LCD i watchdoga.

### Acceptance criteria

- pięć encji pojawia się automatycznie jako jedno urządzenie MQTT w Home Assistant;
- komenda ON/OFF, jasność `0–255` i przejście docierają przez kolejkę kontrolera;
- zmiana lokalnym przyciskiem aktualizuje retained state w Home Assistant;
- reconnect publikuje `online`, discovery i aktualny stan wszystkich kanałów;
- niepoprawny JSON, zakres lub temat nie zmienia stanu kanału;
- przez co najmniej 90 s pracy z brokerem przyciski, heartbeat i watchdog pozostają poprawne;
- `./scripts/check.sh` kończy się kodem 0 bez nowych ostrzeżeń.

### Decision Log

- 2026-07-27: Testowy Home Assistant działa pod `192.168.0.115`, a Mosquitto przyjmuje połączenia TCP na porcie `1883`.
- 2026-07-27: Zgodnie z aktualnym zaleceniem Home Assistant pięć encji zostanie opublikowanych jednym komunikatem MQTT Device Discovery; discovery prefix pozostaje konfigurowalny z domyślnym `homeassistant`.
- 2026-07-27: Limity buforów RAM etapu testowego nie stanowią jeszcze formatu 464-bajtowego rekordu AT24C08N.
- 2026-07-27: ESP-IDF 6 używa zarządzanych komponentów `espressif/mqtt` 1.0.0 i `espressif/cjson` 1.7.19~2; nie dodano Arduino ani innego runtime.
- 2026-07-27: Etap LAN kompiluje tylko `mqtt://`. `mqtts://` wróci po decyzji o źródle CA.
- 2026-07-27: PlatformIO wymaga jawnego `board_build.partitions = partitions_singleapp_large.csv`; samo ustawienie Kconfig ESP-IDF nie zmieniało generowanej przez PlatformIO tablicy.

### Outcomes & Retrospective

- 2026-07-27: Build `0.6.0-test` zakończył się bez ostrzeżeń projektu. Użycie wynosi 47 264 B RAM i 1 035 345 B flash z partycji 1 536 000 B (67,4%).
- 2026-07-27: Odczyt `partitions.bin` potwierdził `nvs` 24 KiB, `phy_init` 4 KiB i pojedynczą partycję `factory` 1500 KiB od `0x10000`.
- 2026-07-27: Pełny `./scripts/check.sh` ma status PASS: 71 plików UTF-8 bez BOM, build bez ostrzeżeń i wszystkie 26 scenariuszy hostowych zaliczone. Pozostaje upload i HIL.
- 2026-07-27: Pełny obraz `0.6.0-test`, bootloader i nowa tablica partycji zostały zapisane na COM4; esptool zweryfikował wszystkie bloki. HIL MQTT/Home Assistant pozostaje w toku.
- 2026-07-27: Częściowy HIL MQTT potwierdził połączenie z brokerem, sterowanie kanałami 1, 2 i 5 oraz jasnością kanału 1 z Home Assistant. Zmiany były widoczne na LCD, a lokalna zmiana kanału 1 została poprawnie odesłana i pokazana w HA. OFF oraz programowy `transition` zostały potwierdzone, w tym pomiarem PWM.
- 2026-07-28: Reconnect HIL ma status PASS. Po EOF klient ponawiał połączenie co około 5 s, lokalna regulacja kanału 1 działała podczas awarii, a po około 25 s klient połączył się z uruchomionym brokerem i wykonał kolejną komendę z HA. Monotoniczny uptime potwierdził brak resetu; pozostaje wizualne potwierdzenie heartbeat GPIO2.
- 2026-07-28: Końcowy HIL MQTT/Home Assistant ma status PASS. `mqtt status` potwierdził drugie połączenie, 13 przyjętych i 0 odrzuconych komend oraz brak zmian oczekujących na `apply`; GPIO2 nadal migało co sekundę. Etap v0.6-test jest zamknięty, a odłożone poprawki LCD przechodzą do następnej aktualizacji.

---

## Zakończony plan: Wi-Fi STA i czas NTP v0.5-test

### Cel

Uruchomić testowe Wi-Fi STA konfigurowane z konsoli UART, bez zapisu SSID, hasła ani IPv4 do pamięci trwałej. Po uzyskaniu adresu IP firmware synchronizuje zegar z NTP i prezentuje czas lokalny Warszawy z automatycznym CET/CEST.

### Progress

- [x] Uzgodniono czytelne polecenia `wifi ...` zamiast emulowania modemu komendami AT.
- [x] Uzgodniono konfigurację przechowywaną tylko w RAM na czas testów.
- [x] Dodać usługę Wi-Fi będącą właścicielem konfiguracji i wywołań ESP-NETIF/Wi-Fi.
- [x] Dodać konsolę UART0 115200 z niewidocznym wprowadzaniem hasła.
- [x] Dodać DHCP, statyczne IPv4, DNS, skanowanie, status i kontrolowany reconnect.
- [x] Dodać SNTP `pool.ntp.org` oraz strefę Warszawy z regułą CET/CEST.
- [x] Zbudować firmware i uruchomić testy automatyczne.
- [x] Wgrać poprawiony obraz na COM4 z ręcznym wejściem BOOT/EN i potwierdzić czysty rozruch.
- [x] Usunąć samozakleszczenie mutexu po `WIFI_EVENT_STA_CONNECTED` i odseparować task kontrolera od blokad Wi-Fi.
- [x] Wgrać hotfix `0.5.1-test` i powtórzyć 90-sekundowy test połączenia z AP bez resetu Task WDT.
- [x] Potwierdzić HIL połączenie z AP, adres IP i synchronizację NTP.

### Kontrakt

- `wifi_init_config_t.nvs_enable` pozostaje wyłączone, a konfiguracja sterownika używa `WIFI_STORAGE_RAM`; wewnętrzne NVS przechowuje wyłącznie kalibrację PHY.
- Reset albo utrata zasilania usuwa SSID, hasło, tryb IPv4 i DNS.
- Hasło jest wprowadzane osobnym promptem bez echa, nie pojawia się w historii, statusie ani logach.
- Domyślnym trybem adresacji jest DHCP; statyczne IPv4 wymaga jawnych wartości IP, maski i bramy.
- Wi-Fi i konsola startują dopiero po uruchomieniu lokalnego kontrolera PWM, przycisków, LCD i watchdoga.
- Brak AP, DNS albo NTP nie zatrzymuje i nie resetuje sterowania lokalnego.
- Task kontrolera pobiera status sieci z zerowym czasem oczekiwania i zachowuje ostatnią poprawną kopię dla LCD.
- Strefa czasowa używa reguły `CET-1CEST,M3.5.0/2,M10.5.0/3`: ostatnia niedziela marca i października.
- Zegar jest uznawany za zsynchronizowany dopiero po zdarzeniu SNTP; wcześniej status nie przedstawia czasu jako poprawnego.

### Acceptance criteria

- `wifi status` po resecie pokazuje brak konfiguracji i magazyn RAM;
- `wifi password` nie wyświetla ani nie loguje wpisywanych znaków;
- po `wifi apply` z DHCP status pokazuje SSID, RSSI, IP, maskę, bramę i DNS;
- tryb statyczny zatrzymuje klienta DHCP i stosuje zwalidowane wartości IPv4;
- po synchronizacji `wifi status` pokazuje datę, godzinę i `CET` albo `CEST`;
- ponowne uruchomienie nie odtwarza danych sieciowych;
- `./scripts/check.sh` kończy się kodem 0 bez nowych ostrzeżeń;
- test HIL potwierdza synchronizację czasu oraz dalsze działanie lokalnych przycisków przy braku sieci.

### Decision Log

- 2026-07-27: Pierwszy provisioning odbywa się przez UART0/COM4 i czytelne polecenia `wifi`, nie przez komendy AT ani captive portal.
- 2026-07-27: Wersja testowa nie używa NVS ani niezamontowanego AT24C08N do konfiguracji Wi-Fi.
- 2026-07-27: Źródłem czasu jest `pool.ntp.org`, a reguła strefy Warszawy jest obliczana przez bibliotekę czasu na podstawie daty.
- 2026-07-27: Żadna blokada usług sieciowych nie może być przejmowana w trybie oczekiwania przez kontroler LED; odczyt do LCD używa wyłącznie `tryGetStatus()`.

### Outcomes & Retrospective

- 2026-07-27: `./scripts/check.sh` zakończył się kodem 0: 65 plików UTF-8 bez BOM, build bez ostrzeżeń i 24 scenariusze hostowe PASS.
- 2026-07-27: Obraz używa 38 720 B RAM i 891 729 B flash, czyli 85,0% obecnej partycji aplikacji 1 MiB. Przed OTA trzeba ustalić docelową tabelę partycji.
- 2026-07-27: Dwie automatyczne próby uploadu na obecny COM4 nie przełączyły ESP32 do ROM bootloadera (`Invalid head of packet (0x65)`). Port otwiera się, więc nie jest zablokowany przez inny proces; następna próba wymaga ręcznej sekwencji BOOT/EN.
- 2026-07-27: Ręczna sekwencja BOOT/EN umożliwiła dwa poprawne uploady. Pierwszy 90-sekundowy HIL potwierdził konsolę RAM-only i skan 13 sieci, ale ujawnił brak inicjalizacji NVS kalibracji PHY oraz starą wersję w metadanych obrazu.
- 2026-07-27: Hotfix inicjalizuje NVS wyłącznie dla PHY i ustawia `PROJECT_VER=0.5.0-test`. Końcowy build używa 38 944 B RAM oraz 901 565 B flash, czyli 86,0% partycji aplikacji 1 MiB.
- 2026-07-27: Drugi upload i 90-sekundowe okno HIL potwierdziły `App version: 0.5.0-test`, wyłączone NVS konfiguracji Wi-Fi, brak błędu kalibracji, pięć PWM OFF, GPIO19 HIGH, poprawny LCD, działającą konsolę oraz skan 12 sieci. Nie wystąpił błąd I2C ani reset watchdog.
- 2026-07-27: Pierwsza próba z rzeczywistym AP uzyskała połączenie, DHCP i RSSI, lecz po około 6 s Task WDT zresetował ówczesny task `corridor_control`. Przyczyną było ponowne przejęcie tego samego nierekurencyjnego mutexu przez `refreshLinkDetails()` wywołane spod handlera `WIFI_EVENT_STA_CONNECTED`; kontroler następnie zablokował się na `status()` podczas tworzenia migawki LCD.
- 2026-07-27: Hotfix `0.5.1-test` usuwa zagnieżdżone przejęcie i dodaje nieblokujące `tryGetStatus()` z cache ostatniej poprawnej migawki. Bieżący `./scripts/check.sh` przechodzi: 65 plików UTF-8 bez BOM, 24 scenariusze hostowe, 39 024 B RAM i 901 809 B flash.
- 2026-07-27: Końcowy HIL hotfixu ma status `PASS`: DHCP przydzielił IPv4, DNS umożliwił uruchomienie `pool.ntp.org`, NTP zsynchronizował czas CEST, a LCD pokazał prawidłowe dane sieciowe i czas. Po ponad 90 s od połączenia GPIO13 nadal sterował kanałem, GPIO2 zachował heartbeat i nie wystąpił Task WDT.
- 2026-07-27: Otwarcie COM4 na obecnym konwerterze USB-UART może wywołać `POWERON_RESET` przez DTR/RTS i skasować konfigurację RAM-only. Właściwy test wykonano po otwarciu PuTTY i pozostawieniu tej samej sesji aktywnej; wcześniejsze próby przerwane resetem nie były zaliczane.
- 2026-07-28: Potwierdzono, że również zamknięcie PuTTY może przełączyć DTR/RTS i zresetować ESP32. `monitor_dtr=0` oraz `monitor_rts=0` chronią monitor PlatformIO, ale nie sterują zewnętrznym terminalem; testy RAM-only wymagają jednej stale otwartej sesji.
- 2026-07-27: Strona Wi-Fi LCD została skrócona z `WIFI -NNdBm SSID` do `-NNdBm SSID`, aby przeznaczyć więcej znaków na nazwę sieci. Zmiana przechodzi test hostowy, ale nie została jeszcze wgrana na ESP32.

---

## Zakończony etap fundamentu: szyfrowane ustawienia v0.4

### Cel

Przygotować bezpieczny format i adapter AT24C08N dla przyszłych danych Wi-Fi/MQTT bez jawnych sekretów, bez blokowania lokalnego sterowania i z odpornością na przerwanie zasilania podczas zapisu. Późniejsza decyzja v0.6.5 wykluczyła jasność oraz stany LED z trwałego payloadu.

### Progress

- [x] Zidentyfikowano AT24C08N-SU27 jako wariant pracujący od 2,7 V i przyjęto zasilanie 3,3 V.
- [x] Przydzielono wspólne I2C0: SDA GPIO21, SCL GPIO22, 100 kHz; EEPROM trafia na stronę 3,3 V translatora LCD.
- [x] Dodano `CONFIG_LED_CONTROLLER_AT24C08=n`, które usuwa cały stos pamięci z domyślnego buildu.
- [x] Dodano adapter czterech bloków `0x50`–`0x53`, stron 16 B i ACK pollingu bez `delay_ms`.
- [x] Zdefiniowano dwa sloty po 512 B z wersją, sekwencją, generacją klucza, nonce i tagiem.
- [x] Dodano AES-256-GCM przez publiczne API PSA Crypto ESP-IDF 6.
- [x] Dodano maszynę zapisu: invalidate, strony, commit i pełna weryfikacja odczytu.
- [x] Dodano trzy scenariusze hostowe formatu rekordu; pełny zestaw 23 testów przechodzi.
- [x] Zaktualizowano kontrakt sprzętowy, architekturę, roadmapę i model bezpieczeństwa.
- [x] Dodać provider losowego klucza urządzenia w NVS z raportowaniem stanu Flash Encryption.
- [x] Zdefiniować binarny payload ustawień Wi-Fi/MQTT i korekcji INA mieszczący się w 464 B, bez pól LED.
- [x] Uruchomić magazyn w zadaniu będącym jedynym właścicielem wspólnego I2C po starcie sterowania lokalnego.
- [ ] Wykonać test HIL AT24C08N, PSA AES-GCM i przerwania zasilania.

### Kontrakt

- EEPROM nigdy nie otrzymuje jawnego payloadu przez publiczne API usługi ustawień.
- Klucz 256-bitowy nie znajduje się w EEPROM, repozytorium, logach ani stałej wspólnego firmware'u.
- Każdy zapis używa nowego 96-bitowego nonce i 128-bitowego tagu GCM.
- Błąd uwierzytelnienia zeruje bufor wyjściowy i nie zwraca częściowego plaintextu.
- Poprzedni slot pozostaje ważny do zakończenia i zweryfikowania nowego zapisu.
- Awaria EEPROM, klucza lub danych wyłącza usługi zależne od konfiguracji, ale nie PWM, przyciski, LCD ani watchdog.
- Flash Encryption, Secure Boot i eFuse nie są aktywowane automatycznie na płytce COM4.

### Plan działania

1. Provider klucza, payload TLV i integracja zadania I2C zostały wykonane w v0.6.5-test.
2. Test znanych wektorów PSA AES-GCM i surowego obrazu EEPROM pozostaje częścią HIL po montażu pamięci.
3. Produkcyjny provisioning Flash Encryption/Secure Boot pozostaje osobnym etapem.

### Acceptance criteria

- `./scripts/check.sh` kończy się kodem 0 bez ostrzeżeń;
- surowy obraz 1024 B nie zawiera SSID, hasła ani fragmentów payloadu;
- zmiana bitu AAD, szyfrogramu albo tagu jest odrzucana;
- odcięcie zasilania na dowolnym etapie zapisu zachowuje poprzedni slot albo bezpieczny brak konfiguracji;
- zapis nie używa stałego opóźnienia i nie wykonuje I2C w zadaniu kontrolera;
- brak EEPROM lub klucza nie wpływa na lokalne sterowanie;
- test produkcyjny potwierdza ochronę klucza niezależną od samego EEPROM.

### Decision Log

- 2026-07-26: Wybrano zewnętrzny Atmel AT24C08N zamiast przechowywania danych sieciowych w jawnym `sdkconfig`.
- 2026-07-26: Nadruk `SU27` potwierdza wariant 2,7–5,5 V; pamięć pracuje bezpośrednio przy 3,3 V.
- 2026-07-27: Zmieniono decyzję PCB: EEPROM współdzieli I2C0 GPIO21/22 z PCF8574AT i korzysta ze strony 3,3 V translatora.
- 2026-07-27: Do czasu montażu układu `CONFIG_LED_CONTROLLER_AT24C08=n` pomija sterownik, rekordy, AES-GCM i magazyn w firmware.
- 2026-07-26: Szyfrowanie to AES-256-GCM, a nie szyfr bez uwierzytelnienia ani własny algorytm.
- 2026-07-26: Nie ma klucza domyślnego i nie wolno wyprowadzać go wyłącznie z publicznego MAC.
- 2026-07-26: Docelowo preferowany jest klucz w wewnętrznym NVS chronionym przez Flash Encryption wraz z Secure Boot; aktywacja zabezpieczeń wymaga osobnego zatwierdzenia procesu.
- 2026-08-09: Zgodnie z decyzją użytkownika rekord nie zawiera jasności ani stanu ON/OFF kanałów LED.
- 2026-08-09: Provider, payload TLV i automatyczny zapis zostały uruchomione w `v0.6.5-test`; pamięć fizyczna nadal czeka na HIL.

### Outcomes & Retrospective

- 2026-07-27: Domyślny build ESP-IDF 6.0.1 z EEPROM-em wyłączonym przeszedł bez ostrzeżeń i nie zawiera czterech źródeł pamięci w grafie Ninja. Obraz używa 14 412 B RAM i 186 617 B flash; kontrolny wariant `CONFIG_LED_CONTROLLER_AT24C08=y` również się kompiluje.
- 2026-07-27: Kontrola 61 plików tekstowych potwierdza poprawny UTF-8 bez BOM.
- 2026-07-27: CMake/CTest przechodzi z 23 scenariuszami, w tym walidacją nagłówka, uszkodzeń, przepełnienia sekwencji i rotacji LCD.
- 2026-07-26: Testów sprzętowych EEPROM ani wykonania PSA AES-GCM na ESP32 jeszcze nie przeprowadzono.

---

## Równoległy plan HIL: LCD 2004 v0.3.4

### Cel

Dodać opcjonalny ekran LCD 20x4 HD44780 przez PCF8574AT oraz przycisk podświetlenia na GPIO19 bez uzależniania lokalnego sterowania PWM od I2C.

### Progress

- [x] Uzgodniono model LCD, ekspander, adres, translację poziomów i wejście przycisku.
- [x] Dodano testowalny model czterech wierszy ekranu.
- [x] Dodano natywny adapter I2C/PCF8574A/HD44780.
- [x] Dodano obsługę przycisku podświetlenia z zewnętrznym 10 kOhm i wewnętrznym pull-upem.
- [x] Zintegrowano ekran z kontrolerem i odwracalną sygnalizację błędu LCD.
- [x] Build, testy hostowe i kontrola UTF-8 przechodzą.
- [x] Wgrano firmware v0.3.0 przez COM4 i potwierdzono start PCF8574AT pod adresem `0x3F`.
- [x] Wykonano wizualny test HIL treści ekranu, mapy backpacka i podświetlenia.
- [x] Usunięto cache wierszy i wymuszono pełne, jawnie adresowane odrysowanie 20x4 co 1 s.
- [x] Zbudowano i wgrano hotfix v0.3.1 na COM4.
- [x] Powtórzono test GPIO13 dla hotfixu v0.3.1.
- [x] Włączono wewnętrzne pull-upy dla wszystkich sześciu przycisków.
- [x] Potwierdzono stan spoczynkowy HIGH na GPIO19.
- [x] Potwierdzono działanie przycisku podświetlenia GPIO19.
- [x] Dodano odzyskiwanie po dowolnym błędzie I2C: odbudowę magistrali, reinicjalizację, czyszczenie i pełne odrysowanie LCD.
- [x] Zbudowano v0.3.2 i uruchomiono testy hostowe ścieżki kasowania błędu diagnostycznego.
- [x] Wgrano v0.3.2 na COM4 i potwierdzono automatyczny powrót LCD po rzeczywistym błędzie I2C podczas startu.
- [x] Dodano kontrolę resetu PCF8574AT, dwa przebiegi inicjalizacji i dwie ramki potwierdzające odzyskanie w v0.3.3.
- [x] Zbudowano i wgrano v0.3.3 na COM4; UART potwierdził wielokrotne odzyskanie po odłączaniu.
- [x] Potwierdzono zimny lut DB6 LCD i poprawiono połączenie.
- [x] Usunięto pole `PWM 19.5k` z trzeciego wiersza i dodano rotację status/uptime co 2 s.
- [x] Wydzielono jednego właściciela wspólnego I2C0 dla LCD i przyszłego EEPROM-u.
- [x] Wgrano v0.3.4 na COM4; UART potwierdził czysty start i gotowość LCD po około 800 ms.
- [x] Potwierdzono wizualnie układ trzeciego wiersza oraz rotację czwartego.

### Kontrakt

- LCD 2004, kontroler zgodny z HD44780, tryb 4-bitowy;
- ekspander PCF8574AT, adres 8-bitowy zapisu `0x7E`, czyli adres 7-bitowy `0x3F` dla ESP-IDF;
- I2C 100 kHz, SDA GPIO21, SCL GPIO22, zewnętrzna translacja BSS138 3,3/5 V;
- standardowa mapa backpacka: P0 RS, P1 RW, P2 E, P3 podświetlenie, P4-P7 D4-D7;
- wszystkie przyciski aktywne LOW z wewnętrznym pull-upem; GPIO19 ma dodatkowo zewnętrzny pull-up 10 kOhm, debounce 25 ms;
- podświetlenie domyślnie ON;
- pierwsza migawka po inicjalizacji, następne najwyżej raz na 1000 ms;
- timeout pojedynczej transakcji I2C 20 ms, wszystkie operacje magistrali w osobnym zadaniu;
- każda awaria I2C uruchamia ponowienie co 2 s, niezależnie od miejsca i rodzaju błędu;
- błąd I2C jest kasowany dopiero po odbudowie magistrali, pełnej inicjalizacji z czyszczeniem i poprawnym odrysowaniu 4x20;
- ekran pokazuje ON/OFF i jasność pięciu kanałów; trzeci wiersz nie zawiera pola PWM;
- przy stanie OK czwarty wiersz przełącza co 2 s status i uptime, a alarm pozostaje stale widoczny.

### Plan działania

1. Wydzielić `LcdDashboard`, który bez ESP-IDF formatuje dokładnie cztery wiersze po 20 znaków.
2. Zaimplementować inicjalizację HD44780 i zapisy PCF8574A przez nowe API master I2C ESP-IDF 6.
3. Ograniczyć odświeżanie ekranu do 1 Hz, użyć kolejki najnowszej migawki i przy każdej ramce jawnie zapisać cztery wiersze po 20 znaków.
4. Wykonywać I2C w osobnym zadaniu; po dowolnym błędzie pozostawić PWM oraz przyciski aktywne i co 2 s próbować odbudować magistralę oraz cały stan LCD.
5. Uruchomić pełną kontrolę jakości, wgrać obraz na COM4 i sprawdzić sprzęt.

### Acceptance criteria

- pięć kanałów mieści się na ekranie 20x4 bez przewijania;
- short press oraz długie przytrzymanie aktualizują stan i jasność na ekranie;
- przycisk GPIO19 przełącza podświetlenie dokładnie raz na naciśnięcie;
- odłączenie LCD nie zatrzymuje taska kontrolera ani watchdoga;
- po usunięciu dowolnej awarii I2C ekran zostaje wyczyszczony i odtworzony, a bieżąca flaga błędu znika;
- GPIO2 wraca po odzyskaniu I2C do heartbeat, chyba że pozostaje inny trwały błąd lub ślad resetu watchdog;
- `./scripts/check.sh` kończy się kodem 0 bez ostrzeżeń;
- test HIL potwierdza adres, mapę PCF8574A, kolejność wierszy i polaryzację podświetlenia.

### Decision Log

- 2026-07-26: Adres `0x7E` podany przez użytkownika jest adresem 8-bitowym zapisu; sterownik używa adresu 7-bitowego `0x3F`.
- 2026-07-26: Magistrala ma zewnętrzny translator BSS138, dlatego firmware nie włącza wewnętrznych pull-upów ESP32.
- 2026-07-26: LCD jest adapterem opcjonalnym; jego awaria może zapalić LED diagnostyczną, ale nie może zatrzymać lokalnego sterowania.
- 2026-07-26: Dashboard jest publikowany najwyżej raz na sekundę; szybsze odświeżanie nie jest potrzebne dla tego wyświetlacza.
- 2026-07-26: Całe I2C działa w zadaniu o niższym priorytecie, a kontroler używa wyłącznie nieblokującej kolejki i powiadomień.
- 2026-07-26: Standardowa mapa DDRAM 20x4 to `0x00`, `0x40`, `0x14`, `0x54`; przepełnienie wiersza 1 przechodzi do 3, a wiersza 2 do 4.
- 2026-07-26: Rezygnujemy z cache zmienionych wierszy. Pełna ramka 1 Hz pozwala samoczynnie naprawić przekłamanie DDRAM widoczne po teście GPIO13.
- 2026-07-26: Na polecenie użytkownika wszystkie przyciski, włącznie z GPIO19, korzystają z wewnętrznych pull-upów. Zewnętrzny 10 kOhm na GPIO19 pozostaje.
- 2026-07-26: Każdy błąd zwrócony przez adapter I2C jest traktowany jednakowo; nie ma ścieżki specjalnej tylko dla SCL. Ponowienie następuje co 2 s poza taskiem sterującym.
- 2026-07-26: Flaga błędu I2C i stałe świecenie GPIO2 są kasowane dopiero po odbudowie magistrali, pełnej inicjalizacji HD44780 z czyszczeniem, przywróceniu podświetlenia i zapisie najnowszej ramki.

### Outcomes & Retrospective

- 2026-07-26: Pełny build ESP-IDF 6.0.1 zakończył się bez ostrzeżeń. Firmware używa 14 464 B RAM i 184 125 B flash; 19 scenariuszy hostowych przechodzi.
- 2026-07-26: Ścisła kontrola 51 plików tekstowych wykazała 0 błędów UTF-8 i 0 plików z BOM.
- 2026-07-26: Obraz v0.3.0 wgrano na COM4. UART potwierdził czysty start, adres LCD `0x3F`, I2C 100 kHz i brak błędu lub resetu przez 8 s.
- 2026-07-26: Użytkownik potwierdził poprawne działanie LCD, treści czterech wierszy i podświetlenia na sprzęcie.
- 2026-07-26: Po zwarciu GPIO13 zaobserwowano przekłamanie treści wierszy. Adresy DDRAM i limit 20 znaków w kodzie były poprawne; hotfix v0.3.1 wymusza pełne odrysowanie zamiast pozostawiania uszkodzonego cache po stronie LCD.
- 2026-07-26: `scripts/check.sh` dla v0.3.1 zakończył się kodem 0; 19 testów przechodzi, firmware używa 14 380 B RAM i 184 209 B flash.
- 2026-07-26: V0.3.1 wgrano na COM4. Przez 8 s pełne ramki 1 Hz działały bez błędu I2C lub resetu.
- 2026-07-26: UART wykazał `GPIO19 przy starcie: LOW`. Przy puszczonym przycisku i zewnętrznym pull-upie 10 kOhm oczekiwany jest HIGH; brak reakcji podświetlenia powstaje przed obsługą PCF8574.
- 2026-07-26: Po włączeniu wewnętrznego pull-upu i ponownym uploadzie UART wykazał `GPIO19 przy starcie: HIGH`. Wszystkie sześć przycisków loguje bias wewnętrzny.
- 2026-07-26: Podczas 30 s monitorowania nie zarejestrowano zdarzenia GPIO19; test bezpośredniego zwarcia pinu do GND pozostaje do wykonania.
- 2026-07-26: Użytkownik potwierdził poprawne przełączanie podświetlenia po bezpośrednim teście GPIO19.
- 2026-07-26: Ponowny test GPIO13 potwierdził poprawną treść LCD po włączeniu pełnego odrysowania 4x20 raz na sekundę.
- 2026-07-26: Odłączenie SCL na v0.3.1 potwierdziło zatrzymanie operacji LCD, stałe świecenie GPIO2 oraz dalsze działanie lokalnego sterowania.
- 2026-07-26: `scripts/check.sh` dla v0.3.2 zakończył się kodem 0; 19 scenariuszy przechodzi, firmware używa 14 376 B RAM i 185 157 B flash.
- 2026-07-26: V0.3.2 wgrano na COM4. Pierwsza inicjalizacja zwróciła rzeczywisty `ESP_ERR_INVALID_RESPONSE`; GPIO2 przeszedł w alarm, a po około 2 s firmware odtworzył I2C, wykonał pełną inicjalizację i ramkę oraz skasował błąd bez resetu MCU.
- 2026-07-26: Kolejne 35 s pracy nie przyniosło błędu ani resetu. W tym oknie nie zarejestrowano celowego odłączenia SDA, dlatego ręczny test przerwania i przywrócenia SDA na v0.3.2 pozostaje do wykonania.
- 2026-07-26: V0.3.3 wykryła reset PCF i dwa kolejne okresy awarii I2C, po czym za każdym razem wykonała podwójną inicjalizację i dwie ramki bez resetu ESP32.
- 2026-07-27: Potwierdzono zimny lut połączenia P6 PCF8574AT z DB6 LCD. Lut został poprawiony i wyświetlacz działa prawidłowo.
- 2026-07-27: V0.3.4 wgrano na COM4. Ośmiosekundowy log po `POWERON_RESET` potwierdził start pięciu kanałów OFF, GPIO19 HIGH, podwójną inicjalizację LCD `0x3F` po około 800 ms oraz brak błędu I2C i resetu.
- 2026-07-27: Test HIL potwierdził prawidłowy trzeci wiersz bez pola PWM oraz rotację `STATUS: OK`/uptime co 2 s.

---

## Zakończony plan: hotfix LEDC v0.2.1

### Cel

Usunąć reset watchdog występujący po pierwszej zmianie duty i przywrócić działanie lokalnych przycisków bez uruchamiania sprzętowej usługi fade LEDC.

### Progress

- [x] Odtworzono błąd z logu UART i zdekodowano backtrace.
- [x] Potwierdzono wymagania API w źródłach ESP-IDF 6.0.1.
- [x] Zastąpiono `ledc_set_duty_and_update()` parą `ledc_set_duty()` i `ledc_update_duty()`.
- [x] Dodano zatrzaskiwanie błędu kanału oraz jednorazowe przejście do LOW.
- [x] Dodano statyczną kontrolę niedozwolonych wywołań fade do `scripts/check.sh`.
- [x] Pełny build i testy hostowe przechodzą.
- [x] Wgrano firmware przez COM4 i sprawdzono czysty start v0.2.1.
- [x] Sprawdzono PWM po fizycznym naciśnięciu przycisku.
- [x] Wykonano wstępne pomiary częstotliwości, wypełnienia i obu zboczy PWM.
- [ ] Wykonano testy z docelowym torem mocy.

### Przyczyna

`ledc_set_duty_and_update()` w ESP-IDF 6.0.1 wywołuje kontrolę inicjalizacji kanału fade. Firmware świadomie nie instalował usługi fade, więc pierwsze naciśnięcie zwracało `ESP_FAIL`. Kontroler ponawiał zapis co 5 ms, logowanie zajmowało rdzeń, a task watchdog resetował układ po 5 s.

### Plan działania

1. Używać bezpośredniej pary `ledc_set_duty()` i `ledc_update_duty()` wyłącznie z pojedynczego taska kontrolera.
2. Po pierwszym błędzie LEDC zatrzymać kanał z poziomem idle LOW i nie ponawiać operacji w pętli.
3. Zachować polling wszystkich przycisków i działanie pozostałych kanałów.
4. Zbudować firmware, uruchomić testy hostowe i sprawdzić kodowanie UTF-8.
5. Wgrać na COM4 i potwierdzić brak komunikatu `Fade service not installed` oraz brak resetu watchdog.

### Acceptance criteria

- pierwsze krótkie naciśnięcie zmienia duty bez błędu usługi fade;
- kontroler nie wpada w pętlę logowania i nie resetuje się po 5 s;
- błąd jednego kanału daje najwyżej jeden raport kontrolera i zatrzaskuje ten kanał w LOW;
- pozostałe kanały i polling przycisków działają nadal;
- `./scripts/check.sh` kończy się kodem 0 bez ostrzeżeń;
- wszystkie pliki tekstowe projektu są poprawnym UTF-8 bez BOM.

### Decision Log

- 2026-07-26: Nie instalujemy usługi fade tylko po to, aby użyć helpera `ledc_set_duty_and_update()`.
- 2026-07-26: Para set/update jest dopuszczalna, ponieważ tylko kontroler LED modyfikuje LEDC.
- 2026-07-26: Błąd sprzętowego adaptera nie może generować ponowień co 5 ms ani zagłodzić zadań idle.

### Outcomes & Retrospective

- 2026-07-26: `scripts/check.sh` zakończył się kodem 0. Firmware używa 14 152 B RAM i 162 701 B flash; 17 scenariuszy hostowych przechodzi.
- 2026-07-26: Obraz v0.2.1 wgrano na COM4. Przechwycono czysty start pięciu kanałów i pięciu przycisków bez błędu fade ani resetu przez 30 s.
- 2026-07-26: Ścisła kontrola wszystkich plików projektu wykazała 0 błędów UTF-8, 0 BOM i 0 podejrzanych sekwencji mojibake.
- 2026-07-26: Oscyloskop potwierdził 19,531 kHz, wypełnienie około 61% dla zadanej jasności 61% oraz poprawne poziomy logiczne. Dalsza walidacja nastąpi po podłączeniu driverów i MOSFET-ów.
- Poprzedni build oraz 17 testów hostowych nie wykryły wymagania runtime narzuconego przez helper ESP-IDF. Pierwszy test HIL ujawnił regresję przy zmianie duty.

---

## Plan v0.2: kompletne sterowanie lokalne

### Cel i wynik

Dodano deterministyczną obsługę krótkiego i długiego naciśnięcia dla pięciu kanałów. `ButtonGesture`, `ChannelState` i `DutyTransition` są niezależne od ESP-IDF i testowane na hoście.

### Zrealizowane zachowanie

- short press przełącza kanał po zwolnieniu;
- long press zaczyna się po 500 ms i nie generuje toggle;
- hold od OFF startuje z 1% i rozjaśnia;
- pierwszy hold od ON ściemnia, kolejne zmieniają kierunek;
- jasność zmienia się w zakresie 1–100% co 1% na 40 ms;
- fade toggle trwa 250 ms i może zostać odwrócony bez blokowania pollingu;
- 17 scenariuszy hostowych przechodziło przed testem HIL.

### Niezakończone testy

- pomiar pięciu wyjść PWM na płytce;
- test z driverami, MOSFET-ami i ograniczonym obciążeniem;
- walidacja bezpiecznego OFF podczas resetu.

---

## Plan v0.1: migracja na ESP32 DOIT i stabilizacja

### Zrealizowane

- target `esp32doit-devkit-v1`, ESP-IDF i C++17;
- pięć kanałów PWM oraz pięć przycisków zgodnych z kontraktem sprzętowym;
- bezpieczna kolejność startu: GPIO LOW przed LEDC i duty 0 przed taskiem;
- zewnętrzne pull-up 10 kOhm oraz logika fail-high wejść PIR GPIO34/35;
- LED diagnostyczna GPIO2 i task watchdog 5 s;
- upload i monitor przypięte do COM4;
- testy hostowe CMake/CTest uruchamiane przez `scripts/check.sh`.

### Niezakończone testy

- pełne testy elektryczne i funkcjonalne po dostarczeniu toru mocy oraz czujek PIR.

---

## Szablon następnego planu

### Cel

Opisz obserwowalny rezultat, nie tylko listę plików.

### Progress

- [ ] Kroki z datami aktualizacji.

### Plan działania

Opisz kolejność, miejsca w kodzie i sposób zachowania kompatybilności.

### Acceptance criteria

Podaj dokładne komendy oraz oczekiwane rezultaty.

### Outcomes & Retrospective

Podsumuj wynik, dług techniczny i następny krok.

## 2026-09-26 — przygotowanie GitHub

- [x] Zainstalowano GitHub Pull Requests 0.166.1; TLS z systemowym CA.
- [x] git init -b main; rekomendacja rozszerzenia i instrukcja docs/GITHUB.md.
- [x] Wykluczono logi, zachowano wykluczenie sekretów/buildów; dołączono bazowy sdkconfig.
- [x] git check-ignore potwierdza wykluczenie sekretów i artefaktów.
- [ ] Logowanie użytkownika i publikacja prywatnego led-korytarz w VS Code.
- [ ] Osobny hosting HTTPS dla OTA; prywatne Releases wymagają autoryzacji.

Nie zmieniano firmware; test sprzętowy i ponowna kompilacja nie są wymagane dla tego etapu.

## 2026-09-27 — publiczne repozytorium wydań OTA

- [x] Użytkownik zatwierdził oddzielny publiczny hosting wydań binarnych.
- [x] Utworzono Daniel99999999999/led-korytarz-firmware przez GitHub API;
  odpowiedź potwierdza visibility=public. Użyto istniejącego logowania Git bez wypisywania tokenu.
- [x] Uzupełniono docs/GITHUB.md; nie zmieniano firmware ani lokalnych danych MQTT.
- [ ] Publikacja pierwszego firmware.bin (nowa wersja), konfiguracja URL i test OTA na ESP32.
- [ ] Automatyzacja publikacji GitHub Releases z VS Code (obecny uploader tylko pobiera i sprawdza plik).

Walidacja: odpowiedź GitHub API i kontrola UTF-8; kompilacja niewymagana dla dokumentacji.

## Aktywny etap v0.9.0 — PWM4/5 i serwisowe OTA AP

Zakres zatwierdzony: niezależna częstotliwość, minimalne wypełnienie, minimalne ON/OFF,
tryb PWM/ON-OFF kanałów 4/5; lokalny hotspot po 10 s przycisku LCD, 10 min sesji,
strona uploadu, zachowanie konfiguracji STA oraz zdalnego MQTT/HTTPS OTA.

- [x] Odczyt kontraktów i bazowa kontrola check.sh.
- [ ] Model ograniczeń PWM, osobne timery i kolejka kontrolera.
- [ ] HA discovery/komendy/publikacja i migracja rekordu DS1307 v1 -> v2.
- [ ] AP, gest serwisowy, ekran danych dostępu, HTTP upload i wspólna blokada OTA.
- [ ] Testy hostowe, kompilacja, dokumentacja i lista HIL.

Nie zmieniać GPIO ani zapisanej sieci. Nie publikować wydania ani nie wgrywać urządzenia.
Nie utożsamiać ustawień PWM z potwierdzeniem kompatybilności żarówek Diall.

## 2026-10-06 — poprawki audytu v0.9.0

- [x] Przy błędzie LEDC przejąć wyjście jako GPIO LOW i zgłosić niepowodzenie przejęcia.
- [x] Zliczać komendę PWM jako przyjętą lub odrzuconą po decyzji kontrolera, bez blokowania jego zadania.
- [x] Dodać i uruchomić test hostowy awarii `ledc_stop` oraz pierwszego ustawienia GPIO LOW.
- [x] Uruchomić pełną kompilację PlatformIO po uzupełnieniu lokalnych zależności ESP-IDF.

Bez zmiany GPIO, polaryzacji, wersji firmware i zapisanych ustawień. Test na ESP32 pozostaje osobnym krokiem.

## 2026-10-07 — czerwony POWER pilota tylko dla wybranego kanalu

- [x] Zmienic POWER na przelaczanie jednego kanalu 1-5; przy celu WSZYSTKIE nie zmieniac LED.
- [x] Usunac nieuzywany zapis i odtwarzanie stanu wszystkich kanalow.
- [x] Rozszerzyc testy mapowania pilota, dokumentacje i uruchomic `check.sh`.
- [ ] HIL: sprawdzic POWER na wybranych kanalach, brak zmian przy WSZYSTKIE i RGB ON/OFF na grupie.

RGB ON/OFF zachowuje obsluge celu WSZYSTKIE. Bez zmiany GPIO, polaryzacji i ustawien trwalych.
Test na ESP32 pozostaje do wykonania; nie wgrywac firmware automatycznie.

## 2026-10-07 - przygotowanie v0.9.1 do OTA przez GitHub

- [x] Podniesc `PROJECT_VER` z 0.9.0 do 0.9.1 dla poprawki POWER pilota.
- [x] Zbudowac pelny profil `esp32doit-devkit-v1-ota` i sprawdzic deskryptor obrazu.
- [ ] Opublikowac dokladnie ten `firmware.bin` w publicznym Release `v0.9.1`.
- [ ] Uzupelnic lokalny, ignorowany `secrets.ota.json`, porownac SHA-256 obrazu z HTTPS
      i dopiero wtedy uruchomic upload OTA oraz HIL pilota na urzadzeniu.

Publiczne repozytorium wydan nie ma jeszcze Release, a na tym komputerze nie ma
`secrets.ota.json`. Kompilacja profilu OTA przeszla; obraz `led_controller/0.9.1`
ma 1 277 440 B. Nie wykonano publikacji ani aktualizacji ESP32.

## 2026-10-08 - lokalny panel diagnostyczny WWW

- [x] Odczytac kontrakty sprzetowe i architekture; uruchomic bazowe `check.sh`.
- [x] Naprawic niezalezny od funkcji blad kompilacji hostowej MSVC (`strcpy`).
- [x] Dodac osobny, ograniczony bufor logow WWW bez oprozniania bufora MQTT.
- [x] Dodac panel HTTPS na porcie 443: statusy i tylko jawnie dozwolone skany.
- [x] Dodac konto admin; admin/admin w trybie pierwszego logowania wymusza zmiane hasla.
- [x] Zapisac solony weryfikator hasla w zaszyfrowanym AT24C08N, zachowac stare rekordy.
- [x] Wygenerowac certyfikat urzadzenia i pokazac odcisk na UART; bez klucza w repo.
- [x] Zapewnic dostep przez IP i `sterownik-led.local`, bez wymagania LCD.
- [x] Zachowac osobny hotspot OTA HTTP na porcie 80, bez kolizji z HTTPS.
- [x] Rozszerzyc testy hostowe, dokumentacje i uruchomic `check.sh`.
- [ ] HIL: sprawdzic logowanie, odswiezanie, skany, brak sekretow oraz lokalne OTA.

Panel nie zapisuje hasel Wi-Fi/MQTT ani nie steruje bezposrednio LEDC. Certyfikat
i klucz TLS pozostaja w NVS, haslo panelu jako solony weryfikator w AT24C08N.
Brak sieci, NVS, MQTT, EEPROM i LCD nie zatrzymuje lokalnego sterowania.

## 2026-10-08 - v0.10.1: spokojna konsola UART

- [x] Oddzielic strumien ESP_LOG na UART od logow WWW/MQTT i odpowiedzi konsoli.
- [x] Wyciszyc ESP_LOG na UART po starcie REPL; dodac `logs status/on/off`.
- [x] Dodac test hostowy, ze wyciszenie UART zachowuje logi w obu buforach.
- [x] Uruchomic `check.sh` i zbudowac profil `esp32doit-devkit-v1-ota`.
- [ ] HIL: sprawdzic wpisywanie polecen, togglowanie logow i odczyt logow WWW.

Zmiana hasla w lokalnym pliku Mosquitto wymagala restartu procesu brokera.
Po restarcie polaczenie TCP ESP32 z brokerem pozostalo stabilne przez ponad 12 s;
status sesji MQTT z UART pozostaje do potwierdzenia.

## 2026-10-08 - v0.10.2: start HTTPS i publikacja OTA

- [x] Przeanalizowac log bootloopu `0.10.1` i potwierdzic przepelnienie stosu zadania main.
- [x] Uruchomic konsole przed HTTPS i przeniesc inicjalizacje TLS do jednorazowego zadania 16 KiB.
- [x] Podniesc PROJECT_VER do 0.10.2, aby OTA odrzucalo starszy obraz.
- [x] Domknac workflow wydania po tagu, manifest i tryb OTA korzystajacy z Release.
- [x] Uruchomic check.sh, build pelnego profilu OTA oraz testy skryptow.
- [ ] Skonfigurowac sekret GitHub, wykonac pierwszy przebieg Actions i obejrzec Release.
- [ ] HIL: potwierdzic stabilny start, wolny stos zadania HTTPS, UART, panel i MQTT.

Nie publikowac jeszcze taga ani obrazu. Na sprzecie `0.10.1` restartuje sie po
przywroceniu konfiguracji; log wskazuje task main, a dokladna funkcja jest ukryta
przez uszkodzony backtrace. Brak LCD/DS18B20 nie jest przyczyna resetu.
