# DS1307 i status LCD - 0.10.7

## Ustalenia

Raport z urzadzenia: DS1307 ESP_ERR_INVALID_RESPONSE (264), brak poprawnego
rekordu, saves=0, pending=1, KEY1 RAM=3 / NVRAM=0. LCD byl odlaczony, dlatego
ESP_ERR_NOT_FOUND (261) jest oczekiwany i nie dowodzi wspolnej awarii.
Nie doszlo do potwierdzonego zapisu przed restartem. To nie jest test retencji
baterii. Nie ustalono jeszcze przyczyny NACK ani nie wykonano naprawy sprzetowej.

Pole LCD nadal obslugiwalo status ogolny i watchdog. DS1307 nie byl przekazywany
do tego pola: ten sam brak wystepuje juz w historii 0.8.2 (2896940).
Nie jest to dowod regresji w 0.10.5. Nie wszystkie opcjonalne podzespoly maja
osobny alarm LCD: INA228/temperatury maja pola pomiarow, szczegoly w logach/WWW.

## Nowa diagnostyka

Przed zaladowaniem rekordow, po starcie i po ponowieniu po bledzie:

1. probe_0x68: sam adres ukladu, i2c_master_probe.
2. pointer_0x08: START, adres WRITE, wskaznik 0x08, STOP; bez bajtu danych.
3. read_byte_0x08: osobny odczyt jednego bajtu z aktualnego wskaznika.
4. read_records: dotychczasowy combined read 56 B od 0x08 z repeated START.

Zachowano najwyzej jedna transakcje na obieg LcdService i timeout 20 ms.
Blad powoduje ponowienie od probe po 30 s. Zapis produkcyjny pozostaje bez
zmian: invalidate, write_payload, commit, verify. Nie zapisujemy wzorca testowego,
nie zmieniamy rejestrow 0x00-0x07 i nie przenosimy ustawien do EEPROM/NVS.
Ustawienie wskaznika oraz osobny odczyt sa niedestrukcyjne. Inne urzadzenia na
tej samej magistrali nie zmieniaja wskaznika DS1307; I2C ma jednego wlasciciela.

Log na pierwszym bledzie lub zmianie etapu zawiera adres, stage i esp_err_t.
WWW pokazuje `nvram.operation` i `nvram.failed_operation`. Ostatni blad pozostaje
aktywny podczas wstepnych prob i znika dopiero po udanym odczycie rekordow
lub zweryfikowanym zapisie. Sam ACK adresu nie oznacza poprawnej NVRAM.
Probe nie identyfikuje modelu ukladu, jedynie odpowiedz pod adresem.
Blad pointer nie wskazuje bez analizatora, ktory bajt tej transakcji otrzymal NACK.

Rozszerzenie tej samej wersji 0.10.7 wprowadza rotacje wszystkich wykrytych
alarmow w czwartej linii, zamiast pojedynczego komunikatu o najwyzszym
priorytecie. DS1307 ma osobne komunikaty BRAK ACK, BLAD ODCZYTU,
BLAD ZAPISU, BRAK REKORDU i TYLKO RAM. Szczegoly: LCD_ALERTS_0.10.7.md.

## Test na urzadzeniu - pozostaje do wykonania

1. Wgrac 0.10.7, podlaczyc LCD; zachowac log od startu oraz sekcje WWW I2C.
2. Odczytac pierwszy nieudany stage. Brak probe ACK oznacza brak odpowiedzi
   pod 0x68, przed ustawieniem wskaznika i odczytem danych.
3. Jesli wszystkie wstepne etapy sa OK, lecz read_records zawodzi, sprawdzic
   przebieg combined read i powtarzany START analizatorem.
4. Po przywroceniu komunikacji zmienic KEY1 na 1+2 i czekac na saves>0,
   runtime_matches=1 oraz maski 3/3. Dopiero wtedy testowac zanik zasilania.
5. Sprawdzic komunikat LCD, jego skasowanie po odzyskaniu komunikacji oraz
   lokalne sterowanie przy odlaczonym DS1307. Bez recznego kasowania NVRAM.

Osobny problem heap przy rownoczesnym HTTPS/auto-OTA pozostaje poza tym etapem.
Nie uznano go za przyczyne wczesniejszych NACK DS1307.

## Walidacja lokalna 2026-11-23

- Bazowy `HOST_TEST_COMPILER=bcc64 ./scripts/check.sh`: PASS.
- Po zmianach ta sama komenda: PASS, build bez ostrzezen, testy hostowe i adaptery.
- Nowe testy: priorytet komunikatu DS1307, powrot do OK, utrzymanie bledu podczas
  udanych etapow wstepnych, skasowanie po udanym read_records/verify.
- `python scripts/check_utf8.py`: 176 plikow PASS; `git diff --check`: PASS.
- RAM statyczny 122520 B, flash 1401481 B; to nie pomiar wolnego heap/stosu na ESP32.
- Log `.pio/check-ds-0107.log`. Nie wykonano testu fizycznej magistrali, retencji,
  publikacji ani OTA. Wynik nie potwierdza usuniecia przyczyny NACK.
