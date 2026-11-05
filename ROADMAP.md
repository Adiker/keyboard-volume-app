# keyboard-volume-app — roadmapa

Stan względem `main`: 5 listopada 2026. Roadmapa zachowuje pierwotne numery pozycji; szczegółowe zachowanie, instrukcje i testy opisują [dokumentacja](docs/pl/) oraz [ARCHITECTURE.md](ARCHITECTURE.md). Zrealizowane pozycje są skróconą historią, a nie deklaracją wyników bieżącego uruchomienia testów.

## Otwarte i częściowo zrealizowane

Kolejność poniżej zachowuje numery pozycji, nie oznacza terminów ani nowej kolejności realizacji. Wcześniejsza notatka z przeglądu kodu wskazywała w pierwszej kolejności #37, następnie #33, #34, #22 i #38; statusy zweryfikowano poniżej.

### 16. Tryb "Follow Focus" (Dynamiczne śledzenie aktywnego okna)

**Problem:** Konieczność ręcznej zmiany profili lub zapamiętywania wielu skrótów klawiszowych dla poszczególnych aplikacji.
**Rekomendacja:** Rozważyć dalszą obsługę kompozytorów, które nie udostępniają używanego protokołu Wayland, zachowując obecne dopasowywanie aktywnego okna do profili.
**Pliki:** `cpp/src/windowtracker.h/cpp`, zmiany w `cpp/src/main.cpp`, `cpp/src/config.{h,cpp}`, `cpp/src/settingsdialog.cpp`, `cpp/src/profileeditdialog.cpp`, `cpp/src/i18n.cpp`, `cpp/CMakeLists.txt`.
**Status:** Częściowo zrealizowane. `WindowTracker` obsługuje X11/XWayland oraz Wayland na kompozytorach udostępniających `zwlr_foreign_toplevel_manager_v1` (zob. #29). Automatyczne przełączanie jest konfigurowane globalnie i per profil. Na pozostałych kompozytorach zależy od dostępności XWayland; szersza obsługa pozostaje otwarta.

### 20. Relatywne pozycjonowanie OSD (Anchor) oraz Custom CSS

**Problem:** Sztywne współrzędne X/Y dla OSD psują się przy przepinaniu monitorów, stacjach dokujących lub zmianach rozdzielczości na laptopie. Hardkodowany wygląd OSD nie pasuje do wszystkich ricingów.
**Rekomendacja:** Zastąpienie X/Y kotwicami (np. `anchor: "bottom-right", margin: 20`) oraz rozważenie zewnętrznego motywu OSD. Tło przezroczystego okna jest rysowane ręcznie, więc sam Qt Stylesheet nie wystarczy do jego stylizacji.
**Pliki:** `cpp/src/config.cpp`, `cpp/src/osdwindow.cpp`, `cpp/src/settingsdialog.cpp`.
**Status:** Planowane.

### 22. clang-tidy w CI

**Problem:** Analiza `clang-tidy` działa w CI, ale jej wyniki nie blokują merge; część reguł pozostaje wyłączona.
**Rekomendacja:** Stopniowo usuwać istniejące ostrzeżenia i rozważyć rozszerzenie zestawu reguł oraz blokowanie merge po uzyskaniu stabilnych wyników.
**Pliki:** `.github/workflows/ci.yml`, `.clang-tidy`
**Status:** Częściowo wdrożone / warning-only. Job "Clang-Tidy changed C++ files" dodany do CI z `continue-on-error: true` — nie blokuje merge. Plik `.clang-tidy` w katalogu głównym z konserwatywnym zestawem checkerów: `bugprone-*`, `performance-*`, `clang-analyzer-*`, wybrane `modernize-*` i `cppcoreguidelines-pro-type-member-init`. `readability-*` i `WarningsAsErrors` wyłączone do czasu wyczyszczenia istniejących ostrzeżeń.

### 23. Pasywne śledzenie modyfikatorów z niezależnych klawiatur (v2)

**Problem:** `InputHandler` śledzi stan modyfikatorów (Ctrl, Shift) na przechwytywanych urządzeniach oraz ich pasywnych urządzeniach siostrzanych. Użytkownik z osobną klawiaturą multimedialną i osobną klawiaturą alfanumeryczną nie może kwalifikować profili modyfikatorami z tej drugiej. Ograniczenie udokumentowane jako `TODO(v2)` w `inputhandler.h`.
**Rekomendacja:** Rozszerzyć `findHotkeyDevices()` o trzecią kategorię: urządzenia EV_KEY bez skonfigurowanych hotkeyów, lecz posiadające `KEY_LEFTCTRL`/`KEY_RIGHTCTRL`/`KEY_LEFTSHIFT`/`KEY_RIGHTSHIFT`. Otwierać je w trybie nieekskluzywnym (bez `EVIOCGRAB`, bez uinput) — wyłącznie do odczytu modyfikatorów. W pętli epoll odczytywać zdarzenia EV_KEY dla tych kodów i aktualizować `heldModifiers`, nie przechwytując ani nie przekierowując innych zdarzeń.
**Pliki:** `cpp/src/inputhandler.h`, `cpp/src/inputhandler.cpp`, `cpp/tests/test_inputhandler.cpp`
**Status:** Częściowo zrealizowane. Modyfikatory są odczytywane także z pasywnych urządzeń siostrzanych wybranego urządzenia. Niezależna klawiatura bez własnego przypisanego hotkeya nadal nie jest obserwowana; dalsze rozszerzenie pozostaje planowane.

### 26. Stopniowe wyciszanie ducking (fade in/out)

**Problem:** `ToggleDucking` zmienia głośność ściszanych aplikacji natychmiastowo — skok z np. 80% do 20% jest słyszalny jako nagłe tąpnięcie. Profesjonalne implementacje duckingu stosują płynne przejścia z konfigurowalnym czasem.
**Rekomendacja:** Dodać pola `ducking.fade_in_ms` i `ducking.fade_out_ms` do `DuckingConfig` (domyślnie 0 ms = zachowanie obecne, brak regresji). W `PaWorker::doToggleDucking()` zamiast natychmiastowych `setAppVolume()` uruchomić QTimer-driven ramp: co ~30 ms obliczać kolejną wartość głośności wzdłuż liniowej krzywej dla każdej ściszanej aplikacji. Anulowanie (drugie wciśnięcie) przerywa ramp i uruchamia fade powrotu do pierwotnych wartości z snapshotu.
**Pliki:** `cpp/src/config.h`, `cpp/src/config.cpp`, `cpp/src/volumecontroller.cpp`, `cpp/src/profileeditdialog.cpp`, `cpp/src/i18n.cpp`, `cpp/tests/test_config.cpp`
**Status:** Planowane.

### 33. Wydzielenie klasy `App` z `main.cpp`

**Problem:** `cpp/src/main.cpp` nadal trzyma klasę koordynatora `App` razem z `main()`. To już nie jest błąd lifetime, ale utrudnia testowanie wiringu i wymusza obecność `main.moc`. Konstrukcja oraz rejestracja D-Bus/MPRIS są sprzężone z wejściem aplikacji.
**Rekomendacja:** Wydzielić `App` do `cpp/src/app.h/cpp`. W `main.cpp` zostawić tylko: setup środowiska (Wayland/XCB decision logic), CLI (`QCommandLineParser`), singleton check przez D-Bus name, first-run i `app.exec()`.
**Pliki:** Nowy `cpp/src/app.h`, `cpp/src/app.cpp`, `cpp/src/main.cpp`, `cpp/CMakeLists.txt`.
**Status:** Planowane.

### 34. Podział `volumecontroller.cpp`

**Problem:** Plik łączy `VolumeController`, `PaWorker`, `PaWatcherThread`, sink-input introspection, stream-restore, pending queue, reconnect/backoff oraz orchestrację fallbacku PipeWire. Trudny w review i ryzykowny przy kolejnych zmianach hot patha.
**Rekomendacja:** Wydzielić: `paworker.h/cpp` (właściciel kontekstu PA i operacji volume/mute), `pawatcherthread.h/cpp` (subskrypcja sink-input + reconnect watcher), wewnętrzny namespace lub `pabackend_helpers.h/cpp` na callback data i stream-restore helpery. `volumecontroller.cpp` zostaje cienką fasadą Qt API.
**Pliki:** Nowe `cpp/src/paworker.h/cpp`, `cpp/src/pawatcherthread.h/cpp`, refactor `cpp/src/volumecontroller.{h,cpp}`, `cpp/CMakeLists.txt`, `cpp/tests/test_volumecontroller.cpp`.
**Status:** Planowane. Wymaga zachowania threading contract (libpulse tylko z `PaWorker`, `std::atomic<bool>` dla flag, brak ścieżki do main thread).

### 35. Podział `pwutils.cpp`

**Problem:** Plik miesza snapshot registry, listowanie klientów, filtrowanie, odczyt node'ów i zapis `SPA_PARAM_Props`. Aliasowanie (#7b) zostało już dodane.
**Rekomendacja:** Przed kolejnymi rozszerzeniami rozdzielić warstwy: registry snapshot, transformacja/filtr (filtrowanie klientów, mapping owner-by-`client.id`, normalizacja nazw), node control (read/write `SPA_PARAM_Props`).
**Pliki:** `cpp/src/pwutils.{h,cpp}` — refactor wewnątrzplikowy lub split na 2–3 jednostki.
**Status:** Planowane.

### 37. D-Bus `setActiveApp()` → tray refresh

**Problem:** `DbusInterface::setActiveApp()` zapisuje `Config::setSelectedApp(name)` i aktualizuje swój cache, ale wcześniej nie wymuszał jawnie odświeżenia stanu `TrayApp`/menu. Po zewnętrznej zmianie aktywnej aplikacji przez D-Bus UI mogło wymagać kolejnego eventu, żeby pokazać spójny stan.
**Rekomendacja:** Sprawdzić zachowanie ręcznie (`qdbus … setActiveApp …` + obserwacja menu). Jeśli problem się potwierdzi: dodać jawny sygnał/slot do zmiany domyślnej aplikacji przez `TrayApp` albo wspólną ścieżkę `Config → UI refresh`.
**Pliki:** `cpp/src/dbusinterface.{h,cpp}`, `cpp/src/trayapp.{h,cpp}`, `cpp/tests/test_dbusinterface.cpp`.
**Status:** Do weryfikacji. `setActiveApp()` aktualizuje konfigurację i emituje `activeAppChanged`; połączenie `TrayApp::appChanged` → `DbusInterface::onActiveAppChanged()` działa w przeciwnym kierunku. Spójność menu po zmianie przez D-Bus wymaga sprawdzenia.

### 38. Uzupełnienie pokrycia testowego

**Problem:** Część scenariuszy nadal wymaga dodatkowego pokrycia, zwłaszcza reconnect i pending state.
**Rekomendacja:** Zachować istniejące testy konfiguracji i dodać:

- testy reconnect/pending state w `VolumeController` bez realnego serwera PA (mock backend),
- testy wiringu `App` po wydzieleniu z `main.cpp` (zależy od #33).
**Pliki:** `cpp/tests/test_config.cpp`, `cpp/tests/test_volumecontroller.cpp`, ewentualnie nowy `cpp/tests/test_app.cpp` po #33.
**Status:** Częściowo zrealizowane. Testy uszkodzonego JSON, nieczytelnego pliku i niedostępnego katalogu istnieją; testy reconnect/pending oraz wiringu `App` pozostają do wykonania.

### 39. File / journald logging z `--log-level`

**Problem:** `qWarning()` / `qDebug()` trafiają na stderr, który systemd może zbierać w journalu. Brak konfigurowalnych poziomów i bardziej szczegółowego routingu utrudnia diagnostykę.
**Rekomendacja:** Dodać `--log-level` do `QCommandLineParser` (off/warn/info/debug/trace), routing do journald (przez `sd_journal_print` lub `QtLogging` dispatcher), opcjonalnie plik logów w `XDG_STATE_HOME`.
**Pliki:** `cpp/src/main.cpp`, ewentualnie nowy `cpp/src/logging.h/cpp`, `cpp/CMakeLists.txt`.
**Status:** Planowane.

### 40. D-Bus sygnały device/app lifecycle

**Problem:** Klienci D-Bus muszą polować `Apps` lub łapać `appsChanged`, żeby zorientować się że aplikacja audio się pojawiła/zniknęła. Dla zewnętrznych wskaźników (panel, widget) brakuje wyraźnych sygnałów `deviceConnected`/`deviceDisconnected` (evdev) i `appStarted`/`appStopped` (PipeWire) z metadanymi.
**Rekomendacja:** Dodać sygnały do `org.keyboardvolumeapp.VolumeControl` z minimum payload (`name`, `binary`). Emisja z `InputHandler` (evdev hot-plug) i `PaWatcherThread` (sink-input add/remove) przez async signal queued na main thread.
**Pliki:** `cpp/src/dbusinterface.{h,cpp}`, `cpp/src/inputhandler.{h,cpp}`, `cpp/src/volumecontroller.{h,cpp}`, `cpp/tests/test_dbusinterface.cpp`.
**Status:** Planowane.

### 41. Skróty „następna / poprzednia aplikacja"

**Problem:** Aby przełączyć cel głośności (active app), trzeba kliknąć w tray menu albo zawołać `kv-ctl set active-app NAME`. Dla power usera bez tray menu (tiling WM) nie ma natywnego skrótu.
**Rekomendacja:** Dodać dwie metody D-Bus `NextActiveApp()` / `PreviousActiveApp()` oraz odpowiadające im profilowe hotkeye (`hotkeys.next_app` / `hotkeys.prev_app` jako `HotkeyBinding`). `kv-ctl next-app` / `kv-ctl prev-app` w komplecie.
**Pliki:** `cpp/src/config.{h,cpp}`, `cpp/src/inputhandler.{h,cpp}`, `cpp/src/dbusinterface.{h,cpp}`, `cpp/src/kvctl*.{h,cpp}`, `cpp/src/i18n.cpp`, testy.
**Status:** Planowane. Uwaga: lista aplikacji jest dynamiczna (sink-input add/remove), więc cykl powinien być stabilny w obrębie jednego wciśnięcia (snapshot na początku, kolejne wciśnięcia w 500 ms używają tego samego snapshotu).

## Zrealizowane

### 1. Paczkowanie i dystrybucja (PKGBUILD + CPack)

**Status:** Zrealizowane. Dostępny jest PKGBUILD `keyboard-volume-app-git` oraz stabilna paczka Arch budowana z taga. Wydania GitHub zawierają także paczki DEB i RPM przez CPack (zob. #27), wraz z sumami kontrolnymi.

### 2. Testy jednostkowe i integracyjne

**Status:** Zrealizowane. Testy jednostkowe i integracyjne są zintegrowane z CTest; aktualny wykaz i sposób uruchamiania opisuje [ARCHITECTURE.md](ARCHITECTURE.md#tests). Pozostałe luki opisano w #38.

### 3. Poszanowanie XDG_CONFIG_HOME

**Status:** Zrealizowane (już używa QStandardPaths).

### 4. Zmniejszenie latencji klawiszy (200ms → 50ms)

**Status:** Zrealizowane. `epoll()` zastąpił `select()`; limit 50 ms ogranicza czas oczekiwania na sprawdzenie warunku zatrzymania, a gotowe zdarzenia mogą być obsłużone wcześniej.

### 5. Konfiguracja pierwszego uruchomienia (first-run wizard)

**Status:** Zrealizowane. Kreator pierwszego uruchomienia obejmuje wybór języka, urządzenia i domyślnej aplikacji audio (zob. #5a).

### 5a. Wybór domyślnej aplikacji w first-run wizard

**Status:** Zrealizowane. `AppPage` z listą klientów PipeWire (obecnie przez libpipewire). Zawiera opcję "Bez domyślnej aplikacji", przycisk Refresh, i zapisuje wybór do `Config::selectedApp`. Wydzielono `listPipeWireClients()` do wspólnego modułu `pwutils.h/cpp`. Lista wydzielona później do reusable `AppListWidget`.

### 5b. Zmiana domyślnej aplikacji z tray menu

**Status:** Zrealizowane. Tray menu ma "Change default application..." — modal dialog (`AppSelectorDialog`) z listą PipeWire, przyciskiem Refresh, opcją "Bez domyślnej aplikacji". Reusable `AppListWidget` współdzielony między `AppPage` (first-run wizard) i `AppSelectorDialog`. Bezpośrednie wybieranie aplikacji z tray listy (radio items) działa bez zmian.

### 5c. Automatyczne odświeżanie listy aplikacji audio

**Status:** Zrealizowane. PaWatcherThread emituje na NEW i REMOVE. PaWorker debounce timer (500ms) odświeża listę automatycznie. Istniejące połączenie appsReady → rebuildMenu obsługuje update tray menu. Brak zmian w API publicznym.

### 5d. Automatyczne ponowne połączenie PulseAudio

**Status:** Zrealizowane. `PaWorker` reconnectuje kontekst PA z backoffem 500ms → 30s, `PaWatcherThread` odbudowuje subskrypcję sink-input, a hot path sprawdza `contextReady()` przed użyciem libpulse. Transient refresh listy aplikacji nie nadpisuje już `selected_app`. `test_volumecontroller` ma dodatkowy smoke test dla niedostępnego PulseAudio.

### 6. Osadzenie ikony jako Qt resource

**Status:** Zrealizowane. `resources.qrc` w SOURCES, ikona `:/icon.png` przez QRC, usunięto POST_BUILD kopiowanie.

### 7. Wsparcie dla PipeWire przez API libpipewire

**Status:** Zrealizowane. `pwutils` używa libpipewire do snapshotu registry, listowania klientów PipeWire oraz odczytu/zapisu `SPA_PARAM_Props` na node'ach streamów. `VolumeController` zachowuje libpulse jako szybki primary backend, ale fallback dla idle/paused node'ów nie uruchamia już `pw-dump` ani `pw-cli`. Dodano zależność CMake `libpipewire-0.3` i testy filtrowania/deduplikacji klientów.

### 7a. Normalizacja nazw aplikacji audio PipeWire/PulseAudio

**Status:** Zrealizowane. `PipeWireClient` niesie teraz `name` (etykieta UI), `binary` (target sterowania) i `id` (global id klienta PipeWire). `pwutils` mapuje `Stream/Output/Audio` do właściciela przez `client.id`, więc Harmonoid jest pokazywany jako `harmonoid`, ale sterowany przez `mpv`. `VolumeController` dopasowuje aktywne sink-inputy także po `media.name`, a `listApps()` normalizuje nazwy aktywnych streamów przez tę samą mapę co picker profilu. Dodano testy dla Harmonoid/mpv oraz YouTube Music/Playback.

### 7b. Aliasowanie i ręczne nazwy aplikacji audio

**Status:** Zrealizowane. Dodano opcjonalną sekcję `app_aliases` w `config.json` (`match` / `display` / `target`): UI pokazuje `display`, hot path steruje `target` (lub oryginalną binarką gdy `target` jest puste). Aliasy są aplikowane w `listApps()`, `AppListWidget` i po heurystykach PipeWire. Settings ma tabelę aliasów (Add/Edit/Remove).

### 8. Obsługa wielu aplikacji jednocześnie

**Status:** Zrealizowane. Wprowadzono profile audio z osobnymi hotkeyami i opcjonalnymi modyfikatorami (Ctrl/Shift, L+R znormalizowane do kanonicznej postaci). Schema config rozszerzona o `profiles` array; migracja ze starego `selected_app`/`hotkeys` (deprecated mirror profile[0] dla backwards compat). InputHandler śledzi modyfikatory z grabowanych urządzeń i pasywnych urządzeń siostrzanych, dispatch per-profile z debounce per-`(binding, profileId)`. Settings dialog ma sekcję Profiles + nowy `ProfileEditDialog`. D-Bus wystawia `Profiles` property + `VolumeUp/Down/ToggleMuteProfile(id)` metody; bare metody i MPRIS targetują profile domyślny (backwards compat). Pending volume z `PaWorker` obsługuje brak działającej apki bez zmian.

### 9. Integracja DBus / MPRIS

**Status:** Zrealizowane. Własny interfejs `org.keyboardvolumeapp.VolumeControl` + MPRIS `org.mpris.MediaPlayer2`/`.Player`. Cache stanu i delegacja async do VolumeController. Endpoint MPRIS jest opcjonalny i domyślnie wyłączony.

### 10. CI/CD (GitHub Actions)

**Status:** Zrealizowane. CI buduje konfiguracje Debug i Release, uruchamia CTest oraz sprawdza formatowanie zmienionych plików C++. `clang-tidy` działa osobno w trybie ostrzegawczym (zob. #22); zmiany wyłącznie dokumentacyjne nie uruchamiają CI.

### 11. Systemd user service

**Status:** Zrealizowane. Dodano user unit z `ExecStart=/usr/bin/keyboard-volume-app`, restartem `on-failure` i `WantedBy=default.target`. CMake instaluje go do `lib/systemd/user`, czyli przy prefixie `/usr` do `/usr/lib/systemd/user/keyboard-volume-app.service`; `$HOME/.config/systemd/user` opisano w [przewodniku instalacji](docs/pl/installation.md) jako wariant ręcznej per-user instalacji. Paczka Arch nie wymaga zmian, bo korzysta z `cmake --install`.

### 12. Refaktoryzacja — deduplikacja kodu evdev

**Status:** Zrealizowane. Klasa RAII `EvdevDevice` (move-only) zarządza fd, `libevdev*`, grab/ungrab i `libevdev_uinput*` z automatycznym cleanupem w destruktorze. Zduplikowana `getVolumeDevices()` wydobyta do `inputhandler.h/cpp`.

### 13. Wersjonowanie binarki (--version, --help)

**Status:** Zrealizowane. Dodano `QCommandLineParser` obsługujący te flagi oraz wstrzykiwanie `APP_VERSION` zdefiniowanego w `CMakeLists.txt`.

### 14. Poprawki jakości kodu i stabilności

**Status:** Zrealizowane. `volatile bool` → `std::atomic<bool>` (3 miejsca). `Config` → `std::unique_ptr`, `OSDWindow` → explicit delete. Key repeat (`ev.value == 2`). Singleton via D-Bus name check. Guard `screens.isEmpty()`. `catch(...)` → `qWarning()`.

### 15. Pozycjonowanie dialogów na właściwym monitorze (XWayland)

**Status:** Zrealizowane. Header-only utility `centerDialogOnScreenAt(QWidget*, QPoint)` z fallbackiem do `primaryScreen()`. Bez event filterów, QTimer ani zmian flag Qt::Dialog.

### 17. Zaawansowane dopasowywanie aplikacji (Regex)

**Status:** Zrealizowane. Pole `app_regex` w profilu (case-insensitive `QRegularExpression`) rozszerza dopasowanie Follow Focus / `findProfileByApp` obok listy `apps`. UI: pole w `ProfileEditDialog`; D-Bus `Profiles` eksponuje `app_regex` i `apps`.

### 18. Tryb "Audio Ducking" (Izolacja dźwiękowa / Focus Mode)

**Status:** Zrealizowane jako ręczny Focus Audio per profil. Hotkey zmniejsza głośność innych aplikacji i przy kolejnym użyciu przywraca zapisany stan. Funkcja jest dostępna także przez D-Bus i `kv-ctl duck [--profile id]`.

### 19. Wbudowane CLI (Sub-komendy dla skryptów / Tiling WM)

**Status:** Zrealizowane. `kv-ctl` używa istniejącego endpointu `org.keyboardvolumeapp.VolumeControl`, obsługuje komendy `up/down/mute/refresh/get/set`, mapuje profile na metody per-profile i ma testowany parser argumentów bez potrzeby uruchamiania D-Bus.

### 21. Sceny audio / Mixer Presets

**Status:** Zrealizowane. `Config` serializuje i sanityzuje sceny, `DbusInterface` wystawia property `Scenes` i metodę `ApplyScene(id)`, `kv-ctl` obsługuje `scene ID` oraz `get scenes`. Sceny można edytować i uruchamiać w GUI, z użyciem hotkeya, D-Bus lub CLI; operacje głośności i wyciszenia są wykonywane asynchronicznie na `PaWorker`.

### 24. Hotkey „Pokaż głośność" (OSD bez zmiany głośności)

**Status:** Zrealizowane. Opcjonalny hotkey `show` wyświetla aktualną głośność profilu na OSD bez jej zmiany. Funkcja jest dostępna w ustawieniach, przez D-Bus oraz `kv-ctl show [--profile id]`.

### 25. Limity głośności per profil (minimalny / maksymalny poziom)

**Status:** Zrealizowane. Pola `vol_min`/`vol_max` (domyślnie 0/100) trzymane w profilu i klampowane w czterech ścieżkach `PaWorker`: active sink-input, stream-restore DB, węzeł PipeWire i parked-pending. Hotkeys profilu (`App::changeVolume`), `DbusInterface::setVolume` oraz `VolumeUp`/`VolumeDown` (bare + per-profile) respektują limity; sceny i `toggleDucking` celowo zachowują pełny zakres `[0,1]`. UI: dwa `QSpinBox` w `ProfileEditDialog` z wzajemnym domknięciem; sanitacja w `Config::setProfiles` (clamp do 0–100, swap przy odwróceniu).

### 27. Pakietowanie .deb i .rpm przez CPack

**Status:** Zrealizowane. CPack buduje paczki DEB i RPM, a workflow wydań publikuje je wraz z paczką Arch `.pkg.tar.zst` oraz `SHA256SUMS.txt` jako assety GitHub Release.

### 28. Natywne pozycjonowanie OSD na Waylandzie (wlr-layer-shell)

**Status:** Zrealizowane. Przed `QApplication` probujemy `zwlr_layer_shell_v1` przez `wayland-client`. Jeśli protokół dostępny: `g_nativeWayland=true`, `OSDWindow::initLayerShell()` ustawia `LayerShellQt::Window` (layer=Overlay, anchor=TopLeft, keyboardInteractivity=None). `positionWindow()` przelicza absolutne X/Y na marginesy layer-shell per-output przez `QApplication::screenAt()`. Gdy natywna ścieżka jest niedostępna, a `QT_QPA_PLATFORM` nie jest ustawione, aplikacja wybiera `xcb`. Obie ścieżki są opcjonalne na etapie kompilacji (`HAVE_WAYLAND_CLIENT`, `HAVE_LAYER_SHELL_QT`); `PKGBUILD` dodaje `wayland` i `layer-shell-qt` do zależności budowania/paczki, żeby binarki Arch miały natywną ścieżkę Wayland. Znane ograniczenia: multi-screen routing best-effort (pełne wsparcie kotwic → punkt #20); GNOME bez XWayland poza zakresem (brak `zwlr_layer_shell_v1`).

### 29. WindowTracker na czystym Waylandzie (wlr-foreign-toplevel)

**Status:** Zrealizowane. `WindowTracker` wybiera w runtime backend Wayland, gdy `WAYLAND_DISPLAY` jest ustawione i registry wystawia `zwlr_foreign_toplevel_manager_v1`; w przeciwnym razie używa dotychczasowego XCB fallbacku, jeśli dostępny jest `DISPLAY`. Kod protokołu jest generowany przez `wayland-scanner` z vendored XML tylko gdy obecne są `wayland-client` i scanner (`HAVE_WAYLAND_FOREIGN_TOPLEVEL`). Pętla Wayland używa fd z `wl_display_get_fd()` i `poll()` z timeoutem 50ms, a XCB polling 500ms pozostał bez zmian.

### 30. Wyczyść przypisany hotkey z UI

**Status:** Zrealizowane. Prawy klik na każdym polu hotkeya w `ProfileEditDialog` → menu `Unassign` / `Wyczyść` → resetuje binding do `HotkeyBinding{}` (code = 0, nieprzypisane). Bez zmian w hot path ani w capture mechanic.

### 31. Interaktywny pasek postępu odtwarzania na OSD (MPRIS consumer)

**Status:** Zrealizowane. OSD korzysta z `MprisClient` do wyświetlania utworu i pozycji, umożliwia przewijanie oraz konfigurację odtwarzaczy; opcję paska wystawiono także przez D-Bus i `kv-ctl`.

### 32. Przyciski sterowania odtwarzaniem na OSD + skalowanie OSD

**Status:** Zrealizowane. OSD ma przyciski sterowania odtwarzaniem oraz konfigurowalną skalę; obsługuje też zmianę rozmiaru myszą.

### 36. Konfigurowalne filtry aplikacji audio

**Status:** Zrealizowane. Sekcja `audio_app_filters` z kluczami `extra_system_binaries` / `remove_system_binaries` / `extra_skip_app_names` / `remove_skip_app_names` nakłada się na wbudowane stałe z `appaudiofilters`. `Config::effectiveSystemBinaries()` / `effectiveSkipAppNames()` zasilają `listPipeWireClients` i `VolumeController`. UI filtrów odłożone (edycja przez config JSON).

### 42. Globalne skróty multimedialne (MPRIS dispatch)

**Status:** Zrealizowane. Globalne skróty MPRIS mają nieprzypisane wartości domyślne, a skróty profili mają pierwszeństwo. Dostępne są ustawienia, metody D-Bus i komendy `kv-ctl media`.

## Weryfikacja zmian

Dobierz sprawdzenia do zakresu zmiany: receptury builda, CTest i przypadki wymagające ręcznej weryfikacji opisuje [ARCHITECTURE.md](ARCHITECTURE.md#tests). Przy zmianach pakowania sprawdź właściwy proces budowania pakietu, a przy zmianach interfejsu — odpowiedni scenariusz ręczny.
