# OTA i przyciski 0.10.8

Nie ustalono przyczyny nieudanej instalacji 0.10.7 bez logu z urzadzenia.
Brak przyczyny w aktywnosci HA nie oznacza braku kodu bledu w MQTT.
W HA nasluchuj `sterownik-led/#`, znajdz temat zakonczony `ota/status`.
Dotychczasowy firmware podaje `error`; nowy dodaje stage, free_heap,
largest_block, http_status i tls_error. HTTP=0/TLS=0 oznacza brak
zarejestrowanej informacji, nie potwierdza poprawnego transportu.
HTTP moze zawierac ostatnia odpowiedz przekierowania. RAM jest migawka,
nie szczytowym zuzyciem; po zwolnieniu TLS moze byc ponownie wysoki.
WWW pokazuje te pola w sekcji system. Nie zmieniono historii aktywnosci HA.

Nowy firmware nie naprawia wstecz kodu pobierania na starym urzadzeniu.
Jesli stare OTA stale zawodzi, instalacja USB jest niezalezna sciezka.
Nie obnizono walidacji TLS, obrazu ani rollbacku. Nie opublikowano wydania.

Podczas queued/downloading/verifying/rebooting obu drog OTA LCD swieci,
GPIO2 miga z polowa zwyklego interwalu. Alarm diody pozostaje zapamietany
i wraca po nieudanej aktualizacji. Samo sprawdzanie manifestu nie wymusza
podswietlenia. Po OTA wraca aktualna polityka przycisku/nocy.

Przyciski: zadanie nadal probkuje co 5 ms. Log KEY zawiera zdarzenie i maske;
odrzucenie z powodu pustej maski/bledu PWM lub zajetego kanalu ma opis.
Przerwa probkowania >=100 ms raportowana najwyzej co 10 s. Nie zmieniono
GPIO, debounce, gestow ani priorytetow. Nie uznano usterki za naprawiona.

HIL: zebrac MQTT OTA error oraz log KEY podczas braku reakcji; sprawdzic
przyciski przed i podczas OTA, LCD w nocy i stale OFF, diode przy alarmie,
powrot polityki po bledzie i poprawny restart po udanej instalacji.

Opcjonalny blueprint `HA_OTA_ERROR_BLUEPRINT.yaml`: skopiowac do
/config/blueprints/automation/sterownik_led/, przeladowac automatyzacje
i utworzyc automatyzacje z dokladnym tematem MQTT. Tworzy powiadomienie
w panelu HA, takze dla starego firmware (sam error). Nie jest wdrozony
w HA przez te zmiane; nie zmienia pola przyczyny historii aktywnosci.

Walidacja lokalna: HOST_TEST_COMPILER=bcc64 ./scripts/check.sh PASS
(build ESP32, testy hostowe i adapterow, testy publikatora).
check_utf8.py i git diff --check PASS; skladnia blueprintu YAML PASS.
Nie wykonano testu na ESP32 ani uruchomienia automatyzacji w HA.
