# GitHub i VS Code

Repozytorium kodu: https://github.com/Daniel99999999999/led-korytarz-v0.1
Repozytorium publicznych wydań OTA: https://github.com/Daniel99999999999/led-korytarz-firmware
Lokalna gałąź: main. Rozszerzenie: GitHub Pull Requests (GitHub.vscode-pull-request-github).

## Pierwsza publikacja

W VS Code: Ctrl+Shift+P -> Publish to GitHub. Zaloguj się w przeglądarce,
wybierz swoje konto i Publish to GitHub private repository, nazwa led-korytarz.
Jeśli VS Code poprosi o autora commitów, podaj własną nazwę i adres e-mail
(można użyć adresu noreply z ustawień konta GitHub).
Nie dodawaj secrets.ota.json ani plików .pio. Nie udostępniaj tokenu w czacie.

## Dalsza praca

Kontrola źródła: przejrzyj zmiany, wpisz opis, Commit, następnie Sync Changes / Push.
Na drugim komputerze: Git: Clone, wybierz repozytorium, otwórz lokalny katalog
oraz zainstaluj rekomendowane rozszerzenia. Najpierw zbuduj profil bazowy PlatformIO.
Lokalny secrets.ota.json należy odtworzyć osobno.

W Git przechowujemy sdkconfig.esp32doit-devkit-v1: zawiera aktualny zestaw funkcji,
który różni się od fabrycznych sdkconfig.defaults. To nie jest kopia NVS ani EEPROM.

## OTA

Prywatne repozytorium źródeł nie zapewnia publicznego URL do firmware.
Obecny firmware nie uwierzytelnia pobrań z prywatnych GitHub Releases.
Utworzono osobne publiczne repozytorium `led-korytarz-firmware` (2026-09-27).
Nie zawiera jeszcze wydania binarnego. Sam Publish/Push kodu nie uruchamia OTA
ani nie publikuje firmware.bin.

Kolejność pierwszego wydania:
1. Zbudować nową wersję (inną niż wersja działająca na sterowniku).
2. W publicznym repozytorium utworzyć Release z tagiem wersji i załączyć
   `.pio/build/esp32doit-devkit-v1-ota/firmware.bin` z tego samego builda.
3. W lokalnym, ignorowanym secrets.ota.json ustawić firmware_url na adres:
   `https://github.com/Daniel99999999999/led-korytarz-firmware/releases/download/TAG/firmware.bin`
   zastępując TAG rzeczywistym tagiem opublikowanego wydania. publish_path: null.
4. Uzupełnić lokalne dane MQTT (nie przesyłać ich na GitHub).
5. W PlatformIO wybrać profil esp32doit-devkit-v1-ota i Upload.

Uploader sprawdza rozmiar i SHA-256 pliku z HTTPS względem lokalnego obrazu przed
wysłaniem komendy MQTT. Jeżeli Upload przebuduje obraz, należy opublikować dokładnie
nowy plik; nie omijać kontroli zgodności. Automatyczna publikacja GitHub Releases
nie jest jeszcze częścią uploader-a. Prywatnego repozytorium kodu nie zmieniano
na publiczne; nie opublikowano sekretów ani nie wykonano OTA.

