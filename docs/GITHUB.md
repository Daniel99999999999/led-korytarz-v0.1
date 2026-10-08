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
Osobne publiczne repozytorium `led-korytarz-firmware` zawiera wyłącznie wydania.
Zwykły commit i push źródeł nie publikuje obrazu. Publikację uruchamia dopiero
tag wersji `vMAJOR.MINOR.PATCH` w prywatnym repozytorium źródeł.

## Jednorazowe przygotowanie publikacji

1. Publiczne repozytorium wydań musi mieć gałąź domyślną (jeżeli jest puste,
   dodaj tam np. README). Nie kopiuj do niego źródeł ani sekretów.
2. Utwórz fine-grained personal access token ograniczony **wyłącznie** do
   `led-korytarz-firmware`, z uprawnieniem `Contents: Read and write` i krótką
   datą ważności. Token `GITHUB_TOKEN` z repozytorium źródeł nie ma dostępu do
   drugiego repozytorium.
3. W prywatnym repozytorium źródeł dodaj token jako Actions secret o nazwie
   `RELEASE_REPO_TOKEN`. Nie wklejaj tokenu do kodu, logów ani rozmowy.

## Wydanie obrazu

1. Ustaw `PROJECT_VER` w `CMakeLists.txt`, wykonaj testy i commit/push źródeł
   razem z `.github/workflows/publish-firmware.yml`.
2. Dopiero gdy wersja jest gotowa do testu sprzętowego, wypchnij tag, np.:

   ```powershell
   git tag -a v0.10.2 -m "Firmware 0.10.2"
   git push origin v0.10.2
   ```

3. GitHub Actions uruchomi `check.sh`, zbuduje pełny profil OTA z przypiętego
   `sdkconfig.esp32doit-devkit-v1`, sprawdzi wersję w obrazie i opublikuje w
   publicznym Release `firmware.bin`, `release.json` i `SHA256SUMS`.
   Istniejące wydanie nie jest nadpisywane. Sam Release **nie aktualizuje ESP32**.
4. Sprawdź wynik Actions i trzy pliki w Release przed rozpoczęciem OTA.

W `secrets.ota.json` ustaw `firmware_url` na
`https://github.com/Daniel99999999999/led-korytarz-firmware/releases/download/TAG/firmware.bin`,
zastępując `TAG` rzeczywistym tagiem. Dla obrazu budowanego przez GitHub Actions
ustaw `"artifact_source": "release"` oraz `"publish_path": null`; zachowaj lokalne
dane MQTT. Profil PlatformIO `esp32doit-devkit-v1-ota` -> Upload przebuduje lokalny
obraz, sprawdzi zgodność **wersji** z Release, pobierze manifest i publiczny obraz,
zweryfikuje jego rozmiar, SHA-256 i deskryptor ESP-IDF, a dopiero potem wyśle
komendę MQTT. Obrazy z dwóch komputerów mogą mieć różne SHA mimo tych samych źródeł.

Tryb `"artifact_source": "local"` (także domyślny przy braku pola) zachowuje
poprzednią, ścisłą kontrolę SHA-256 obrazu lokalnego względem obrazu HTTPS.
Nie stosuj trybu `release` dla niezweryfikowanego wydania. Test uruchomienia,
rollbacku i sterowania lokalnego na ESP32 jest odrębnym krokiem HIL.

Wersja `0.10.1` z logu użytkownika restartuje się przez przepełnienie stosu main.
Nie publikuj ani nie wgrywaj jej jako wydania. `0.10.2` wymaga kompilacji,
publikacji i testu sprzętowego; sam opis powyżej nie oznacza ich wykonania.

