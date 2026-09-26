# Logi MQTT — firmware v0.8.1

## Uruchomienie w HA

1. Wgrać v0.8.1 standardowym Upload przez USB, bez Erase Flash.
2. Poczekać, aż sterownik połączy się z brokerem i odświeży MQTT Discovery.
3. W HA otworzyć urządzenie Sterownik LED i włączyć **Debug - logi MQTT**.
4. Otworzyć **Ustawienia → Urządzenia i usługi → MQTT / Mosquitto broker → Konfiguruj**.
   W sekcji **Nasłuchuj temat / Listen to a topic** wpisać:

   ```text
   sterownik-led/+/debug/log
   ```

5. Wybrać **Rozpocznij nasłuchiwanie / Start listening**. W tej zakładce pojawiają się wpisy.
   Najlepiej rozpocząć nasłuch przed włączeniem debug w drugiej karcie przeglądarki.
   Przy zmienionym topic_prefix zastąpić `sterownik-led` własnym prefiksem.
   Znak `+` obejmuje identyfikator dowolnego sterownika; można podać dokładny device_id.
6. Odtworzyć problem, skopiować wpisy i wyłączyć Debug. Samo zamknięcie okna HA nie wyłącza sesji.

Jest to istniejące okno integracji MQTT w HA, bez dodatkowych kart lub dodatków.
Nie instalowano nic w HA użytkownika. Oficjalna instrukcja:
[Testing your setup](https://www.home-assistant.io/integrations/mqtt/#testing-your-setup).

## Działanie

- ON uruchamia sesję na 600 s. Kolejne polecenie ON odnawia pełne 600 s.
- OFF zatrzymuje zbieranie nowych wpisów i opróżnia lokalny bufor.
- Restart zawsze wyłącza debug. Retained commands i niepełne payloady są odrzucane.
- Bufor w RAM: 32 wpisy po maksymalnie 255 bajtów tekstu. Przy przepełnieniu usuwa najstarszy.
- Maksymalnie 5 publikacji logów/s; QoS 0, bez retained, bez gwarancji dostarczenia.
- Przy utracie MQTT aktywna sesja nadal zbiera do RAM najnowsze wpisy; po powrocie
  połączenia wysyła pozostały bufor, jeżeli sesja jeszcze trwa. Timeout usuwa bufor także offline.
- Gdy outbox przekracza 8 KiB, wysyłka logów czeka; zwykłe stany mają zachowany zapas outboxu.
- Wpisy już przekazane do klienta MQTT przed OFF mogą jeszcze dotrzeć.
- UART nadal działa. Nie podniesiono poziomu logowania do DEBUG/VERBOSE.
- Brak zapisu do DS1307, EEPROM, NVS, flash lub RTC. DS1307 nie jest wymagany.

## Encje diagnostyczne dostępne także przy Debug OFF

| Encja | Znaczenie |
| --- | --- |
| Przyczyna restartu | Kod przyczyny zwrócony przez ESP-IDF |
| Czas pracy | Sekundy od uruchomienia, aktualizacja co 10 s |
| Pominiete logi | Przepełnienia, skrócone wpisy, kolizje try-lock lub odrzucone enqueue w sesji |

Stan jest wysyłany również po reconnect/birth HA oraz zmianie aktywności debug.
Licznik pominięć resetuje się przy rozpoczęciu nowej sesji, nie przy odnowieniu aktywnej.
Nie liczy strat sieciowych QoS 0, których nadajnik nie może potwierdzić.
Typowe powody restartu: `power_on`, `software`, `panic`, `task_watchdog`,
`interrupt_watchdog`, `watchdog`, `brownout`, `external`, `deep_sleep`, `sdio`, `other`.
Sam `panic` nie wskazuje konkretnego miejsca błędu. Retained stan przy urządzeniu offline
jest ostatnim odebranym stanem, a nie dowodem bieżącej pracy.

## Tematy

Względem `<topic-prefix>/<device-id>`:

- `debug/set`: tekst ON/OFF, bez retained; steruje sesją.
- `debug/status`: retained QoS 1, JSON z `debug`, `reset_reason`, `uptime_s`, `dropped`, `version`.
- `debug/log`: tekst logu, QoS 0, retain false.

## Ograniczenia i implementacja

Kopiowane są wpisy przechodzące przez `esp_log_set_vprintf`, przy bieżących poziomach logowania.
Nie obejmuje to zwykłego printf/konsoli, bootloadera, ROM, ESP_EARLY_LOG/ESP_DRAM_LOG ani
pełnego zrzutu panic. UART może więc zawierać informacje nieobecne w MQTT.
Przechwytywanie omija ISR i zadanie eksportera MQTT (ochrona przed pętlą własnych logów).
Formatowanie odbywa się do stałego bufora; try-lock nie czeka na inne zadanie i nie używa
transakcji sieciowych. Krytyczna sekcja modelu zawiera tylko kopiowanie krótkiego wpisu.
Publikację wykonuje istniejące zadanie MQTT; żadne dodatkowe zadanie nie steruje LEDC.
Znaki ANSI/kontrolne są usuwane; znaki spoza ASCII zastępuje `?`.

Debug musi być włączony przed problemem. Przy zawieszeniu procesora lub braku MQTT ostatnie
wpisy mogą nie dotrzeć. Bufor przepada przy resecie. Po restarcie dostępna będzie przyczyna
resetu i uptime po odzyskaniu MQTT, ale nie poprzedni bufor. Ten etap nie naprawia jeszcze
przyczyny obserwowanego watchdog. Kopiowane logi mogą zawierać adresy i nazwy sieci jak UART.

API przechwytywania: [ESP-IDF Logging](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32/api-reference/system/log.html).

## Testy HIL — do wykonania

- Discovery czterech encji i włączenie/wyłączenie debug z HA.
- Odbiór logów równolegle do UART, działanie przycisków i PIR podczas zalewu błędów I2C.
- Timeout 10 minut także bez MQTT; restart z domyślnym OFF.
- Ponowne połączenie bez restartu w aktywnej sesji, kontrola licznika pominięć.
- Przyczyna resetu, uptime i ewentualny watchdog podczas zasilania EXT.
