[English](../en/user-guide.md) · [Powrót do README](../../README.pl.md)

# Przewodnik użytkownika

Ten przewodnik opisuje codzienną obsługę zasobnika, skrótów i OSD. Szczegółowa
edycja JSON jest w [przewodniku konfiguracji](configuration.md), a skrypty i
D-Bus w [przewodniku sterowania zdalnego](remote-control.md).

## Pierwsze uruchomienie i wybór aplikacji

Kreator pierwszego uruchomienia pyta o język, urządzenie wejściowe i domyślną
aplikację audio. Lista zasobnika pokazuje najpierw aktywnie grające klienty,
a następnie bezczynne klienty PipeWire, które są podłączone, lecz wstrzymane.

Menu zasobnika pozwala:

- wybrać aktywną aplikację audio;
- odświeżyć listę aplikacji audio;
- zmienić przechwytywane urządzenie wejściowe;
- otworzyć Ustawienia;
- otworzyć dwujęzyczne okno About z wersją, linkami i pełnym tekstem licencji.

Wybrana aplikacja pozostaje zachowana podczas ponownego łączenia z PulseAudio
lub PipeWire. Jej chwilowe zniknięcie przy odświeżaniu nie nadpisuje wyboru.

## Codzienne sterowanie

- Głośniej/ciszej zmienia tylko wybraną aplikację o skonfigurowany krok.
- Wyciszenie działa tylko na tej aplikacji i pokazuje symbol wyciszenia na OSD.
- Hotkey profilu `show` pokazuje aktualną wartość bez jej zmiany.
- Powiązania kółka myszy mogą działać równolegle ze zwykłymi skrótami evdev.
- Zasobnik pozostaje dostępny podczas działania globalnego przechwytywania.

Handler przechwytuje także sąsiednie węzły wybranej klawiatury, np. osobny
interfejs Consumer Control, oraz każde inne urządzenie zgłaszające skonfigurowany
klawisz lub kierunek kółka. Dzięki temu pulpit nie obsługuje tego samego zdarzenia.

Skonfigurowane skróty są przechwytywane na poziomie evdev. Pulpit i Qt nie
widzą tych konkretnych zdarzeń podczas pracy aplikacji. Aby zastąpić aktywny
skrót, kliknij prawym przyciskiem jego pole w Ustawienia → Profile, wybierz
**Wyczyść**, zapisz, otwórz profil ponownie i przechwyć nowy klawisz lub kierunek
kółka.

## OSD

OSD jest bezramkową nakładką zawsze na wierzchu z nazwą aplikacji, paskiem i
procentem. Czas wyświetlania, krycie, kolory, ekran i położenie są
konfigurowalne. Przeciągnięcie dowolnej widocznej krawędzi lub narożnika zmienia
rozmiar i zapisuje go automatycznie.

Opcjonalne sterowanie położeniem może dodać:

- przyciski strzałek przyciągające OSD do krawędzi ekranu;
- przeciąganie z wnętrza OSD;
- skróty klawiaturowe aktywne tylko przy widocznym OSD.

Wiersz odtwarzania może pokazywać nazwę aplikacji/utworu, czas, pasek postępu i
okładkę. Kliknięcie lub przeciąganie paska wykonuje seek, gdy odtwarzacz zgłasza
`CanSeek` i znaną długość. Dla strumieni bez długości pojawia się `LIVE`.
`progress_interactive: false` zostawia wizualny pasek bez seekowania myszą.

Tryb etykiety może być `app`, `title_artist`, `artist_title`, `app_track`,
`player_track`, `player_track_art` lub `custom`. Długie nazwy aplikacji i
utworów przewijają się jako marquee, krótkie są statyczne. Tokeny i pola JSON
opisano w przewodniku konfiguracji.

Ustawienia obejmują także język interfejsu, krok głośności, czas i kolory OSD,
częstotliwość odświeżania postępu, priorytet odtwarzaczy, tryb OSD akcji
multimedialnych, opcjonalny endpoint MPRIS, profile i aliasy aplikacji. Zmiany
są atomowo zapisywane w `config.json`.

## Profile i przełączanie według focusu

Profile pozwalają sterować wieloma aplikacjami z jednej klawiatury. Każdy profil
ma:

- jedną lub więcej nazw aplikacji i opcjonalny regex bez rozróżniania wielkości liter;
- własne skróty głośniej, ciszej, mute, show i ducking;
- opcjonalne modyfikatory `Ctrl` i/lub `Shift`;
- opcjonalny sink PulseAudio;
- minimalny i maksymalny poziom głośności;
- flagę `auto_switch` określającą udział w przełączaniu według aktywnego okna.

Pierwszy profil jest domyślny. Zwykłe akcje zasobnika, D-Bus i `kv-ctl` używają
go, jeśli profil nie został wskazany. Po włączeniu globalnego przełączania
focus wybiera pasujący profil; `auto_switch` może wyłączyć pojedynczy profil.

## Ducking, sceny i routing

Włącz Focus audio ducking w profilu, aby obniżyć wszystkie inne znane aplikacje
audio do wybranego procentu. Drugie naciśnięcie przywraca zapisane poziomy.

Scena jest nazwanym presetem. Dla każdego celu może ustawiać:

- głośność od 0 do 100;
- stan mute;
- nazwę wyjściowego sinka PulseAudio.

Sceny można mieć z globalnym hotkeyem i stosować z Ustawień albo przez
`kv-ctl`. Kolejność rozwiązywania to profil > scena > media; przy wspólnym
hotkeyu dwóch scen wygrywa pierwsza na liście.

Profile i sceny używają stabilnych nazw sinków PulseAudio, a nie ich opisów.
Przykład ręcznego routingu znajduje się w przewodniku sterowania zdalnego.

## Nazwy aplikacji i aliasy

Wrappery PipeWire mogą wystawiać przyjaznego klienta i inny binarny stream.
Aplikacja normalizuje te tożsamości, aby zasobnik pokazywał przyjazną nazwę,
ale sterowanie trafiało do właściwego streamu. Ustawienia → Aliasy aplikacji
pozwalają wymusić mapowanie:

```json
{ "match": "chromium", "display": "YouTube Music", "target": "youtube-music" }
```

`audio_app_filters` dodaje lub usuwa wpisy list systemowych binarek i nazw
pomijanych; nie zastępuje tych list w całości.

## Odtwarzanie i skróty multimedialne

Aplikacja pobiera z MPRIS metadane, pozycję, możliwość seekowania i priorytet
odtwarzaczy. Gdy focus wskazuje aplikację audio, preferowany jest jej pasujący
odtwarzacz MPRIS; w pozostałych przypadkach używana jest kolejność
`tracked_players` i stan odtwarzania.

Skróty play/pause, next, previous i stop są domyślnie nieprzypisane. Wysyłają
akcję do wybranego odtwarzacza i są niezależne od profili głośności. Jeśli ten
sam skrót posiada aktywny profil, profil wygrywa. OSD multimediów może być
wyłączone, pokazywać tylko akcję albo pełny OSD głośności.

Opcjonalny fałszywy endpoint
`org.mpris.MediaPlayer2.keyboardvolumeapp` jest domyślnie wyłączony. Włącz go
w Ustawienia → Postęp odtwarzania tylko wtedy, gdy widget lub inny klient MPRIS
ma widzieć aplikację jako odtwarzacz.

## Ostrzeżenie o głośności PipeWire

**Wykryto ukrytą głośność PipeWire** oznacza, że surowy mnożnik PipeWire różni
się od `1.0`, choć wartości kanałów widoczne w mikserze pokazują inny poziom.
Wykrywanie przy starcie, odświeżeniu, reconnect, zmianie profilu i pojawieniu
się streamu jest tylko do odczytu. Następna jawna zmiana głośności składa gain
w kanałach i zeruje surowy mnożnik; migracja konfiguracji nie jest potrzebna.

Natywny PulseAudio nie ma osobnego mnożnika PipeWire i nadal używa libpulse oraz
stream-restore.

## Diagnozowanie

- Brak urządzeń wejściowych: sprawdź grupę `input` i rozpocznij nową sesję logowania.
- Znikająca aplikacja po restarcie demona audio: poczekaj na ponowne łączenie z backoffem; wybór pozostaje zachowany.
- Problemy z postępem MPRIS: uruchom aplikację z `KVA_DEBUG_PROGRESS=1`, aby logować metadane i decyzje OSD.
- Niemożność ponownego przechwycenia skrótu: najpierw wyczyść go zgodnie z instrukcją powyżej; grab evdev celowo ukrywa zdarzenie przed Qt.
