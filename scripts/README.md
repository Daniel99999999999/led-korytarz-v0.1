# Skrypty projektu

Glowny punkt publikacji: `publish_ota.py`. Zastapil `publikator_z_build.py`.

| Plik | Rola |
| --- | --- |
| publish_ota.py | Przygotowanie wersji, build/publikacja GitHub, zlecenie OTA |
| ota_upload.py | Transport MQTT, weryfikacja pliku HTTPS; uzywany przez publikator |
| release_artifact.py | Pakowanie release.json / firmware.bin / SHA256SUMS, takze w CI |
| use_primary_sdkconfig.py | Profil OTA korzysta z bazowego sdkconfig PlatformIO |
| check.sh | Kontrola projektu, build i testy |
| check_utf8.py | Kontrola kodowania plikow |
| test_release_artifact.py | Testy publikacji/manifestu/MQTT bez sieci |
| test_host_bcc.py | Testy C++ na Windows, kompilator Embarcadero |

Zwykly Build nic nie publikuje. Upload profilu `esp32doit-devkit-v1-ota`
buduje i wywoluje `publish_ota.py --image ...`. Upload profilu podstawowego
pozostaje USB. Nie usuwac modulow importowanych przez publikator ani CI.

Przed nowym wydaniem mozna uruchomic:

```text
python scripts/publish_ota.py --prepare-version --yes
```

Nastepnie Commit, Push i Upload OTA. Przygotowanie wersji zmienia CMakeLists.txt;
nie robi commitu ani push za uzytkownika. Alternatywnie ustaw PROJECT_VER recznie.
Szczegoly: ../docs/AUTO_OTA_0.10.6.md oraz ../docs/GITHUB.md.
