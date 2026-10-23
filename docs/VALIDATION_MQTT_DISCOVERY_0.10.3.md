# Robocza poprawka MQTT/Discovery po logach 0.10.3

Kod pozostaje w wersji `PROJECT_VER=0.10.3`; to nie jest nowe wydanie.
Nie wykonano flashowania, publikacji ani OTA.

## Walidacja RAM i pelnego JSON (kolejny etap, 2026-10-12)

Test hostowy uruchamia **ten sam** `mqtt_discovery::build()` co firmware, z
wlaczonymi INA228 i DS18B20. Parsuje wynik cJSON, sprawdza kompletna liste 39
identyfikatorow komponentow, unikalnosc `unique_id`, pola `state_topic`, tematy
pieciu kanalow, metadata urzadzenia i root availability. Testuje za maly bufor
oraz cztery wymuszone awarie alokacji bez pozostawionych alokacji cJSON.

| Profil hostowy | Rozmiar JSON | Bufor | Zapas bufora |
|---|---:|---:|---:|
| Domyslny `sterownik-led` | 16 211 B | 24 576 B | 8 365 B |
| Maksymalny prefix topic, 63 znaki | 20 261 B | 24 576 B | 4 315 B |

Na hoscie builder wykonal 1494 alokacje cJSON, ze szczytem **51 998 B
zadanych bajtow**. Probe cJSON: przed drzewem 0 B, po drzewie 51 998 B,
po serializacji 51 998 B, po zwolnieniu 0 B. Liczby nie obejmuja narzutu
alokatora, statycznego bufora 24 576 B ani kopii MQTT outbox. **Nie sa to
pomiary wolnego heap ESP32.** Nie wyprowadzac z nich gwarancji, ze budowa
przejdzie przy otwartym HTTPS.

Wykonano `py scripts/test_host_bcc.py`, `./scripts/check.sh`, osobne
`pio run -e esp32doit-devkit-v1` oraz `git diff --check`: PASS. Kontrola
skryptem objela 163 pliki UTF-8, 10 testow narzedziowych, testy hostowe i
build. Obraz ma 118 736 B statycznego RAM i 1 384 825 B flash. Osobny mock
JS wykonal 100 pobran / 1200 wpisow i potwierdzil limit 32 768 znakow widoku.
Nie uruchomiono ESP32, nie wysylano OTA i nie publikowano obrazu.

Firmware loguje teraz `Discovery RAM before_tree`, `after_tree`,
`after_serialization`, `after_free`: free, largest i historyczne minimum
wewnetrznego 8-bitowego heap. Przy wczesnej awarii pozniejsze etapy nie
pojawia sie, z wyjatkiem `after_free`. `minimum` jest low-watermark od startu,
nie pamiecia dostepna w chwili probki. Log `Discovery enqueue` mierzy dodatkowo
koszt kopii payloadu do outboxu. Callback nieudanej alokacji tylko zapamietuje
rozmiar, bez logowania w callbacku.

Cache jest uniewazniany przy `mqtt apply` (przed probami budowy nowych tematow)
oraz `mqtt clear`. Zmiana prefiksu w konsoli modyfikuje tylko konfiguracje
oczekujaca; do `apply` biegnie stary klient i stary JSON. Reconnect i birth HA
uzywaja cache, bo nie zmieniaja schematu; nowy obraz firmware buduje go od zera.
Nie ma jeszcze testu integracyjnego `apply` na ESP32.

SUBACK ma timeout 5 s i backoff 2-30 s. Discovery otrzymuje teraz timeout
PUBACK 35 s (po domyslnym 30 s wygasnieciu outboxu ESP-MQTT), backoff 5-60 s
i ponowienie tylko przy outboxie nieprzekraczajacym 8 KiB. Pozny PUBACK przed
ponowieniem jest akceptowany; po nowym PUBLISH stary identyfikator jest
ignorowany. Test hostowy obejmuje brak SUBACK, odmowe, brak PUBACK, opozniony
PUBACK, nieudane enqueue i reset stanu na ponowny CONNECT. Broker z jeszcze
otwarta stara sesja moze ja przejac po nowym CONNECT z tym samym Client ID;
model resetuje stan, lecz zachowanie TCP/brokera wymaga HIL.

HTTPS: w uzywanym ESP-IDF poprawna sciezka zamkniecia wywoluje
`esp_tls_server_session_delete()` i `free()` kontekstu sesji. Handlery
`/api/status` i `/api/logs` trzymaja cJSON w RAII, a odpowiedz w statycznym
buforze. Panel pobiera status i logi sekwencyjnie co 2 s (flaga `busy` nie
pozwala na nakladanie pollingu); tekst logow w przegladarce ma teraz limit
32 768 znakow. Liczniki sesji TLS i probka heap **po zamknieciu** sa logowane
najwyzej raz na 60 s. Wyjatkowa sciezka SDK: w `esp_https_server` alokacja
transport context po udanym handshake moze sie nie udac; w tej wersji SDK
ta galaz zwraca bez zwolnienia `esp_tls_t`. To ryzyko przy OOM, nie dowod
wycieku podczas normalnego pollingu. Sam audyt zrodel nie zastepuje testu HIL.

### Propozycja obnizenia pierwszego szczytu RAM

Zachowac jeden retained MQTT Device Discovery i wszystkie 39 encji. Budowac
`dev`, `o` i `cmps` w istniejacym statycznym buforze, ale tworzyc w cJSON
**pojedynczy komponent**, serializowac go bez alokacji do konca bufora i od
razu zwalniac. Helpery lokalnych/diagnostycznych komponentow musialyby
umozliwiac budowe po jednym elemencie. To utrzymuje poprawne escapowanie
cJSON i obcina dynamiczny szczyt z calego drzewa do najwiekszej encji.
Przed wdrozeniem: porownac semantycznie caly nowy JSON z obecnym w tescie
39 encji, przetestowac granice 24 576 B i alokacje, nastepnie zmierzyc
ESP32 z aktywnym HTTPS. Nie dzielic Discovery na niezgodne fragmenty MQTT.

### Test na ESP32/RPi5 (jeszcze niewykonany)

1. Po osobnej zgodzie na wgranie roboczego obrazu uruchomic UART `logs on` i
   `mqtt status`; oczekiwac 28/28 SUBACK, JSON 39 encji i PUBACK. W HA odczytac
   retained `homeassistant/device/sterownik_led_<MAC>/config` i policzyc `cmps`.
2. Zapisac cztery probki `Discovery RAM`, `Discovery enqueue`, `HTTPS RAM`
   oraz `HTTPS after close`. Powtorzyc po resecie bez przegladarki, z jedna
   karta, z kilkoma kartami, a potem po ich zamknieciu. Porownywac
   `free/largest` przy tej samej liczbie aktywnych sesji, nie sam historyczny
   `minimum`.
3. Na brokerze wymusic niedostepnosc, brak SUBACK/PUBACK i reconnect, rowniez
   zanim broker zamknie stara sesje. Sprawdzic brak lawiny SUBSCRIBE i duplikatow
   outboxu; zmiany stanow LED/PIR maja pozostac lokalnie responsywne.
4. Zmienic topic/discovery prefix i wykonac `mqtt apply`; sprawdzic nowy JSON
   i temat. Bez `apply` stary klient powinien pozostac aktywny. Nie usuwac
   automatycznie starych retained rekordow bez osobnej decyzji migracyjnej.
5. Pozostawic jedna karte z pollingiem na 30-60 min; obserwowac licznik
   otwarc/zamkniec TLS, `largest`, restarty, `errno=12` i `-0x008D`. Po
   zamknieciu kart aktywne sesje powinny spasc do zera i heap sie ustabilizowac.
   Osobno sprawdzic przyciski, PIR, LCD i OTA dopiero po zgodzie na OTA.

## Potwierdzone i niepotwierdzone

- Potwierdzone w bazowym `MqttService::taskLoop` i `synchronizeClient`:
  porazka `buildDiscoveryPayload` powodowala co 500 ms powtorne 28 SUBSCRIBE
  i ponowne `invalidateAll`. Poprawka rozdziela te stany i nie wykonuje
  ponownych subskrypcji przy samym bledzie JSON.
- Potwierdzone: `connected` oznacza `MQTT_EVENT_CONNECTED`, przed SUBACK i
  Discovery. Nowy status pokazuje etapy oddzielnie.
- Potwierdzone: 24 576 B statycznego bufora oraz dynamiczne drzewo cJSON.
  Sam fragment nie dowodzi wycieku ani konkretnej przyczyny bledow.
- Logi z diagnozy wskazuja silna presje RAM oraz `errno=12`/ENOMEM.
  Nie dowodza, czy pierwsza nieudana alokacja jest w cJSON, TLS czy MQTT.
  `-0x7780` nie jest automatycznie bledem pamieci; `-0x008D` w tej wersji
  MbedTLS oznacza brak pamieci przy inicjalizacji TLS.
- Mosquitto odrzucal logowanie w okresie 15:02-15:08, pozniej je przyjmowal.
  Nie ma dowodu, ze RPi5, HA albo Zigbee2MQTT powoduje ten blad firmware.

## Zmiana

Na CONNECT usluga publikuje `online`, potem subskrybuje jeden temat i czeka
na jego SUBACK. Nieudany SUBSCRIBE/SUBACK ma backoff 2-30 s. Po potwierdzeniu
28 tematow buduje i kolejkuje Discovery; niepowodzenie ma osobny backoff
5-60 s. Birth HA odnawia `online`, Discovery i retained migawke, ale nie
subskrybuje ponownie. Status OTA i migawka kanalu sa wysylane niezaleznie
od Discovery. Kolejny reconnect/birth uzywa bufora JSON po udanej budowie;
`mqtt apply` go uniewaznia. `discovery_confirmed` oznacza PUBACK brokera,
nie potwierdzenie rejestracji encji przez HA.

Nie zmieniono identyfikatorow, tematow ani formatu JSON Discovery. Pelny
wariant ma 39 encji: 20 lokalnych ustawien,
4 diagnostyczne, 5 LED, 2 PIR, 1 OTA, 5 INA228 i 2 DS18B20. Hostowe testy
obejmuja teraz pelny payload oraz fragmenty `local_control` i `debug`.

## Pomiary do zebrania na ESP32

1. Bez przegladarki: po CONNECT sprawdz `mqtt status`: 28/28, Discovery
   zakolejkowane i PUBACK, 39 encji HA. Zachowaj wiersze `Discovery build`,
   `Discovery enqueue`, `HTTPS RAM` i `MQTT ERROR`.
2. Powtorz z jedna karta HTTPS, potem z kilkoma kartami i wymuszonym
   reconnect MQTT. Porownaj free/largest/with_tree/min oraz outbox.
3. Sprawdz lokalne przyciski, PIR i PWM podczas rozlaczenia brokera oraz
   przy bledzie Discovery. Zmien stan kanalu, przywroc broker i potwierdz
   retained stan w HA bez lawiny SUBSCRIBE.
4. Uruchom HA birth i obserwuj: bez nowych SUBSCRIBE, ponowny Discovery,
   migawka i status OTA. Rozroznij `discovery_queued` od PUBACK.
5. Pozostaw uklad 30-60 min z pollingiem WWW. Monitoruj restarty, minimum
   stosu, `-0x008D`, `errno=12`, largest free block i poprawny stan swiatel.
6. Dopiero po oddzielnej zgodzie na OTA sprawdz transfer przy aktywnym HTTPS
   i rollback. Nie wykonuj go w ramach tej poprawki.

`minimum` z logu heap jest historycznym low-watermark, a
`failed_alloc_during_build` jest globalnym sygnalem w oknie budowy i moze
pochodzic z innego zadania. Bez tych pomiarow nie obiecywac usuniecia
pierwszego szczytu RAM. W razie `discovery_buffer` trzeba najpierw poznac
rzeczywisty rozmiar wymagany przez serializacje; w razie braku RAM wybrac
serializer o mniejszej alokacji i dopiero wtedy dodac test pelnego JSON.
