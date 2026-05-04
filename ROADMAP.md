# keyboard-volume-app — rekomendacje rozwoju

Projekt jest w pełni funkcjonalny (C++20/Qt6, 6 dni od startu), ale brakuje infrastruktury wokół kodu. Poniżej priorytety rozwoju.

---

## Priorytet 1 — Krytyczne / wysoki wpływ

### 1. Paczkowanie i dystrybucja (PKGBUILD + CPack) ✓

**Problem:** Użytkownik musi budować ze źródeł. Brak jakiegokolwiek paczkowania.
**Rekomendacja:** Stworzyć PKGBUILD dla Arch Linux, rozważyć .deb/.rpm przez CMake/CPack.
**Pliki:** Nowy `pkg/arch/PKGBUILD`, ewentualnie `cmake/cpack.cmake`
**Status:** Zrealizowane. `pkg/arch/PKGBUILD` — paczka `keyboard-volume-app-git` budująca z brancha `main` via `git clone`. `pkgver()` generowany przez `git describe`. `depends`: `qt6-base libevdev libpulse pipewire`. CMake Release build z `DESTDIR` install. Dodano `resources/keyboard-volume-app.desktop` (bez hardkodowanych ścieżek) oraz reguły `install()` dla `.desktop` → `share/applications/` i ikony → `share/pixmaps/`. Dodano plik `LICENSE` (GPL-2.0-or-later).

### 2. Testy jednostkowe i integracyjne ✓

**Problem:** Zero testów. Narzędzie systemowe bez testów to ryzyko regresji przy każdej zmianie.
**Rekomendacja:** Dodać Google Test lub Catch2, pokryć minimum:

- Config (merge, load/save)
- VolumeController (mock PA)
- InputHandler (mock evdev)
- i18n (lookup, fallback)
**Pliki:** `cpp/tests/`, zmiana w `cpp/CMakeLists.txt` (subdirectory)
**Status:** Zrealizowane. 5 plików testowych (test_config 23 testy, test_i18n 7 testów, test_pwutils 3 testy, test_volumecontroller 5 smoke testów, test_inputhandler 15 testów), zintegrowane z CTest, 100% pass.

### 3. Poszanowanie XDG_CONFIG_HOME ✓

**Problem:** Config ścieżka zahardkodowana jako `~/.config/keyboard-volume-app/`. Na NixOS i innych dystrybucjach nie zadziała.
**Rekomendacja:** Użyć `QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)`.
**Pliki:** `cpp/src/config.cpp` — zmiana ~1 linii
**Status:** Zrealizowane (już używa QStandardPaths).

---

## Priorytet 2 — Istotne ulepszenia

### 4. Zmniejszenie latencji klawiszy (200ms → 50ms) ✓

**Problem:** InputHandler polluje evdev z select() timeout 200ms. Odczuwalne opóźnienie.
**Rekomendacja:** Zmniejszyć timeout do 50ms lub użyć epoll/inotify na deskryptorze evdev.
**Pliki:** `cpp/src/inputhandler.cpp` — zmiana stałej timeoutu
**Status:** Zrealizowane. Zastąpiono `select()` wydajniejszym `epoll()` z limitem 50ms, co minimalizuje zużycie CPU i skraca okno sprawdzania warunku zatrzymania pętli.

### 5. Konfiguracja pierwszego uruchomienia (first-run wizard) ✓

**Problem:** Nowy użytkownik musi ręcznie wybrać urządzenie evdev, skonfigurować OSD itp.
**Rekomendacja:** Prosty dialog powitalny przy pierwszym uruchomieniu (gdy config.json nie istnieje).
**Pliki:** Nowy `cpp/src/firstrunwizard.h/cpp`, modyfikacja `cpp/src/main.cpp`
**Status:** Zrealizowane. `QWizard` z 2 stronami (język + urządzenie). `Config::isFirstRun()`. `App` — dwufazowa inicjalizacja.

### 5a. Wybór domyślnej aplikacji w first-run wizard ✓

**Problem:** Użytkownik musi po pierwszym uruchomieniu wejść do tray menu aby wybrać aplikację audio.
**Rekomendacja:** Dodać trzecią stronę w `FirstRunWizard` umożliwiającą wybór domyślnej aplikacji już przy pierwszym uruchomieniu.
**Pliki:** Nowe `cpp/src/pwutils.h/cpp`, modyfikacja `cpp/src/firstrunwizard.h/cpp`, `cpp/src/volumecontroller.cpp`, `cpp/src/i18n.cpp`, `cpp/CMakeLists.txt`, `cpp/tests/CMakeLists.txt`
**Status:** Zrealizowane. `AppPage` z listą klientów PipeWire (obecnie przez libpipewire). Zawiera opcję "Bez domyślnej aplikacji", przycisk Refresh, i zapisuje wybór do `Config::selectedApp`. Wydzielono `listPipeWireClients()` do wspólnego modułu `pwutils.h/cpp`. Lista wydzielona później do reusable `AppListWidget`.

### 5b. Zmiana domyślnej aplikacji z tray menu ✓

**Problem:** Użytkownik nie może zmienić domyślnej aplikacji inaczej niż przez radio listę w tray menu (tylko aktualnie wykryte aplikacje).
**Rekomendacja:** Dodać "Change default application..." do tray menu, otwierające dialog z listą PipeWire, odświeżaniem i opcją "No default application".
**Pliki:** Nowe `cpp/src/applistwidget.h/cpp`, `cpp/src/appselectordialog.h/cpp`, modyfikacja `cpp/src/trayapp.h/cpp`, `cpp/src/firstrunwizard.h/cpp`, `cpp/src/i18n.cpp`, `cpp/CMakeLists.txt`, `CLAUDE.md`
**Status:** Zrealizowane. Tray menu ma "Change default application..." — modal dialog (`AppSelectorDialog`) z listą PipeWire, przyciskiem Refresh, opcją "Bez domyślnej aplikacji". Reusable `AppListWidget` współdzielony między `AppPage` (first-run wizard) i `AppSelectorDialog`. Bezpośrednie wybieranie aplikacji z tray listy (radio items) działa bez zmian. 4/4 testów przechodzi.

### 5c. Automatyczne odświeżanie listy aplikacji audio ✓

**Problem:** Użytkownik musi ręcznie klikać "Refresh" w tray menu po uruchomieniu/zamknięciu aplikacji audio.
**Rekomendacja:** Rozszerzyć PaWatcherThread o obsługę eventów REMOVE, dodać debounced auto-refresh (500ms) w PaWorker.
**Pliki:** `cpp/src/volumecontroller.cpp`
**Status:** Zrealizowane. PaWatcherThread emituje na NEW i REMOVE. PaWorker debounce timer (500ms) odświeża listę automatycznie. Istniejące połączenie appsReady → rebuildMenu obsługuje update tray menu. Brak zmian w API publicznym.

### 5d. Automatyczne ponowne połączenie PulseAudio ✓

**Problem:** Po restarcie `pipewire-pulse` / PulseAudio `PaWorker` mógł utracić kontekst PA i wymagać restartu aplikacji w scenariuszach, których nie obsłużył fallback PipeWire.
**Rekomendacja:** Wykrywać `PA_CONTEXT_FAILED` / `PA_CONTEXT_TERMINATED`, czyścić kontekst PA i reconnectować z backoffem. Pending volume/mute trzymać w `PaWorker`, żeby nie ginęły przy restarcie watchera.
**Pliki:** `cpp/src/volumecontroller.cpp`, `cpp/src/trayapp.cpp`, `cpp/tests/test_volumecontroller.cpp`
**Status:** Zrealizowane. `PaWorker` reconnectuje kontekst PA z backoffem 500ms → 30s, `PaWatcherThread` odbudowuje subskrypcję sink-input, a hot path sprawdza `contextReady()` przed użyciem libpulse. Transient refresh listy aplikacji nie nadpisuje już `selected_app`. `test_volumecontroller` ma dodatkowy smoke test dla niedostępnego PulseAudio.

### 6. Osadzenie ikony jako Qt resource ✓

**Problem:** Ikona ładowana z pliku obok binarki. Jeśli plik zniknie — brak ikony w tray.
**Rekomendacja:** Dodać plik `.qrc`, osadzić icon.png jako zasób Qt.
**Pliki:** Nowy `cpp/resources.qrc`, zmiany w `trayapp.cpp` i `osdwindow.cpp`
**Status:** Zrealizowane. `resources.qrc` w SOURCES, ikona `:/icon.png` przez QRC, usunięto POST_BUILD kopiowanie.

### 7. Wsparcie dla PipeWire przez API libpipewire

**Problem:** `pw-dump` i `pw-cli` subprocesy są wolne (~30ms) i wymagają obecności tych binarek.
**Rekomendacja:** Rozważyć libpipewire bezpośrednio zamiast subprocesów. Alternatywnie: PulseAudio API działa przez pipewire-pulse.
**Pliki:** `cpp/src/pwutils.cpp`, `cpp/src/volumecontroller.cpp`, `cpp/CMakeLists.txt`
**Status:** Zrealizowane. `pwutils` używa libpipewire do snapshotu registry, listowania klientów PipeWire oraz odczytu/zapisu `SPA_PARAM_Props` na node'ach streamów. `VolumeController` zachowuje libpulse jako szybki primary backend, ale fallback dla idle/paused node'ów nie uruchamia już `pw-dump` ani `pw-cli`. Dodano zależność CMake `libpipewire-0.3` i testy filtrowania/deduplikacji klientów.

---

## Priorytet 3 — Dobre mieć

### 8. Obsługa wielu aplikacji jednocześnie ✓

**Problem:** Tylko jedna aplikacja audio może być kontrolowana naraz.
**Rekomendacja:** Profile/keybinds — np. Ctrl+Vol dla przeglądarki, sam Vol dla Spotify.
**Pliki:** `cpp/src/config.{h,cpp}`, `cpp/src/inputhandler.{h,cpp}`, `cpp/src/main.cpp`, `cpp/src/trayapp.{h,cpp}`, `cpp/src/settingsdialog.{h,cpp}`, `cpp/src/profileeditdialog.{h,cpp}` (nowe), `cpp/src/dbusinterface.{h,cpp}`, `cpp/src/i18n.cpp`, `cpp/CMakeLists.txt`, `cpp/tests/test_config.cpp`, `cpp/tests/test_inputhandler.cpp`
**Status:** Zrealizowane. Wprowadzono profile audio z osobnymi hotkeyami i opcjonalnymi modyfikatorami (Ctrl/Shift, L+R znormalizowane do kanonicznej postaci). Schema config rozszerzona o `profiles` array; migracja ze starego `selected_app`/`hotkeys` (deprecated mirror profile[0] dla backwards compat). InputHandler śledzi modyfikatory z grabowanych urządzeń, dispatch per-profile z debounce per-`(code, profileId)`. Settings dialog ma sekcję Profiles + nowy `ProfileEditDialog`. D-Bus wystawia `Profiles` property + `VolumeUp/Down/ToggleMuteProfile(id)` metody; bare metody i MPRIS targetują profile domyślny (backwards compat). Pending volume z `PaWorker` obsługuje brak działającej apki bez zmian. 53 testy przechodzą (23 config + 7 i18n + 3 pwutils + 5 volumecontroller + 15 inputhandler).

### 9. Integracja DBus / MPRIS ✓

**Problem:** Aplikacja działa tylko na poziomie evdev, nie integruje się z desktopowymi API.
**Rekomendacja:** Wystawić interfejs DBus (get/set volume, wybór aplikacji). MPRIS dałby integrację z KDE Connect, widgetami.
**Pliki:** Nowy `cpp/src/dbusinterface.h/cpp`, `cpp/src/mprisinterface.h/cpp`, zmiana CMakeLists.txt
**Status:** Zrealizowane. Własny interfejs `org.keyboardvolumeapp.VolumeControl` + MPRIS `org.mpris.MediaPlayer2`/`.Player`. Cache stanu, delegacja async do VolumeController. ExportAdaptors dla adaptorów MPRIS. Cleanup w App::cleanup().

### 10. CI/CD (GitHub Actions) ✓

**Problem:** Brak automatycznej walidacji przy PR/commit.
**Rekomendacja:** Workflow: build (Release + Debug), testy, lint (clang-format, clang-tidy).
**Pliki:** Nowy `.github/workflows/ci.yml`
**Status:** Zrealizowane. `.github/workflows/ci.yml` uruchamia się dla PR-ów i pushy do `main`, buduje projekt w konfiguracjach Debug oraz Release, uruchamia CTest i sprawdza `clang-format` tylko dla zmienionych plików C++ w `cpp/src` oraz `cpp/tests`. Workflow ma filtry ścieżek: zmiany wyłącznie dokumentacyjne nie uruchamiają CI, ale zmiany w `cpp/`, `pkg/`, `deploy/`, `resources/`, CMake oraz samym `ci.yml` już tak. `.clang-format` dodano w repo. `clang-tidy` pozostaje osobnym follow-upem. `Claude Code Review` jest tymczasowo wyłączony przez `if: false` w `.github/workflows/claude-code-review.yml`.

### 11. Systemd user service ✓

**Problem:** Autostart tylko przez KDE autostart, brak mechanizmu dla innych DE/WM.
**Rekomendacja:** Dodać `keyboard-volume-app.service` dla systemd user mode.
**Pliki:** Nowy `deploy/keyboard-volume-app.service`
**Status:** Zrealizowane. Dodano user unit z `ExecStart=/usr/bin/keyboard-volume-app`, restartem `on-failure` i `WantedBy=default.target`. CMake instaluje go do `lib/systemd/user`, czyli przy prefixie `/usr` do `/usr/lib/systemd/user/keyboard-volume-app.service`; `$HOME/.config/systemd/user` opisano w README tylko jako wariant ręcznej per-user instalacji. Paczka Arch nie wymaga zmian, bo korzysta z `cmake --install`.

### 12. Refaktoryzacja — deduplikacja kodu evdev ✓

**Problem:** `openDev`/`closeDev` i grab/release powtarzają się w `inputhandler.cpp` i `deviceselector.cpp`; `getVolumeDevices()` zduplikowane w `deviceselector.cpp` i `firstrunwizard.cpp`.
**Rekomendacja:** Wydzielić RAII wrapper `EvdevDevice`.
**Pliki:** Nowy `cpp/src/evdevdevice.h/cpp`, zmiany w `inputhandler.cpp`, `deviceselector.cpp`, `firstrunwizard.cpp`
**Status:** Zrealizowane. Klasa RAII `EvdevDevice` (move-only) zarządza fd, `libevdev*`, grab/ungrab i `libevdev_uinput*` z automatycznym cleanupem w destruktorze. Zduplikowana `getVolumeDevices()` wydobyta do `inputhandler.h/cpp`. 4/4 testów przechodzi.

### 13. Wersjonowanie binarki (--version, --help) ✓

**Problem:** Binarka nie ma `--version` ani `--help`.
**Rekomendacja:** Dodać QCommandLineParser z `--version` (z CMakeLists.txt) i `--help`.
**Pliki:** `cpp/src/main.cpp`
**Status:** Zrealizowane. Dodano `QCommandLineParser` obsługujący te flagi oraz wstrzykiwanie `APP_VERSION` zdefiniowanego w `CMakeLists.txt`.

### 14. Poprawki jakości kodu i stabilności ✓

**Problem:** Kilka nieformalnych bugów i brakujących guardsów wykrytych podczas przeglądu kodu: `volatile bool` zamiast `std::atomic<bool>` (data race), memory leaki (`Config`, `OSDWindow`), brak obsługi key repeat, brak singletona, brak walidacji pustej listy monitorów, połykanie wyjątków.
**Rekomendacja:** Batch quick-fix — 6 zmian w 6 plikach.
**Pliki:** `inputhandler.h`, `inputhandler.cpp`, `volumecontroller.cpp`, `main.cpp`, `osdwindow.cpp`, `settingsdialog.cpp`
**Status:** Zrealizowane. `volatile bool` → `std::atomic<bool>` (3 miejsca). `Config` → `std::unique_ptr`, `OSDWindow` → explicit delete. Key repeat (`ev.value == 2`). Singleton via D-Bus name check. Guard `screens.isEmpty()`. `catch(...)` → `qWarning()`. Build + 4/4 testów OK.

### 15. Pozycjonowanie dialogów na właściwym monitorze (XWayland) ✓

**Problem:** Na XWayland z wieloma monitorami dialogi (Settings, AppSelector, DeviceSelector, FirstRunWizard) wyskakują na złym monitorze — Qt domyślnie centruje na primary screen, który na XWayland jest zawodny.
**Rekomendacja:** API `centerDialogOnScreenAt(window, QCursor::pos())` przed `exec()` — robi `ensurePolished`, `adjustSize`, wylicza rozmiar z `sizeHint/minimumSizeHint/minimumSize`, centruje w `screen->availableGeometry()` z clampem do granic.
**Pliki:** Nowy `cpp/src/screenutils.h`, zmiany w `cpp/src/trayapp.cpp`, `cpp/src/main.cpp`
**Status:** Zrealizowane. Header-only utility `centerDialogOnScreenAt(QWidget*, QPoint)` z fallbackiem do `primaryScreen()`. Bez event filterów, QTimer ani zmian flag Qt::Dialog. 4/4 testów OK.

---

## Priorytet 4 — Funkcje Power User (Killer Features)

### 16. Tryb "Follow Focus" (Dynamiczne śledzenie aktywnego okna)

**Problem:** Konieczność ręcznej zmiany profili lub zapamiętywania wielu skrótów klawiszowych dla poszczególnych aplikacji.
**Rekomendacja:** Dodanie profilu `Focus`, który poprzez zapytania do systemu (X11 / D-Bus / KDE / Sway) śledzi PID/nazwę aktywnego okna na pulpicie i dynamicznie łączy go z odpowiednim węzłem w PipeWire. Sterowanie głośnością zawsze wpływa na okno będące na wierzchu.
**Pliki:** Do określenia (np. nowy moduł integracji WM, modyfikacja `config` i `volumecontroller`).
**Status:** Planowane.

### 17. Zaawansowane dopasowywanie aplikacji (Regex)

**Problem:** Niektóre aplikacje (szczególnie gry uruchamiane przez Wine/Proton lub aplikacje oparte na Electronie) zmieniają nazwy procesów lub generują wiele podprocesów, co psuje sztywne dopasowywanie po nazwie binarki.
**Rekomendacja:** Dodanie obsługi wyrażeń regularnych (`app_regex`) w definicji profili `config.json`. Możliwość zdefiniowania jednego profilu do obsługi całej grupy komunikatorów (np. `.*(discord|teams|slack|zoom).*`).
**Pliki:** `cpp/src/config.cpp`, `cpp/src/pwutils.cpp`, `cpp/src/volumecontroller.cpp`.
**Status:** Planowane.

### 18. Tryb "Audio Ducking" (Izolacja dźwiękowa / Focus Mode)

**Problem:** Gdy użytkownik odbiera połączenie (np. na Discordzie), musi ręcznie ściszać inne aplikacje w tle (muzykę, grę).
**Rekomendacja:** Wprowadzenie dedykowanej akcji / skrótu klawiszowego, który automatycznie obniża o np. 70% (lub całkowicie wycisza) głośność *wszystkich innych aplikacji* w PipeWire, oprócz aktualnie wybranego profilu. Drugie wciśnięcie przywraca stary stan.
**Pliki:** `cpp/src/volumecontroller.cpp`, `cpp/src/inputhandler.cpp`.
**Status:** Planowane.

### 19. Wbudowane CLI (Sub-komendy dla skryptów / Tiling WM)

**Problem:** Używanie `qdbus` w konfiguracjach takich środowisk jak Sway, Hyprland czy i3 jest uciążliwe ze względu na bardzo długie komendy.
**Rekomendacja:** Osobna binarka `kv-ctl` jako lekki klient QtDBus bez zależności od zewnętrznego programu `qdbus`, np. `kv-ctl up`, `kv-ctl down --profile firefox`, `kv-ctl set volume 35`.
**Pliki:** `cpp/src/kvctl.cpp`, `cpp/src/kvctlcommand.h/cpp`, `cpp/CMakeLists.txt`, `cpp/tests/test_kvctlcommand.cpp`.
**Status:** Zrealizowane. `kv-ctl` używa istniejącego endpointu `org.keyboardvolumeapp.VolumeControl`, obsługuje komendy `up/down/mute/refresh/get/set`, mapuje profile na metody per-profile i ma testowany parser argumentów bez potrzeby uruchamiania D-Bus.

### 20. Relatywne pozycjonowanie OSD (Anchor) oraz Custom CSS

**Problem:** Sztywne współrzędne X/Y dla OSD psują się przy przepinaniu monitorów, stacjach dokujących lub zmianach rozdzielczości na laptopie. Hardkodowany wygląd OSD nie pasuje do wszystkich ricingów.
**Rekomendacja:** Zastąpienie X/Y kotwicami (np. `anchor: "bottom-right", margin: 20`) oraz wczytywanie zewnętrznego pliku `theme.qss` (Qt Stylesheet) dla pełnej kastomizacji wyglądu okienka OSD.
**Pliki:** `cpp/src/config.cpp`, `cpp/src/osdwindow.cpp`, `cpp/src/settingsdialog.cpp`.
**Status:** Planowane.

---

## Weryfikacja (dla każdej zmiany)

1. `cmake --build cpp/build -j$(nproc)` — build bez błędów
2. Dla testów: `cd cpp/build && ctest --output-on-failure`
3. Dla paczkowania: `makepkg -f` (Arch) / `cpack` (deb/rpm)
4. Dla zmian UI: uruchomić i przetestować manualnie
