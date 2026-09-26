# AGENTS.md

## Cel repozytorium

Firmware uniwersalnego pięciokanałowego sterownika LED dla klasycznego ESP32.
Projekt używa wyłącznie C++17, ESP-IDF i PlatformIO.

## Zasady bezwzględne

- Nie dodawaj Arduino, ESPHome ani zależności wymagających ich runtime.
- Nie zmieniaj przypisania GPIO ani polaryzacji sygnałów bez jawnej aktualizacji `src/BoardConfig.hpp`, `docs/HARDWARE_CONTRACT.md` i opisu zmiany.
- Nie używaj GPIO0, GPIO5, GPIO12 ani GPIO15.
- GPIO2 jest zarezerwowane wyłącznie dla wbudowanej LED diagnostycznej i nie może otrzymać innej funkcji.
- Wszystkie wyjścia PWM mają zostać ustawione LOW, a kanały LEDC na duty 0 przed uruchomieniem zadania sterującego.
- GPIO34 i GPIO35 korzystają z zewnętrznego pull-up 10 kOhm; nie próbuj włączać na nich wewnętrznych rezystorów podciągających.
- Awaria Wi-Fi, MQTT, NVS lub OTA nie może blokować lokalnego sterowania przyciskami.
- Nie umieszczaj haseł, kluczy ani danych sieciowych w repozytorium.
- Wyjątki C++ i RTTI pozostają wyłączone.
- Nie wykonuj operacji blokujących ani logowania w ISR.
- Nie używaj wielu wątków do bezpośredniej modyfikacji LEDC. Przyszłe komendy sieciowe mają trafiać do kolejki kontrolera.

## Pierwsze kroki przed zmianą kodu

1. Przeczytaj `CODEX_HANDOFF.md`.
2. Przeczytaj `docs/HARDWARE_CONTRACT.md`, `docs/ARCHITECTURE.md` i `docs/OPEN_DECISIONS.md`.
3. Uruchom `./scripts/check.sh`.
4. Najpierw napraw błędy kompilacji i ostrzeżenia bez rozszerzania funkcji.

## Budowanie

```bash
pio run -e esp32doit-devkit-v1
```

Pełna lokalna kontrola jakości:

```bash
./scripts/check.sh
```

Jeżeli dodasz środowisko testów hostowych `native`, skrypt ma je uruchamiać automatycznie.

## Styl kodu

- Identyfikatory i nazwy plików: angielski.
- Komentarze i logi mogą być po polsku; logi powinny pozostać czytelne w ASCII.
- Bez globalnych obiektów wymagających złożonej inicjalizacji przed `app_main()`.
- Preferuj RAII, stały ownership i obiekty o statycznym czasie życia.
- Publiczne funkcje sprzętowe zwracają `esp_err_t`; błędy są obsługiwane na granicy modułów.
- `ESP_ERROR_CHECK` stosuj wyłącznie tam, gdzie bezpieczna kontynuacja nie jest możliwa, głównie w `app_main()`.
- Unikaj alokacji z heap po zakończeniu inicjalizacji.
- Formatuj zmienione pliki zgodnie z `.clang-format`.

## Zakres zmian

- Wykonuj jeden logiczny etap naraz.
- Dla większej pracy utwórz lub zaktualizuj plan w `PLANS.md`.
- Nie implementuj punktów oznaczonych jako nierozstrzygnięte w `docs/OPEN_DECISIONS.md`; przygotuj interfejs lub pozostaw wyraźne TODO.
- Nie deklaruj testu sprzętowego jako wykonanego, jeżeli przeprowadzono tylko kompilację lub test hostowy.

## Kryteria zakończenia zadania

- Projekt buduje się bez nowych ostrzeżeń.
- Testy hostowe przechodzą, jeżeli istnieją.
- Zaktualizowano dokumentację oraz plan.
- W podsumowaniu podano: zmienione pliki, wykonane komendy, wynik testów i testy pozostające do wykonania na sprzęcie.

## Review guidelines

Podczas przeglądu priorytetem są:

1. bezpieczny stan wyjść podczas startu, resetu i błędu;
2. wyścigi między przyciskami, fade i przyszłymi komendami sieciowymi;
3. zużycie pamięci, stosu i NVS;
4. błędy polaryzacji, zakresu duty i konwersji czasu;
5. rozróżnienie alarmu PIR od stanu normalnego LOW oraz reakcja na przerwę przewodu;
6. zależność lokalnego sterowania od sieci;
7. przypadkowe ujawnienie sekretów.
