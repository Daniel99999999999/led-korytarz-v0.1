# Codex — diagnoza MQTT / Home Assistant Discovery / HTTPS w sterowniku LED 0.10.3

**Data opracowania:** 2026-10-12  
**Repozytorium:** `Daniel99999999999/led-korytarz-v0.1`  
**Analizowany stan źródeł:** archiwum gałęzi `main` z 2026-10-08, firmware `0.10.3`  
**Cel:** znaleźć pierwotne przyczyny niestabilności, wdrożyć minimalną i bezpieczną poprawkę oraz przygotować testy. Docelową wersję poprawkową można oznaczyć `0.10.4` **dopiero po weryfikacji**.

> **Wiadomość do Codexa:** Ten plik stanowi przekazanie ustaleń i zadanie techniczne, nie specyfikację zmian do wykonania bez weryfikacji. Najpierw przeczytaj aktualny kod (mógł się zmienić od 2026-10-08), `AGENTS.md`, `CODEX_HANDOFF.md`, `PLANS.md`, `docs/ARCHITECTURE.md`, `docs/HARDWARE_CONTRACT.md`, `docs/OPEN_DECISIONS.md` i `docs/WEB_DIAGNOSTICS.md`. Rozdziel **potwierdzone błędy** od **hipotez wymagających instrumentacji**. Nie publikuj firmware ani nie uruchamiaj OTA bez osobnego polecenia użytkownika.

## A. Kontekst problemu

- Urządzenie: klasyczny ESP32, pięciokanałowy sterownik LED, firmware `0.10.3`, ESP-IDF/PlatformIO, C++17.
- MQTT: broker Mosquitto na Raspberry Pi 5 z Home Assistant OS, połączenie nieszyfrowane przez port 1883. Panel diagnostyczny ESP32 działa oddzielnie po HTTPS/443 i pozwala odczytywać logi bez UART.
- ESP32 widoczny w logu pod adresem lokalnym `192.168.1.50`; broker używany przez ESP32 `192.168.1.130:1883`; Client ID: `sterownik_led_582abd7c8874`. Te wartości służą **wyłącznie identyfikacji zdarzeń**; nie hardcoduj ich w firmware.
- Użytkownik zgłosił powtarzane błędy budowy Home Assistant Discovery, rozłączanie MQTT, błędy `errno=12` oraz problemy z handshake HTTPS przy otwartym panelu WWW.
- Kilkugodzinny test z brokerem Mosquitto na PC był wcześniej stabilny, lecz warunki tego testu (identyczny build, aktywne HTTPS, obciążenie, konfiguracja brokera) **nie są dostatecznie ustalone**, aby przesądzać o winie RPi5/HA.

## B. Potwierdzone ustalenia z kodu źródłowego 0.10.3

### B1 — Główna wada logiki synchronizacji po połączeniu MQTT [POTWIERDZONE W KODZIE]

**Lokalizacja:** `src/MqttService.cpp`, `taskLoop()` ~497–586, zwłaszcza ~534–544; `synchronizeClient()` ~844–883; `publishDiscovery()` ~1109–1128. Parametry: `src/MqttService.hpp` ~125–135.

1. Gdy klient MQTT zgłasza połączenie (`MQTT_EVENT_CONNECTED`, ~612–623), kod żąda synchronizacji.
2. `synchronizeClient()` subskrybuje po kolei liczne tematy (`esp_mqtt_client_subscribe()`), publikuje `online`, a potem wywołuje `publishDiscovery()`.
3. `publishDiscovery()` zwraca `false`, jeśli nie zbuduje payloadu HA Discovery (`Nie udalo sie zbudowac payloadu Home Assistant Discovery`).
4. `taskLoop()` interpretuje wynik jako nieudaną **całą synchronizację** i ponawia `synchronizeClient()` po `kPublishRetryIntervalMs = 500` ms, włącznie z wszystkimi subskrypcjami. W tym przebiegu powtarza także unieważnianie publish-trackera (`publish_tracker_.invalidateAll()`).
5. Synchronizacja wykonuje kilka wywołań subskrypcji nawet przy problemach transportu i niedoborze zasobów. W logu firmware widać później `Error to send subscribe message`, `Writing didn't complete in specified timeout`, `Poll read error: 12`.

**Wymagana poprawka:** oddziel stany i ponawianie: (i) subskrypcje, (ii) availability/birth, (iii) publikacja Discovery, (iv) pełna migawka stanów. Nie ponawiaj wszystkich SUBSCRIBE dlatego, że nie powstał JSON Discovery. Ogranicz częstotliwość i zastosuj backoff dla błędów trwałych / braku RAM. Upewnij się, że ponowne CONNECT, birth HA i częściowo udana synchronizacja zachowują prawidłową semantykę. Unikaj blokowania obsługi stanów na czas oczekiwania na Discovery. Zweryfikuj dotychczasowy tracker/QoS/retained.

### B2 — Duża chwilowa konsumpcja RAM podczas budowy Discovery [KONSTRUKCJA POTWIERDZONA; PRZYCZYNA AWARII DO POMIARU]

**Lokalizacja:** `src/MqttService.hpp` ~133 i ~243; `src/MqttService.cpp` `buildDiscoveryPayload()` ~1403–1616.

- `kDiscoveryPayloadLength = 24'576` B — statyczny bufor będący polem obiektu `MqttService`, **nie przydzielany dynamicznie przy każdej próbie**.
- `buildDiscoveryPayload()` buduje od zera duże, dynamicznie alokowane drzewo `cJSON` (`cJSON_CreateObject`, kolejne obiekty i tablice), a następnie serializuje je przez `cJSON_PrintPreallocated` do wspomnianego bufora.
- `JsonOwner` z `cJSON_Delete()` zwalnia drzewo po opuszczeniu funkcji, więc **nie ma w tym fragmencie oczywistego dowodu wycieku**. Przy błędach trzeba jednak zmierzyć chwilową konsumpcję, fragmentację i wielkość największej nieudanej alokacji.
- Nie można z dotychczasowego komunikatu odróżnić: niepowodzenia którejś alokacji cJSON, przepełnienia bufora wynikowego, problemu w `local_control::appendDiscovery()` lub `debug_discovery::append()`.
- Istnieje dodatkowe zapotrzebowanie na RAM podczas kolejkowania dużego MQTT PUBLISH z QoS 1/retained.

**Wymagana poprawka:** najpierw instrumentacja rozmiaru payloadu, zużycia heap i miejsca awarii; następnie rozważ serializer strumieniowy / deterministyczny z ograniczonym buforem roboczym albo inne rozwiązanie redukujące szczytowe zużycie sterty. Nie dziel na ślepo pojedynczego komunikatu MQTT Device Discovery na niezgodne ze schematem fragmenty. Zachowaj wszystkie obecne encje i ich `unique_id`, `state_topic`, `command_topic`, retained/availability/OTA. Sprawdź zgodność wygenerowanego JSON z HA.

### B3 — MQTT i HTTPS rywalizują o wewnętrzną pamięć RAM [RYZYKO POTWIERDZONE, STOPIEŃ WPŁYWU DO USTALENIA]

**Lokalizacja:** `src/WebDiagnosticsService.cpp` ~45–46, ~152–163, ~165–181; `sdkconfig.esp32doit-devkit-v1` ~2577–2579; `docs/WEB_DIAGNOSTICS.md`.

- Serwer HTTPS jest uruchamiany niezależnie od przeglądarki; pełne TLS handshake i odpowiedzi WWW konsumują dodatkowy RAM dopiero podczas sesji.
- W `0.10.3` ustawiono `stack_size = 12288`, `max_open_sockets = 1`, `lru_purge_enable = true`. Strona WWW wywołuje `poll()` co **2000 ms** (status, potem logi). Dużo ponownych handshake'ów lub długotrwałe sesje mogą zwiększyć presję pamięci.
- W sdkconfig: `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384`, `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096`. **Nie zmniejszaj globalnych buforów TLS bez testów wpływu na HTTPS OTA/GitHub OTA.**
- `-0x7780` podczas TLS może pochodzić od alertu klienta; sam ten kod nie dowodzi braku RAM. `-0x008D` w logach tego projektu/dokumentacji wskazuje na niewystarczającą pamięć przy inicjalizacji sesji TLS; należy potwierdzić w używanej wersji MbedTLS i skorelować z alokacjami.
- Log `HTTPS RAM` mierzony jest przy obsłudze statusu, maksymalnie co 60 s; jest to RAM wewnętrzny `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`, niekoniecznie cała dostępna pamięć systemu.

**Wymagana poprawka:** zachowaj panel HTTPS i OTA. Zmierz koszty sesji przy przeglądarce zamkniętej, jednej karcie i kilku kartach; sprawdź recykling zasobów sesji, buforowanie odpowiedzi i wpływ odświeżania. Priorytet: stabilność lokalnego sterowania i MQTT przy aktywnym WWW.

### B4 — Status `connected` jest statusem transportu, nie gotowości HA [POTWIERDZONE]

**Lokalizacja:** `src/MqttService.cpp` ~612–623, ~844–883; diagnostyka WWW `src/WebDiagnosticsService.cpp`.

`status_.state = State::connected` następuje po `MQTT_EVENT_CONNECTED`, przed subskrypcjami i Discovery. W efekcie panel może pokazywać `connected`, mimo że konfiguracja HA nie została aktualnie opublikowana. `publish_backlog_` nie jest rzeczywistym rozmiarem outboxu.

**Wymagana poprawka:** rozdziel diagnostycznie `broker_connected`, `subscriptions_ready`, `discovery_queued`, `discovery_confirmed` (gdy istnieje wiarygodne potwierdzenie MQTT/QoS; nie nazywaj enqueue potwierdzeniem), `last_sync_error` i rzeczywisty odczyt outboxu, jeśli udostępnia go ESP-MQTT. Nie zmieniaj bez potrzeby istniejącej semantyki MQTT API na zewnątrz.

### B5 — Konfiguracja buforów MQTT [POTWIERDZONE]

**Lokalizacja:** `src/MqttService.cpp` ~1188–1206; `src/MqttService.hpp` ~133–135.

`config.buffer.size = 8192`, `config.buffer.out_size = 8192`, `config.outbox.limit = 32768`. Limit outboxu nie oznacza od razu zajęcia 32 KiB RAM, lecz duży retain/QoS 1 publish może wymagać znaczącego bufora w kolejce. Zanim zmienisz limity, zmierz rozmiary PUBLISH, zachowanie `esp_mqtt_client_enqueue()` i możliwości biblioteki dla wiadomości większych od bufora roboczego.

## C. Fakty z zebranych logów

### C1 — Log z panelu ESP32, 0.10.3 (użytkownik wkleił do rozmowy)

Kluczowe sekwencje:

```text
mqtt_client: Connection refused, not authorized
mqtt_service: ... connect_return_code=5
mqtt_service: Polaczono z brokerem MQTT
mqtt_service: Nie udalo sie zbudowac payloadu Home Assistant Discovery
mqtt_client: ... transport_read() error: errno=12
mqtt_client: Error to send subscribe message ...
httpd: httpd_accept_conn: session creation failed
wifi:fail to alloc timer, type=5
```

Kolejne próbki `HTTPS RAM` (bajty; wartości bez przepisywania znaczników czasu):

| Wolne B | Największy ciągły blok B | Historyczne minimum wolnego B |
|---:|---:|---:|
| 27416 | 16384 | 13040 |
| 16296 | 15360 | 16 |
| 14092 | 4864 | 16 |
| 12548 | 7424 | 16 |

Interpretacja: silna presja i chwilowe wyczerpanie sterty; `minimum=16` to historyczny low-watermark, **nie bieżąca ilość wolnego RAM**. `errno=12` to błąd alokacji/braku pamięci (ENOMEM) na poziomie operacji transportowej, ale trzeba ustalić jego konkretną ścieżkę.

### C2 — Log serwera Mosquitto `core_mosquitto_2026-10-10T14-06-36.452Z.log.txt`

Analiza tylko zdarzeń ESP32 z **2026-10-10** (cały plik zawiera również znacznie starsze wpisy):

- 15:02:56–15:08:01: **39** odrzuconych prób `not authorised`; broker realnie odrzucał uwierzytelnianie.
- Pierwszy potwierdzony sukces: **15:08:08**, `New client connected` (p4, keepalive 30 s). Później serwer konsekwentnie przyjmuje dane uwierzytelniające.
- Dla tej daty: **20** zaakceptowanych połączeń, **16** `connection closed by client`, **3** `session taken over` (15:09:30, 15:26:49, 15:57:36).
- Ostatnie połączenia potrafiły utrzymywać się znacznie dłużej niż pierwsze; nie występuje nieustanne rozłączanie przez cały okres.

**Ostrożność interpretacyjna:** `session taken over` oznacza, że broker zobaczył nową sesję z tym samym Client ID i zastąpił poprzednią. **Nie dowodzi to samodzielnie istnienia dwóch lokalnych instancji klienta** — może to być reconnect zanim broker wykryje zanik starego gniazda. Zbadaj cykl życia klienta i powody resetowania transportu/Wi-Fi/heap.

### C3 — Diagnostyka integracji HA `config_entry-mqtt-01M0X2WRG005AQCVB0MQ44PM92.json`

- Integracja MQTT HA ma `connected: true`, broker `core-mosquitto`, port 1883.
- HA zna urządzenie „Sterownik LED” z **39 encjami** (5 kanałów LED, 2 PIR, pomiary, konfiguracja KEY/PIR/LCD/PWM, OTA i diagnostyka).
- Rejestrowane były rzeczywiste aktualizacje stanów i telemetryczne dane z urządzenia; w sekcji `mqtt_debug_info` są także zdarzenia `availability` `online` / `offline` skorelowane z 15:26:49 i 15:57:36 czasu lokalnego.
- To **potwierdza działającą transmisję części komunikatów MQTT**, ale **nie dowodzi, że każda nowa próba budowy Discovery w 0.10.3 była udana**. Encje mogą istnieć dzięki uprzedniej poprawnej publikacji lub retained Discovery.
- `mqtt_debug_info.entities` powtarza zdarzenia availability per encja — nie sumuj ich jako unikalnych zdarzeń sieciowych.
- Hasło i login integracji HA występują jako `**REDACTED**`. Nie próbuj odczytywać ani kopiować sekretów.

### C4 — Log `45df7312_zigbee2mqtt_2026-10-10T14-06-00.710Z.log.txt`

Jedyny konkretny błąd startu Zigbee2MQTT pochodzi z **2026-09-15**:

```text
Failed to init port ... Resource temporarily unavailable Cannot lock port
Failed to start EZSP layer with status=HOST_FATAL_ERROR
```

Z2M wskazuje wtedy `/dev/ttyUSB0` dla Sonoff Dongle Plus V2; późniejsze wpisy to głównie próby uruchomienia/onboarding. Samo przełożenie adaptera USB nie potwierdza przyczyny. Problem Zigbee **pozostawić poza zakresem patcha ESP32**, chyba że pojawi się nowy dowód wspólnej przyczyny.

## D. Hipotezy do rozstrzygnięcia pomiarami (nie ogłaszać jako fakt)

1. Czy pierwszym błędem podczas bieżącej synchronizacji jest niedobór RAM w trakcie `cJSON_Create...`, niedostateczny rozmiar output buffer (24 KiB), czy niepowodzenie w jednym z `appendDiscovery()`?
2. Czy krótkie odstępy ponawiania synchronizacji/subscribe zwiększają presję pamięci lub blokują zadanie MQTT na tyle, aby spowodować transport `errno=12` i reconnect?
3. Czy używanie panelu WWW przyspiesza utratę ciągłego bloku heap lub ujawnia istniejący błąd MQTT? Porównać z panelem zamkniętym.
4. Dlaczego broker widzi `connection closed by client` i `session taken over`? Rozróżnić reconnect biblioteki od explicit stop/destroy oraz przyczyn po stronie TCP.
5. Czy build Discovery jest zbyt duży dla obecnego bufora oraz czy ograniczenie alokacji cJSON rozwiązuje problem bez zmian w MbedTLS?

## E. Zakres zadania implementacyjnego dla Codexa — wykonuj etapami

**Etap 0 — inspekcja i bezpieczny baseline**

- Sprawdź, że bieżące repo faktycznie odpowiada 0.10.3; przeczytaj dokumentację wymienioną na początku.
- Uruchom `./scripts/check.sh` oraz `pio run -e esp32doit-devkit-v1`; jeśli narzędzia lub zależności są niedostępne, zgłoś to jawnie, bez deklarowania PASS.
- Zachowaj minimalny diff. Jeśli zmiany staną się większe, najpierw plan w `PLANS.md`.

**Etap 1 — diagnostyka przyczyn awarii**

- Wydziel zwroty błędów `buildDiscoveryPayload()` z dokładną przyczyną i etapem (`root`, `append local control`, `append debug`, `component`, `print buffer overflow`, `enqueue`); nie loguj JSON z sekretami ani nie powoduj zalewu logów.
- Mierz `heap_caps_get_free_size`, `heap_caps_get_largest_free_block`, `heap_caps_get_minimum_free_size` dla `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` przed/po budowie i po publikacji, plus rzeczywisty rozmiar JSON.
- Jeżeli możliwe, obsłuż `heap_caps_register_failed_alloc_callback` w bezpieczny sposób i mierz high-water mark stosu odpowiednich zadań; nie loguj ani nie alokuj w callbacku.
- Dodaj diagnostykę cyklu życia klienta: connect/disconnect, odroczone retry, reason codes, status Wi-Fi, próby subskrypcji, rozmiar outboxu (jeśli API to umożliwia). Bez haseł, kluczy i pełnych tokenów.

**Etap 2 — naprawa pętli synchronizacji**

- Oddziel SUBSCRIBE od publikacji Discovery, availability i snapshotu; nie ponawiaj wszystkich subskrypcji co 500 ms z powodu błędu JSON.
- Zapewnij ograniczony backoff, przywracanie pracy po recovery pamięci/reconnect i prawidłową synchronizację na `homeassistant/status` birth. Zapewnij, że pełna telemetria i sterowanie działają mimo błędu samego Discovery, zgodnie z projektem.
- Nie traktuj samo `enqueueMessage() == true` jako dostarczenia QoS1; jeśli chcesz pokazywać potwierdzenie, uwzględnij odpowiednie zdarzenie MQTT i identyfikator komunikatu.

**Etap 3 — odchudzenie Discovery i współistnienie z HTTPS**

- Po pomiarach wybierz metodę zmniejszenia chwilowych alokacji budowy JSON, zachowując wszystkie 39 encji, identyfikatory i zgodność z Home Assistant MQTT Device Discovery.
- Zweryfikuj, że czas publikowania, rozmiar outboxu i sytuacje przy błędach/błędach drukowania JSON nie powodują przepełnienia/wycieków.
- Nie usuwaj panelu WWW; ewentualne ograniczenia współbieżnych połączeń i częstotliwości aktualizacji uzasadnij pomiarami.
- W dokumentacji `docs/ARCHITECTURE.md` pozostaje starsza informacja „Bufor discovery ma 16 KiB”; źródło 0.10.3 ma 24 576 B. Ujednolić dokumentację po potwierdzeniu docelowych wartości.

**Etap 4 — walidacja**

- Przygotuj test jednostkowy/hostowy dla budowy payloadu: kompletność 39 encji, poprawny JSON, zgodność tematów, payload nie przekracza bufora; błędy alokacji i granica pojemności.
- Sprawdź scenariusz: CONNECT + nieudane Discovery = bez lawiny SUBSCRIBE, bez blokowania świateł i migawki; późniejsze ponowienie Discovery po ustąpieniu błędu.
- Symuluj broker niedostępny, odrzucenie hasła, reconnect, birth HA oraz odcięcie Wi-Fi; brak bootloop/wycieków, zachowane lokalne sterowanie i OTA.
- Test HIL na ESP32: (A) MQTT bez przeglądarki, (B) jedna karta WWW z pollingiem, (C) kilka kart i reconnect, (D) minimum 30–60 min oraz kontrola heap/free-largest/min, (E) wykonanie lokalnego OTA/GitHub OTA z aktywnym TLS.
- Testy sprzętowe oznacz jako *do wykonania*, jeśli Codex nie dysponuje urządzeniem. Nie obiecuj ich wykonania i nie symuluj wyników.

## F. Ograniczenia regresji / zasady projektu

1. **Najważniejsze:** lokalne przyciski monostabilne NO, pięć kanałów LED, PWM4/5, PIR1/PIR2 i algorytm sterowania muszą działać nawet przy braku Wi-Fi/MQTT/HTTPS.
2. Bez zmian GPIO, polaryzacji, pinów zabronionych, ISR i architektury pojedynczego właściciela LEDC. W szczególności nie zmieniać `BoardConfig.hpp` bez konieczności i uzgodnienia.
3. Zachować: HA Discovery, retained availability, MQTT telemetry, OTA z GitHub po HTTPS, lokalne OTA AP, stronę diagnostyczną HTTPS, uwierzytelnianie i brak sekretów w logach.
4. Zachować ESP-IDF + PlatformIO + C++17, bez Arduino/ESPHome, bez wyjątków/RTTI, bez dynamicznych alokacji w miejscach real-time.
5. Nie resetować EEPROM/NVS, nie zmieniać układu persistent settings ani nie usuwać urządzenia i encji z HA bez dowodu, że jest to konieczne.
6. Nie poprawiać „przy okazji” błędu Zigbee2MQTT: dotyczy osobnej usługi na RPi5.

## G. Oczekiwany rezultat Codexa

Proszę oddać w jednym PR/diff (bez automatycznego merge/release/flash):

1. Krótkie **root-cause analysis** z dowodami (pliki/funkcje/linie, pomiary lub testy) i rozróżnieniem faktów/hipotez.
2. Listę zmienionych plików i opis nowej maszyny stanów synchronizacji MQTT.
3. Wyniki `./scripts/check.sh`, `pio run -e esp32doit-devkit-v1` oraz testów hostowych; albo dokładny powód, dlaczego nie mogły się uruchomić.
4. Opis sposobu zachowania 39 encji HA i testu poprawności Discovery.
5. Jasną listę testów sprzętowych, które użytkownik powinien wykonać na ESP32/RPi5, bez ryzyka wyłączenia lokalnego sterowania.
6. Aktualizację `PLANS.md`, `CODEX_HANDOFF.md` i właściwych dokumentów w `docs/` zgodnie z `AGENTS.md`.

**Kryterium sukcesu:** po normalnym CONNECT i przy obciążeniu HTTPS nie ma lawiny błędów Discovery/SUBSCRIBE ani niekontrolowanych reconnectów; panel HTTPS i wszystkie funkcje lokalne oraz OTA pozostają sprawne, a mechanizm Discovery zachowuje pełną zgodność z istniejącym HA.

---

### Materiały, z których sporządzono diagnozę

- ZIP projektu: `led-korytarz-v0.1-main.zip` (snapshot 2026-10-08, wersja 0.10.3), w tym `src/MqttService.*`, `src/WebDiagnosticsService.cpp`, `AGENTS.md`, `CODEX_HANDOFF.md`, `docs/ARCHITECTURE.md`, `docs/WEB_DIAGNOSTICS.md`, `sdkconfig.esp32doit-devkit-v1`.
- Log ESP32 0.10.3 — wklejony w rozmowie (brak osobnego pliku w tej paczce).
- `core_mosquitto_2026-10-10T14-06-36.452Z.log.txt` (8504 linie, zawiera także historyczne wpisy sierpień–wrzesień).
- `45df7312_zigbee2mqtt_2026-10-10T14-06-00.710Z.log.txt` (93 linie).
- `config_entry-mqtt-01M0X2WRG005AQCVB0MQ44PM92.json` (diagnostyka HA; nie dodawać pliku diagnostycznego do publicznego repozytorium bez kontroli danych wrażliwych).

**Uwaga:** Plik jest raportem do pracy lokalnej z Codexem; nie zawiera haseł ani kluczy. Oryginalne logi i JSON mogą zawierać identyfikatory infrastruktury i nie powinny być bezmyślnie publikowane w GitHub Issues/PR.
