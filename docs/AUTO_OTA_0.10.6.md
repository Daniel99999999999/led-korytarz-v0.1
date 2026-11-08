# Publikacja i automatyczne OTA 0.10.6

## Codzienna procedura w VS Code

1. Zmien kod i numer PROJECT_VER w CMakeLists.txt. Opcjonalnie
   `python scripts/publish_ota.py --prepare-version --yes` proponuje nastepny numer
   na podstawie stabilnych wydan GitHub (wymaga Internetu).
2. Wykonaj kontrole projektu. Commit i Push przez Source Control; galez main
   oraz origin/main musza wskazywac ten sam commit, katalog musi byc czysty.
3. PlatformIO -> Project Tasks -> esp32doit-devkit-v1-ota -> Upload.
   Upload kompiluje, tworzy i weryfikuje publiczny Release oraz wypycha tag zrodel.
4. Jesli broker z secrets.ota.json jest osiagalny: polecenie MQTT bez retain,
   oczekiwanie na nowa wersje i idle po restarcie. HA nie musi byc uruchomiony.
5. Jesli nie ma pliku sekretow lub nie mozna polaczyc sie TCP z brokerem:
   publikacja pozostaje udana, komunikat wyraznie mowi o ODROCZONEJ instalacji.
   Blad TLS/logowania albo brak potwierdzenia sterownika pozostaje bledem Upload;
   Release moze juz istniec, sprawdz log. To nie jest potwierdzenie instalacji.

Build nie wykonuje publikacji. Upload profilu podstawowego nadal korzysta z USB.
Nie nadpisujemy istniejacego wydania; rozny commit lub hash wymaga nowego numeru.
Ponowne zlecenie istniejacego wydania: `python scripts/publish_ota.py --install-only`.
`--publish-only` pomija komende MQTT, ale NIE wstrzymuje autonomicznej instalacji
przez urzadzenia, ktore pobiora opublikowany Release.

## Co robi sterownik

- Funkcja jest domyslnie aktywna w 0.10.6, bez nowej konfiguracji NVRAM/NVS.
- Pierwsza proba 120-300 s od startu zadania OTA, po gotowosci Wi-Fi i czasu TLS.
- Zwykly odstep 6 h + losowe 0-15 min.
- Bledy: 15, 30, 60, 120, 240, maksymalnie 360 min + losowe 0-15 min.
- Brak Wi-Fi/czasu nie generuje prob HTTPS; po ich uzyskaniu zalegla proba rusza
  przy kolejnym obiegu zadania (do ok. 10 s).
- Pobierany jest maly manifest najnowszego stabilnego Release:
  https://github.com/Daniel99999999999/led-korytarz-firmware/releases/latest/download/release.json
- Wymagane: schema=1, projekt led_controller, profil OTA, firmware.bin,
  wersja MAJOR.MINOR.PATCH zgodna z tagiem, poprawny rozmiar i SHA-256.
- Instalowane sa tylko nowsze wersje, porownywane liczbowo; bez downgrade i
  ponownej instalacji tej samej wersji. Draft i prerelease nie sa kanalem auto.
- Obraz pobierany jest spod URL konkretnego tagu, nie zmiennego latest.
- Weryfikowane sa projekt/wersja deskryptora, pelny rozmiar oraz SHA-256
  odczytanej nieaktywnej partycji przed zatwierdzeniem OTA. Certyfikaty HTTPS
  sa sprawdzane przez bundle ESP-IDF. SHA z manifestu nie jest osobnym podpisem.
- Zachowano rollback ESP-IDF; wersja ostatniego obrazu oznaczonego invalid jest
  pomijana, aby nie instalowac ponownie znanej nieudanej wersji.
- Zadanie OTA i blokada sa wspolne dla MQTT, auto-OTA oraz transferu lokalnego.
  Nie powstaje dodatkowe zadanie RTOS. Pobieranie odbywa sie poza kontrolerem LED.
- Instalacja restartuje ESP32. Stany wyjsc i jasnosci pozostaja nietrwale;
  po restarcie obowiazuje istniejacy bezpieczny start LOW/duty 0.

Wybrano 6 h jako kompromis dla sterownika domowego, nie wymaganie standardu.
Jitter rozklada proby, backoff ogranicza obciazenie przy awarii. Instalacja jest
bez potwierdzenia uzytkownika i nie ma osobnego okna nocnego.

## Widocznosc i ograniczenia

WWW pokazuje liczbe prob, wynik, sprawdzanie w toku i termin nastepnej proby;
logi pokazuja pobieranie manifestu, wynik oraz instalowana wersje.
Nie zapisujemy historii prob do EEPROM, DS1307 ani NVS.
Automatyka wymaga dostepu ESP32 do Internetu, DNS i poprawnego czasu; nie wymaga
RPi, HA, brokera ani dostepu laptopa do domowej sieci.

Firmware 0.10.4/0.10.5 nie uzyska tej funkcji przez samo opublikowanie Release.
Pierwsze wgranie 0.10.6 wymaga istniejacej sciezki MQTT/OTA, USB albo lokalnego AP.
Stary mechanizm zatwierdzania obrazu po 10 s pozostaje; nie jest dlugotrwalym
sprzetowym autotestem wszystkich podzespolow.

## Walidacja sprzetowa (do wykonania)

- Pierwsza aktualizacja do 0.10.6 i pomiar marginesu stosu ota_service podczas
  sprawdzania manifestu, pobierania, hashowania oraz rownoleglego korzystania z WWW.
- Nowy stabilny Release bez brokera: instalacja przez Internet i restart.
- Ta sama/starsza wersja: brak flashowania; prerelease: brak instalacji.
- Brak DNS/Internetu, bledny manifest/hash, przerwany download: dziala lokalne
  sterowanie, brak przelaczenia partycji, backoff i log bledu.
- Rownoczesne zlecenie MQTT/AP: jeden transfer; rollback nieudanego obrazu bez
  ponownej automatycznej instalacji tego samego numeru.

Zrodla: [GitHub - latest release assets](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases),
[Espressif - HTTPS OTA](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_https_ota.html).
