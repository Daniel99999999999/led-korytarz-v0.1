# Diagnostyka I2C 0.10.5

Panel WWW /api/status zawiera hardware: migawke publikowana co sekunde przez
wlasciciela I2C. HTTP nie wykonuje operacji na magistrali. Log podsumowania co 30 s.
Brak migawki oznacza brak pomiaru, nie sprawny sprzet.

- bus_initialized: zainicjalizowany kontroler, nie potwierdzenie wszystkich ukladow.
- lcd_io: wynik ostatniej operacji; lcd_ready: inicjalizacja wyswietlacza.
- frames / checks / mismatches: ramki, kontrole DDRAM, niezgodnosci.
- ddram_checked: kontrola od ostatniego bledu; ddram_error pozostaje historyczny.
- nvram.io: ostatnia rzeczywista komunikacja DS1307, nie samo dodanie uchwytu.
- nvram.record_valid: poprawny rekord; runtime_matches: zgodnosc z RAM.
- nvram.restore_queued: zlecono odtworzenie, nie potwierdzenie zastosowania.
- nvram.save_pending / retry_pending: oczekujacy zapis / ponowienie po bledzie.
- nvram.saves: zapisy potwierdzone odczytem od startu; sequence: numer rekordu.
- nvram.ram_key1 / saved_key1: maska kanalow; 1 = kanal 1, 3 = kanaly 1+2.
- nvram.phase: 0 load, 1 restore, 2 idle, 3 invalidate, 4 write, 5 commit, 6 verify.
- *_s / sampled_at: sekundy od uruchomienia, nie czas kalendarzowy.
- error: kod esp_err_t, 0 = ESP_OK; attempted=0 oznacza brak proby.

Dane sa ostatnia obserwacja, nie cyklicznym skanem adresow. Bateria nie jest
mierzona. Nie zmieniono rejestrow zegara ani sposobu zapisu ustawien.

## Test na sprzecie (pozostaje do wykonania)

1. Uruchomic 0.10.5; zapisac status LCD i DS1307.
2. Ustawic KEY1 na kanaly 1+2, odczekac co najmniej 5 s.
3. Sprawdzic saved_key1=3, runtime_matches=1, save_pending=0 oraz saves.
4. Zresetowac ESP32 przy stalym zasilaniu. Zapisac status i logi po starcie.
5. Porownac record_valid, restore_queued i obie maski. Osobno sprawdzic brak
   DS1307, odlaczony LCD i zachowanie sterowania lokalnego.

Zgloszony objaw dotyczy resetu przy stalym zasilaniu. Nie ustalono jeszcze
przyczyny pustego LCD ani utraty przypisania. Diagnostyka nie jest ich naprawa.
Wydania nie opublikowano ani nie wgrano na urzadzenie.

## Walidacja lokalna

`HOST_TEST_COMPILER=bcc64 ./scripts/check.sh`: PASS (build ESP32, testy hostowe,
adaptery 0 bledow). Bez ostrzezen kompilatora. RAM 120328 B, flash 1395185 B.
`python scripts/check_utf8.py`: PASS; `git diff --check`: PASS.
Log: `.pio/check-i2c-final.log`. Testy sprzetowe oraz renderowanie w przegladarce
na urzadzeniu pozostaja do wykonania.
