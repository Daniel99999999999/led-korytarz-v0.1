# Diagnostyka stosu 0.10.4

## Zakres

Wersja zrodel 0.10.4, bez publikacji Release i bez wgrywania urzadzenia.
Nie zmieniono rozmiarow stosow, GPIO, sterowania ani formatu EEPROM/NVRAM.
Dane diagnostyczne tylko RAM. Progi orientacyjne: warning <512 B, critical
<256 B. Nie sa gwarancja bezpieczenstwa i nie wykrywaja wszystkich przepelnien.

## Dane

Panel HTTPS /api/status zawiera stacks: nazwa zadania, budget, minimum,
used_peak, level, measured, sampled_at, minimum_at, finished. W panelu
widac kolor alarmu, minimum i budzet w bajtach, szczyt oraz wiek pomiaru.
minimum to minimum wolnego stosu zaobserwowane przez FreeRTOS, NIE aktualne
wolne miejsce. minimum_at to uptime pierwszego ZAOBSERWOWANIA minimum,
nie dokladny czas instrukcji powodujacej szczyt. used_peak jest estymacja.
Brak pomiaru ma stan unknown, nigdy OK. Wylaczone funkcje pozostaja bez pomiaru.

MQTT <base>/stack/status: retained QoS1, nowa encja Discovery
`Stos - diagnostyka`, stan unknown/ok/warning/critical. Atrybuty:
- task, minimum, budget, used_peak, sampled_at, minimum_at;
- uptime_s publikacji, measured_tasks, boot_id, version, reset_reason;
- heap_free, heap_minimum, heap_largest: wewnetrzny heap 8-bit w momencie
  publikacji, NIE jednoczesny pomiar z chwila historycznego minimum stosu.

Zbiorcza encja wskazuje najmniejszy zapas z monitorowanych zadan, rowniez
zakonczonych. Minimum pozostaje zatrzasniete do restartu ESP32. Brak
powiadomienia o powrocie do normy: dla historycznego minimum byloby mylace.
Nie oznacza to, ze wszystkie zadania systemowe zostaly sprawdzone.
W celu analizy konkretnego stosu trzeba uzyc tabeli WWW.

## Czestotliwosc i narzut

LED, LCD, mqtt_service, local_ota i ota_service probkuja siebie co 10 s.
OTA podczas oczekiwania na zadanie budzi sie co 10 s. IR mierzy na obiegu
odbioru, konsola po poleceniu, sys_event po callbacku Wi-Fi/IP, mqtt_client
po callbacku MQTT, HTTP/HTTPS po handlerze, OTA po operacji (takze blednej).
Dla zadan zdarzeniowych nie ma gwarancji nowego pomiaru co 10 s: bez zdarzen
wynik pozostaje historyczny, dlatego udostepniono wiek pomiaru.
main i web_start zapisuja wynik przed zakonczeniem. Nie przechowujemy
uchwytow zadan do pozniejszego odczytu, wiec nie ma dereferencji po usunieciu.
Przy ponownym utworzeniu MQTT/HTTP minimum roli jest zachowane do resetu ESP.

Monitor nie tworzy zadania, nie alokuje heap i nie wykonuje logowania w
kontekscie producenta pomiaru. Try-lock pomija kolizje bez oczekiwania.
Sam pomiar ma narzut stosu i czasu skanowania stosu; wyniki uwzgledniaja
instrumentacje. Nie mierzymy idle, Wi-Fi driver, TCP/IP, mDNS ani ISR.

Stan MQTT publikowany po reconnect/birth, pogorszenie poziomu przy nastepnej
obsludze MQTT, zmiana minimum/liczby zmierzonych zadan maksymalnie raz na
60 s. Nie wysylamy stalej ramki co minute, jezeli nic sie nie zmienilo.
Znacznik sampled_at w HA dotyczy ostatniego opublikowanego pomiaru.
Log alarmowy raz na wzrost poziomu na dany boot, niezaleznie od debug MQTT.
Awaria sieci nie zatrzymuje pomiarow ani lokalnego sterowania; powiadomienie
sieciowe wymaga przywrocenia polaczenia. Ostry overflow moze wystapic przed
pomiarem i wysylka. Monitoring nie zastapi testow HIL ani ochrony canary.

## Push tylko na telefon Daniela

Gotowy blueprint: `docs/home-assistant/stack_alert_daniel.yaml`.
Nie aktywowano go w HA i nie wyslano zadnej wiadomosci. Nie znamy jeszcze
identyfikatora telefonu. Konto `daniel` nie jest adresem notify.

1. Na telefonie uruchom oficjalna aplikacje Home Assistant Companion,
   polacz z HA jako daniel i zezwol na powiadomienia systemowe.
2. HA -> Narzedzia deweloperskie -> Akcje: wyszukaj `notify.mobile_app_`.
   Jesli brak, zamknij/uruchom aplikacje i uruchom ponownie HA zgodnie z
   dokumentacja Companion. Sprawdz integracje Aplikacja mobilna i nazwe telefonu.
3. Po wgraniu 0.10.4 i MQTT Discovery wybierz encje `Stos - diagnostyka`.
4. Utworz pomocnik Tekst (input_text), dlugosc maksymalna 255, bez wartosci
   initial (ma odtwarzac poprzedni stan po restarcie HA). Osobny pomocnik
   dla tego sterownika i tej automatyzacji.
5. Skopiuj blueprint do /config/blueprints/automation/led_controller/
   stack_alert_daniel.yaml, przeladuj blueprinty i utworz automatyzacje.
6. Wybierz encje stosu, TELEFON DANIELA i pomocnik. Blueprint nie wysyla do
   grupy ani do innych kont. Nie wybieraj telefonu drugiego uzytkownika.
7. Testuj najpierw akcje notify na wybranym telefonie, potem automatyzacje
   na pomocniczej/testowej encji z wymaganymi atrybutami. Nie wywoluj celowo
   przepelnienia produkcyjnego stosu.

Deduplikacja: boot_id + poziom + encja, zapis w helperze HA po zaakceptowaniu
akcji push. Ponowny reconnect i retained stan nie powinny tworzyc kopii.
Sprawdzenie co minute pozwala ponowic akcje po bledzie HA; brak gwarancji
fizycznego doreczenia przez telefon. Nagly restart HA pomiedzy push i zapisem
helpera moze dac duplikat. Restart ESP32 tworzy nowy boot_id i nowy alarm.

Zrodla:
https://companion.home-assistant.io/docs/notifications/notifications-basic/
https://companion.home-assistant.io/docs/troubleshooting/faqs/
https://www.home-assistant.io/docs/blueprint/selectors/

## Walidacja

`./scripts/check.sh` PASS (Git Bash, HOST_TEST_COMPILER=bcc64): build ESP32,
testy hostowe i adaptery (0 failures), 169 plikow UTF-8. Brak ostrzezen
kompilatora. Log: .pio/check-0104-final.log. Testy sprawdzaja progi 256/512,
minimum historyczne, odrzucenie blednego pomiaru, limit 10 s, pomijanie ISR,
pomiar zakonczonego zadania i agregacje; Discovery obejmuje 40 encji.
Discovery: 16532 B, maksymalny prefiks 20682 B / bufor 24576 B.
Statyczny RAM 120000 B (+1260 B wzgledem audytu); flash 1389117 B (+4204 B).
Nie dodano zadania ani nie powiekszono stosow; heap cJSON Discovery jest
osobnym kosztem, nie zawartym w statycznym RAM. Tabela WWW jest dopisywana
bez dodatkowego drzewa cJSON do istniejacego bufora 8192 B.
Blueprint sprawdzono parserem ruamel.yaml i potwierdzono jawny wybor odbiorcy;
nie jest to walidacja wykonania w Home Assistant. `git diff --check` PASS.

HIL nie wykonano. Wymagane na ESP32: WWW
pod obciazeniem TLS, skan Wi-Fi, EEPROM, MQTT reconnect, OTA sukces/przerwanie,
IR/przyciski/PIR rownoczesnie. W HA: wybor telefonu, pierwszy warning,
critical, brak duplikatow po reconnect/restart HA i nowy boot ESP32.
