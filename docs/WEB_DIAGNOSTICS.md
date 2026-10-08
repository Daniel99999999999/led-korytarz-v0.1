# Panel diagnostyczny HTTPS

Firmware od v0.10.2 udostepnia `https://sterownik-led.local/` i `https://<IPv4 ESP32>/`.
Adres IP odczytaj z `wifi status` lub z listy klientow routera. LCD nie jest wymagany.
Windows moze nie rozwiazywac nazwy `.local`; wtedy uzyj IP.

## Pierwsze logowanie

1. Otworz adres HTTPS i sprawdz odcisk SHA-256 certyfikatu z komunikatem na UART.
   Certyfikat jest samopodpisany, wiec przegladarka wyswietli ostrzezenie. Certyfikat
   ma nazwe `sterownik-led.local`; dostep przez IP moze dodatkowo zglosic niezgodna nazwe.
2. Zaloguj sie jako `admin`, haslo `admin`. Dostepne jest tylko ustawienie nowego hasla.
3. Wpisz dwukrotnie nowe haslo: 12-64 widoczne znaki ASCII. Haslo nie jest wyswietlane
   w logach i nie jest wysylane poza polaczeniem HTTPS. Po kilku sekundach odswiez
   strone i zaloguj sie nowym haslem. Jesli ponownie widac formularz pierwszego
   logowania, sprawdz `settings status`: zapis mogl sie nie powiesc.

Alternatywa po UART: `settings web-password` prosi dwukrotnie o haslo bez wyswietlania
go w konsoli. `settings status` pokazuje `Haslo WWW: ustawione` dopiero po zapisie.
Przy gotowym EEPROM-ie inne komendy konsoli sa wstrzymane, dopoki haslo nie zostanie
zmienione. Przy braku EEPROM-u konsola pozostaje dostepna, lecz panel nie wpuszcza.

Haslo jest zapisywane w AT24C08N jako losowa sol i weryfikator PBKDF2-HMAC-SHA256
(20 000 iteracji) wewnatrz istniejacego rekordu AES-256-GCM. EEPROM nie zawiera
hasla jawnego. Klucz szyfrowania rekordu, certyfikat i klucz TLS sa w NVS. Obecnie
Flash Encryption jest wylaczone, wiec odczyt fizycznej pamieci flash moze ujawnic
klucze; HTTPS chroni transmisje w LAN, nie fizyczny dostep do plytki.

## Zakres panelu

Panel pokazuje status Wi-Fi, MQTT, OTA, EEPROM, kanaly LED, PIR, INA228, DS18B20
oraz ostatnie komunikaty ESP_LOG. Jedynymi komendami sa skan Wi-Fi i skan 1-Wire.
Hasel Wi-Fi i MQTT nie da sie wpisac przez panel. Logi sa buforowane niezaleznie
od diagnostyki MQTT; pamiec jest ograniczona do 48 wpisow po 256 znakow.
Odczyt wymaga logowania; 5 blednych prob blokuje logowanie na 60 s. Sesja jest
ulatujaca i znika po restarcie, zmianie hasla lub 30 minutach bezczynnosci.
Nie przekierowuj portu 443 z
Internetu i nie traktuj domyslnego `admin/admin` jako hasla docelowego.

## Proby na urzadzeniu

- Sprawdz, ze aktualna wersja nie restartuje sie podczas tworzenia certyfikatu TLS;
  w logach WWW odczytaj minimalny wolny stos zadania `web_start` (16 KiB).
- Sprawdz, ze konsola UART jest gotowa takze wtedy, gdy HTTPS nie wystartuje.
- Sprawdz dostep przez IP oraz `.local`, odcisk certyfikatu i brak HTTP panelu na porcie 80.
- Sprawdz, ze `admin/admin` nie odczyta `/api/status` przed zmiana hasla.
- Ustaw haslo przez WWW lub UART; potwierdz `settings status`, restart i logowanie nowym haslem.
- Odlacz AT24C08N lub zasymuluj timeout: panel ma odmowic dostepu, przyciski nadal dzialac.
- Otworz lokalne OTA i sprawdz, ze port 80 AP dziala rownolegle z HTTPS na porcie 443.
- Sprawdz RAM po kilku polaczeniach TLS i dluzszej pracy z aktywnym MQTT.

Powyzsza lista to procedura HIL, nie wynik wykonanych testow sprzetowych.

## Logi i konsola UART

Logi startowe sa widoczne na UART przed uruchomieniem promptu. Po starcie
konsoli asynchroniczne `ESP_LOG` nie przerywaja wpisywania polecen; odpowiedzi
na polecenia nadal sa drukowane. `logs status` pokazuje tryb, `logs on` wlacza
strumien UART do diagnostyki, a `logs off` ponownie go wycisza. Bufory logow
WWW i diagnostyki MQTT dzialaja niezaleznie od tego przelacznika. Po resecie
ustawienie wraca do domyslnego trybu.

HIL: podczas wpisywania polecen wywolaj reconnect MQTT i sprawdz brak tekstu
logow miedzy znakami; uruchom `logs on`, potwierdz widoczne ESP_LOG, potem
`logs off` i potwierdz cisze UART oraz dalsze aktualizowanie logow WWW.

Wersja `0.10.1` na sprzecie restartowala sie z komunikatem `stack overflow in
task main` przed startem panelu. W `0.10.2` przygotowanie TLS i HTTPS dziala
w jednorazowym zadaniu z osobnym stosem, po uruchomieniu konsoli. Na ESP32
potwierdzono logowanie, zmiane hasla, Wi-Fi, MQTT i NTP bez widocznego restartu.
Nadal trzeba odczytac minimalny wolny stos zadania `web_start` i sprawdzic
dluzsza prace. `-0x7780` podczas TLS oznacza alarm od klienta, mozliwy przy
odrzuceniu samopodpisanego certyfikatu; pojedyncze 404 dla `/favicon.ico` jest
nieszkodliwym zadaniem przegladarki. `0.10.3` usuwa okresowe ostrzezenie
`1-wire.device` przy pustej magistrali; wymaga testu z czujnikiem i bez niego.
