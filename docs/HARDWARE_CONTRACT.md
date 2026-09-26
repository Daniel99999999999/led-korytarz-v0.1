# Kontrakt sprzętowy

Ten plik odtworzono z `BoardConfig.hpp`, README i handoffu; brakowało go w dostarczonej kopii.
Źródłem pełnej listy stałych sprzętowych jest `src/BoardConfig.hpp`.

- Klasyczny ESP32 DOIT DevKit V1, ESP-IDF, C++17, bez Arduino/ESPHome.
- Kanały PWM 1–5: GPIO25, 26, 27, 32, 33, aktywne HIGH, LEDC 12 bitów.
- KEY1–5: GPIO13, 14, 16, 17, 18, przyciski do GND, aktywne LOW.
- GPIO2 tylko LED diagnostyczna. GPIO0/5/12/15 nie są używane przez aplikację.
- PWM LOW przed konfiguracją LEDC, następnie duty 0 przed startem kontrolera.
  Bezpieczny stan przed `app_main()` wymaga zabezpieczenia sprzętowego toru mocy.
- PIR1/2: GPIO34/35, zewnętrzne pull-up 10 kOhm, wewnętrzne wyłączone.
  Styk NC: LOW normalnie, HIGH przy ruchu/otwarciu/przerwaniu przewodu.
- Wspólne I2C0: SDA GPIO21, SCL GPIO22, 100 kHz, timeout transakcji 20 ms.
- LCD PCF8574AT: 0x3F, EEPROM AT24C08N: 0x50–0x53, INA228: 0x40.
- Przycisk podświetlenia GPIO19 do GND, aktywny LOW; DS18B20 opcjonalnie GPIO4;
  odbiornik IR opcjonalnie GPIO36. Pozostałe wymagania elektryczne opisuje README.

## DS1307 NVRAM — v0.8.0

- Opcjonalny klient 0x68 tej samej magistrali; nie wymaga nowych GPIO.
- DS1307 wymaga VCC 4,5–5,5 V oraz prawidłowej baterii VBAT do podtrzymania pamięci.
  Użyć istniejącej translacji poziomów; nie podciągać linii ESP32 bezpośrednio do 5 V.
- Sterownik jest przeznaczony dla **DS1307**, nie dla DS3231 o tym samym adresie.
- Zarezerwowane całe 56 B RAM 0x08–0x3F na konfigurację KEY/PIR/LCD; nie modyfikować 0x00–0x07.
- Wyłączona opcja kompilacji nie tworzy klienta I2C. Brak odpowiedzi nie zatrzymuje sterowania.
- Sama obecność modułu nie potwierdza sprawności baterii; retencja wymaga HIL.

Szczegóły: [sterowanie lokalne](LOCAL_CONTROL.md),
[dokumentacja DS1307](https://www.analog.com/media/en/technical-documentation/data-sheets/DS1307.pdf).

Odczyt DDRAM wymaga RW LCD do P1 PCF8574, D4–D7 do P4–P7; nie wolno zwierać RW do GND.
Nie zmieniono GPIO, polaryzacji ani mapy PCF. Pętle walidacji BoardConfig używają indeksów
ze względu na zgodność constexpr z kompilatorem hostowym Embarcadero.
