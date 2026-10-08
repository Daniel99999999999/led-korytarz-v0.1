# Architektura

Dokument odtworzony dla bieżącego kodu; poprzedniej wersji brakowało w dostarczonej kopii.

`LedController` jest jedynym właścicielem LEDC i stanu kanałów. Próbkuje wejścia co 5 ms,
rozpoznaje gesty, wykonuje komendy i timery PIR, aktualizuje fade oraz karmi watchdog.
Przypisanie KEY jest mapą logiczną; aktualizacja każdego PWM jest niezależna od mapy przycisków.
Regulacja grupy zamraża maskę kanałów do zakończenia gestu.

MQTT i IR przekazują komendy przez statyczną kolejkę (10 elementów), bez dostępu do LEDC.
Pilotowy czerwony POWER przełącza tylko wybrany kanał 1–5; wybór WSZYSTKIE daje komunikat
bez zmiany wyjść. Przycisk RGB ON/OFF nadal może przełączyć cel WSZYSTKIE.
Nowe komendy zmieniają pojedyncze ustawienie KEY/PIR/LCD lub odtwarzają cały rekord NVRAM.
Odtwarzanie jest przyjmowane tylko przed pierwszą zmianą konfiguracji w RAM.
Sterowanie lokalne nie zależy od Wi-Fi, MQTT, odczytu pamięci ani OTA.

`LcdService` jest jedynym właścicielem I2C0. Otrzymuje kopię stanu oraz konfiguracji
przez jednoelementową kolejkę nadpisującą. Obsługuje LCD i opcjonalnie EEPROM, INA228,
DS1307 NVRAM oraz usługę temperatury. `Ds1307Nvram` wykonuje najwyżej jedną transakcję
na obieg usługi, z timeoutem. NACK pozostaje lokalny; timeout resetuje automat wspólnej
magistrali, zachowując uchwyty innych klientów.

NVRAM jest oddzielona od `SettingsService` i payloadu EEPROM-u. Dwa rekordy CRC/commit
przechowują wyłącznie pięć masek KEY, dwa czasy PIR, tryb LCD i czas przycisku. Stany świateł i jasność pozostają w RAM.
Nie dodano mutexów blokujących kontroler ani operacji I2C w jego zadaniu.

MQTT publikuje różnicowo 27 pól migawki (zależnie od włączonych funkcji), z pełną
synchronizacją po reconnect/birth HA. Konfiguracja ma dziesięć nowych pól trackera.
PWM podczas ON/OFF nie generuje dodatkowych wartości jasności w MQTT; LCD odróżnia
przejście ON/OFF od regulacji jasności.

Szczegóły priorytetów, NVRAM, tematów MQTT i testów: [LOCAL_CONTROL.md](LOCAL_CONTROL.md).

BacklightPolicy działa w kontrolerze; żądany stan podświetlenia przekazuje atomowo do LcdService.
LcdDisplay sprawdza jeden znak DDRAM co 125 ms i korzysta z istniejącego recovery po błędzie.
Bufor discovery ma 16 KiB, limit outboxu 32 KiB (limit, nie stała alokacja).
Szczegóły: [LCD_CONTROL.md](LCD_CONTROL.md).

## Debug MQTT v0.8.1

DebugLog instaluje kopię wyjścia ESP_LOG przy starcie app_main; domyślnie przechwytywanie
jest wyłączone. Nie czeka na zajęty bufor i nigdy nie wykonuje MQTT w kontekście producenta logu.
Model DebugBuffer ma stałą pojemność i limit sesji. Eksport i wygaszanie sesji obsługuje
MqttService, budzony najwyżej po 200 ms także offline. Telemetria diagnostyczna co 10 s
jest niezależna od migawki kanałów. Debug nie zmienia NVRAM ani istniejącego trackera pól.
Szczegóły i ograniczenia: [DEBUG_MQTT.md](DEBUG_MQTT.md).

## Start sieci v0.8.2

WifiService publikuje atomowy znacznik gotowości po GOT_IP, ustawieniu DNS i logu runtime.
Kasuje go przy disconnect/apply, zdarzeniu rozłączenia i LOST_IP. MqttService tworzy klienta
przy apply, a jego zadanie wywołuje start po gotowości sieci. client_mutex chroni uchwyt,
stan startu i retry przed clear/disconnect/apply. Stop klienta, który jeszcze nie wystartował,
pomija esp_mqtt_client_stop i niszczy uchwyt. Udany start nie jest powtarzany na GOT_IP;
ponowne połączenia obsługuje ESP-MQTT. Zanik sieci może nastąpić także po sprawdzeniu
znacznika; zwykła obsługa błędów transportu pozostaje aktywna.

## v0.9.0 — PWM4/5 i lokalne OTA

Model PwmConfig sprawdza ograniczenia i ogranicza duty także podczas fade. Komendy
10 encji discovery trafiają przez ControlCommand do LedController; inne wątki
nie dotykają LEDC. DS1307 zachowuje dwa sloty/CRC/commit, kompresując payload v2
w 115 bitach; v1 nadal czytelny. Tracker MQTT ma 37 pól, bufor discovery 24 KiB.
Wynik komendy PWM jest liczony po decyzji kontrolera, bez czekania w zadaniu MQTT.
Awaria LEDC zatrzaskuje kanał OFF i wyzwala próbę przejęcia jego pinu jako GPIO LOW.

LocalOtaService ma własne zadanie obsługi AP i osobne zadanie HTTP do transferu.
Przycisk LCD wysyła tylko atomowe żądanie. Snapshot ekranu jest pobierany bez czekania
na mutex; hasło nie przechodzi do migawki MQTT. OtaService udostępnia wspólną blokadę
z MQTT/HTTPS OTA. Obie ścieżki używają nieaktywnej partycji i dotychczasowej weryfikacji
startu/rollback. AP używa RAM i nie zmienia ustawień sieci w AT24C08N.
Strona przyjmuje surowy binarny POST, weryfikuje adres AP, token i deskryptor przed
zapisem; odrzuca obcy projekt i tę samą wersję, obsługuje przerwanie i limit czasu.

## Panel diagnostyczny HTTPS v0.10.0

`WebDiagnosticsService` uzywa portu 443 i mDNS `sterownik-led.local`. Nie przejmuje
portu 80 uzywanego przez lokalne OTA w hotspotcie serwisowym. Certyfikat i klucz
P-256 powstaja na ESP32 przy pierwszym starcie i sa zachowywane w NVS; UART pokazuje
odcisk SHA-256 certyfikatu. Panel czyta snapshot kolejki MQTT i osobny, ograniczony
bufor ESP_LOG. Przyciski, PWM i stan kontrolera nie sa modyfikowane przez WWW.

Pierwsze uwierzytelnienie `admin/admin` daje dostep tylko do zmiany hasla. Solony
weryfikator PBKDF2-SHA256 trafia do istniejacego rekordu AES-GCM w AT24C08N; nowy
login staje sie aktywny dopiero po potwierdzonym zapisie. Bez dostepnego EEPROM-u
panel odmawia dostepu, a lokalne sterowanie nie jest blokowane. Zwykle komendy
UART sa wstrzymane do zmiany hasla, gdy EEPROM jest gotowy. Szczegoly i testy:
[WEB_DIAGNOSTICS.md](WEB_DIAGNOSTICS.md).

## Konsola UART v0.10.1

`DebugLog` przepuszcza ESP_LOG na UART podczas startu. Po uruchomieniu REPL
wycisza tylko ten strumien; odpowiedzi `printf` konsoli, logi WWW i opcjonalny
bufor diagnostyki MQTT pozostaja niezalezne. Komendy `logs status`, `logs on`
i `logs off` pozwalaja czasowo przywrocic logi na UART. Ustawienie nie jest
zapisywane i po resecie wraca do trybu startowego.

## Start HTTPS v0.10.2

`app_main()` uruchamia lokalny kontroler i konsolę przed zadaniem `web_start`.
Zadanie ma osobny stos 16 KiB, tworzy/odczytuje tożsamość TLS, uruchamia
serwer HTTPS i kończy się; właściwy serwer ma własne zadanie. Błąd alokacji
lub inicjalizacji panelu nie zatrzymuje sterowania lokalnego. Minimalny wolny
stos zadania startowego jest logowany do diagnostyki. To ogranicza regresję
`0.10.1`, w której inicjalizacja MbedTLS na stosie `main` powodowała bootloop.
Wymagany jest HIL startu z pustym NVS certyfikatu i z certyfikatem zapisanym.
