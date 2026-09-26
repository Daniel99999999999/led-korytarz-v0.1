# Otwarte decyzje

Plik odtworzono, ponieważ nie występował w dostarczonej kopii repozytorium.

Uzgodnione w bieżącym zadaniu: konfigurowalne przypisania KEY, dwa niezależne czasy PIR,
czas od ustania aktywności oraz zapis tych ustawień do NVRAM DS1307 zamiast EEPROM-u.
Szczegółowe zachowanie opisano w LOCAL_CONTROL.md.

Poza zakresem, nie implementować bez dalszych ustaleń:

- RTC DS1307 jako źródło daty/czasu, synchronizacja z NTP i harmonogramy;
- trwałe zapamiętywanie jasności lub ON/OFF, osobne profile jasności domyślnej/dzień/noc;
- rozpoznawanie uszkodzenia przewodu PIR niezależnie od ruchu i limit ciągłego HIGH;
- źródło CA dla MQTT TLS;
- progi ochronne INA228, obsługa ALERT i trwałe sumowanie energii.

Wymaga potwierdzenia sprzętowego: montaż DS1307, bateria i retencja NVRAM, schemat
połączenia do istniejącej translacji I2C. Brak układu jest obsługiwany jako praca w RAM.

Uzgodniono noc z HA według słońca, czasowy priorytet przycisku LCD także przy Stale OFF
oraz kontrolę DDRAM. Domyślny czas przycisku 120 s jest konfigurowalny w HA.
Szczegóły w LCD_CONTROL.md; harmonogram lokalny/RTC nadal poza zakresem.
