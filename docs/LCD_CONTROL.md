# LCD — v0.8.0

## Podświetlenie

| Ustawienie w HA | Zakres / domyślnie |
| --- | --- |
| LCD - tryb podswietlenia | Automatyczne (domyślnie), Stale OFF, Stale ON |
| LCD - priorytet przycisku | 1–3600 s, domyślnie 120 s |
| LCD - noc (HA) | ON = noc, OFF = dzień |

Automatycznie: w dzień ON, w nocy OFF. PIR2 budzi podświetlenie w nocy na czas
ustawiony dla PIR2/kanału 2, liczony od ostatniego HIGH. Czas 0 wyłącza budzenie PIR2.
Ręczna zmiana kanału LED 2 nie zmienia LCD; oba mają oddzielny stan automatyki.

Przycisk GPIO19 przełącza podświetlenie na ustawiony czas, potem wraca bieżący tryb.
Działa również przy Stale OFF: czasowo zapala ekran. Kolejne naciśnięcie przełącza stan
i liczy czas od nowa. Zmiana trybu w HA od razu anuluje wymuszenie przyciskiem.
Zmiana czasu dotyczy kolejnego naciśnięcia; zachód/wschód nie skraca aktywnego wymuszenia.

Tryb i czas przycisku zapisują się w NVRAM DS1307 razem z KEY/PIR. Stan nocy, ruch
oraz wymuszenie pozostają w RAM. Po restarcie, bez HA, przyjmowany jest dzień;
po otrzymaniu sygnału z HA sterownik zachowuje ostatni dzień/noc podczas utraty sieci.
Nie oblicza nocy samodzielnie i nie korzysta z zegara DS1307.
Przed odtworzeniem NVRAM podczas startu podświetlenie jest domyślnie ON.
Późniejsze odzyskiwanie LCD zachowuje żądany stan podświetlenia.

Tematy względem `<topic-prefix>/<device-id>`:

- `config/lcd_mode/set` / `config/lcd_mode/state`: Automatyczne, Stale OFF, Stale ON;
- `config/lcd_override/set` / `config/lcd_override/state`: sekundy 1–3600;
- `lcd/night/set` / `lcd/night/state`: ON lub OFF.

Komendy bez retained; potwierdzony stan retained QoS 1, jak dla konfiguracji KEY/PIR.

## Automatyzacja HA

1. Skopiować [lcd_sun.yaml](home-assistant/lcd_sun.yaml) do
   `config/blueprints/automation/sterownik_led/lcd_sun.yaml` w HA.
2. Przeładować blueprinty, utworzyć automatyzację z tego szablonu i podać temat bazowy
   `<topic-prefix>/<device-id>` bez końcowego `/`, zgodny z tematami `mqtt status`.
3. Włączyć integrację Sun, sprawdzić lokalizację domu i raz uruchomić akcje automatyzacji.

Automatyzacja wysyła stan sun.sun po wschodzie/zachodzie, starcie HA, przeładowaniu
automatyzacji i komunikacie online sterownika. QoS 1, retain false, bez cyklicznego ruchu.
Plik przygotowano do instalacji; nie zainstalowano go w HA użytkownika.
Schemat według [wyzwalaczy HA](https://www.home-assistant.io/docs/automation/trigger/)
i [integracji Sun](https://www.home-assistant.io/integrations/sun/).

## Kontrola zawartości LCD

Jeden znak DDRAM odczytywany jest co około 125 ms; pełne 80 pozycji co około 10 s,
gdy LCD jest gotowy. Pierwszy odczyt i odczyt po błędzie mają 10 s zwłoki.
Porównywana jest pamięć znaków z ostatnią poprawnie wysłaną ramką, także znak stopnia 0xDF.
Niezgodność uruchamia czyszczenie, podwójną inicjalizację i wysłanie dwóch pełnych ramek.
NACK pozostaje lokalny; timeout powoduje odzyskiwanie I2C bez usuwania uchwytów innych urządzeń.

Pełna ramka nadal odświeża się co sekundę, więc może naprawić znak przed odczytem.
Zgodna DDRAM nie wyklucza błędnego przesunięcia ekranu, kontrastu, zasilania ani uszkodzenia matrycy.
Odczyt wymaga RW LCD podłączonego do P1 PCF8574 i D4–D7 do P4–P7, z działającym
odczytem dwukierunkowym. Nie zmieniono GPIO ani polaryzacji; połączenia wymagają sprawdzenia.
Test transportu na komputerze nie zastępuje testu elektrycznego i fizycznego LCD.

## Testy sprzętowe — niewykonane

- Brak fałszywych alarmów przy odczycie wszystkich znaków, także 0xDF; odzyskanie po zakłóceniu LCD.
- Odłączanie LCD przy pracującym EEPROM/INA228/DS1307, bez zatrzymania lokalnego sterowania.
- Zachód/wschód, ponowne połączenie HA, PIR2 i czas 0.
- Stale OFF: przycisk daje czasowe ON, potem OFF mimo ruchu PIR2.
- Czasowe OFF w dzień i zmiana trybu HA podczas wymuszenia.
- Odtworzenie trybu i czasu z NVRAM po zaniku zasilania.
