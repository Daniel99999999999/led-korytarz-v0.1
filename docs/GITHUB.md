# GitHub i VS Code

Projekt przygotowano do prywatnego repozytorium `led-korytarz`.
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
Do ustalenia pozostaje oddzielne publiczne repozytorium wydań binarnych albo inny
serwer HTTPS. Sam Publish/Push nie uruchamia OTA ani nie publikuje firmware.bin.
