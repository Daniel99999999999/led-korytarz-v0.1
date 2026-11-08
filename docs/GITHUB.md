# GitHub i VS Code

Kod: prywatne repozytorium Daniel99999999999/led-korytarz-v0.1.
Firmware: publiczne Daniel99999999999/led-korytarz-firmware.

Source Control: Commit zapisuje zmiany lokalnie, Push wysyla je na GitHub,
Pull pobiera zmiany z GitHub. Te operacje same nie wgrywaja firmware.
Wtyczka GitHub Pull Requests jest przydatna do PR; podstawowe operacje Git sa
wbudowane w VS Code. Publikator korzysta z logowania Git Credential Manager.

Od 0.10.6 Upload profilu `esp32doit-devkit-v1-ota` buduje, publikuje Release
oraz zleca OTA przez MQTT, jezeli broker jest osiagalny. Build nie publikuje.
Przed Upload potrzebne sa nowy numer PROJECT_VER, Commit i Push na main.
Sekrety MQTT pozostaja tylko w ignorowanym secrets.ota.json; do samej publikacji
nie sa wymagane. Nie umieszczaj tokenu GitHub na ESP32.

Instrukcja i interwaly automatyki: [AUTO_OTA_0.10.6.md](AUTO_OTA_0.10.6.md).
Lista skryptow: [scripts/README.md](../scripts/README.md).

Alternatywa: workflow `.github/workflows/publish-firmware.yml` uruchamiany tagiem
vMAJOR.MINOR.PATCH. Gdy Release juz istnieje, sprawdza go; do samodzielnej
publikacji potrzebuje sekretu RELEASE_REPO_TOKEN z prawem Contents: write
wylacznie do publicznego repozytorium firmware. Publikator lokalny nie wymaga
tego sekretu Actions. Obie sciezki publikuja firmware.bin, release.json i SHA256SUMS.
