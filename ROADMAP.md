# keyboard-volume-app — roadmapa

Stan względem `main`: 5 listopada 2026. Roadmapa zachowuje pierwotne numery pozycji; szczegółowe zachowanie, instrukcje i testy opisują [dokumentacja](docs/pl/) oraz [ARCHITECTURE.md](ARCHITECTURE.md). Zrealizowane pozycje są skróconą historią, a nie deklaracją wyników bieżącego uruchomienia testów.

## Otwarte i częściowo zrealizowane

Kolejność poniżej zachowuje numery pozycji, nie oznacza terminów ani nowej kolejności realizacji. Priorytety z wcześniejszego przeglądu kodu (`#37`, `#33`, `#34`, `#38` i `#22`) zostały zrealizowane i przeniesione poniżej do sekcji „Zrealizowane".

### 16. Tryb "Follow Focus" (Dynamiczne śledzenie aktywnego okna)

**Problem:** Konieczność ręcznej zmiany profili lub zapamiętywania wielu skrótów klawiszowych dla poszczególnych aplikacji.
**Rekomendacja:** Rozważyć dalszą obsługę kompozytorów, które nie udostępniają używanego protokołu Wayland, zachowując obecne dopasowywanie aktywnego okna do profili.
**Pliki:** `cpp/src/windowtracker.h/cpp`, zmiany w `cpp/src/main.cpp`, `cpp/src/config.{h,cpp}`, `cpp/src/settingsdialog.cpp`, `cpp/src/profileeditdialog.cpp`, `cpp/src/i18n.cpp`, `cpp/CMakeLists.txt`.
**Status:** Częściowo zrealizowane. `WindowTracker` obsługuje X11/XWayland oraz Wayland na kompozytorach udostępniających `zwlr_foreign_toplevel_manager_v1` (zob. #29). Automatyczne przełączanie jest konfigurowane globalnie i per profil. Na pozostałych kompozytorach zależy od dostępności XWayland; szersza obsługa pozostaje otwarta.

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

### 20. Relatywne pozycjonowanie OSD (Anchor) oraz Custom CSS

**Status:** Zrealizowane. Dodano tryb kotwicowy `osd.anchor` (`custom` + 9 kotwic ekranu) i `margin_x`/`margin_y` (z fallbackiem skalaru `margin`), wspierany natywnie przez `LayerShellQt::Window` na Waylandzie oraz przez `computeAnchoredOsdPos()` + sygnały `screenAdded`/`screenRemoved` na X11/XWayland. Dodano opcjonalny zewnętrzny arkusz stylów `osd.custom_css_path` z tokenami `Q_PROPERTY` (`qproperty-panelBackground`, `qproperty-panelBorderColor`, `qproperty-panelBorderWidth`, `qproperty-panelRadius`) czytanymi przez `OSDWindow::paintEvent()`, kontrolki w `SettingsDialog` oraz testy w `test_config` i `test_osdwindow`.

### 21. Sceny audio / Mixer Presets

**Status:** Zrealizowane. `Config` serializuje i sanityzuje sceny, `DbusInterface` wystawia property `Scenes` i metodę `ApplyScene(id)`, `kv-ctl` obsługuje `scene ID` oraz `get scenes`. Sceny można edytować i uruchamiać w GUI, z użyciem hotkeya, D-Bus lub CLI; operacje głośności i wyciszenia są wykonywane asynchronicznie na `PaWorker`.

### 22. clang-tidy w CI

**Status:** Zrealizowane. Wyczyszczono zaległe ostrzeżenia `clang-tidy` w całym `cpp/src` i `cpp/tests`, wyłączono eksport `compile_commands.json` dla `EXCLUDE_FROM_ALL` targetu `kwin_tracker_live`, naprawiono budowanie nagłówków/autogen w jobie CI, włączono `WarningsAsErrors: "*"` w `.clang-tidy` i usunięto `continue-on-error: true` w `.github/workflows/ci.yml`.

### 23. Pasywne śledzenie modyfikatorów z niezależnych klawiatur (v2)

**Status:** Zrealizowane. `findHotkeyDevices()` / `selectHotkeyDevices()` obejmują jako trzecią kategorię niezależne klawiatury posiadające `KEY_LEFTCTRL`/`KEY_RIGHTCTRL`/`KEY_LEFTSHIFT`/`KEY_RIGHTSHIFT` otwierane pasywnie (read-only, bez `EVIOCGRAB` i bez uinput). Stan modyfikatorów jest śledzony per urządzenie (`heldModifiersByDevice`) i agregowany przez `aggregateHeldModifiers()`, więc rozłączenie jednego urządzenia nie zeruje modyfikatorów trzymanych na innym. Dodano testy jednostkowe w `cpp/tests/test_inputhandler.cpp`.

### 24. Hotkey „Pokaż głośność" (OSD bez zmiany głośności)

**Status:** Zrealizowane. Opcjonalny hotkey `show` wyświetla aktualną głośność profilu na OSD bez jej zmiany. Funkcja jest dostępna w ustawieniach, przez D-Bus oraz `kv-ctl show [--profile id]`.

### 25. Limity głośności per profil (minimalny / maksymalny poziom)

**Status:** Zrealizowane. Pola `vol_min`/`vol_max` (domyślnie 0/100) trzymane w profilu i klampowane w czterech ścieżkach `PaWorker`: active sink-input, stream-restore DB, węzeł PipeWire i parked-pending. Hotkeys profilu (`App::changeVolume`), `DbusInterface::setVolume` oraz `VolumeUp`/`VolumeDown` (bare + per-profile) respektują limity; sceny i `toggleDucking` celowo zachowują pełny zakres `[0,1]`. UI: dwa `QSpinBox` w `ProfileEditDialog` z wzajemnym domknięciem; sanitacja w `Config::setProfiles` (clamp do 0–100, swap przy odwróceniu).

### 26. Stopniowe wyciszanie ducking (fade in/out)

**Status:** Zrealizowane. Dodano `ducking.fade_out_ms` i `ducking.fade_in_ms` (0–5000 ms, domyślnie 0 ms) w `DuckingConfig`, UI (`ProfileEditDialog`) i D-Bus `Profiles`. `PaWorker::doToggleDucking()` napędza liniową rampę per kanał timerem 30 ms z płynnym odwracaniem przy przerwaniu w połowie i parkowaniem przywracania po utracie kontekstu PA; dodano testy `test_config` i `test_paworker`.

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

### 33. Wydzielenie klasy `App` z `main.cpp`

**Status:** Zrealizowane. Klasa `App` oraz `osdLayoutInputFromConfig()` wydzielone do `cpp/src/app.{h,cpp}` z opcjonalnym wstrzykiwaniem konfiguracji i `AppInitOptions` pod testy. `main.cpp` obsługuje wyłącznie startup (Wayland/XCB, sygnały UNIX, CLI, singleton, FirstRunWizard) bez `main.moc`.

### 34. Podział `volumecontroller.cpp`

**Status:** Zrealizowane. `volumecontroller.cpp` zredukowany do cienkiej fasady Qt; wydzielono `cpp/src/paworker.{h,cpp}`, `cpp/src/pawatcherthread.{h,cpp}` oraz `cpp/src/pabackend_helpers.{h,cpp}` z pełnym zachowaniem kontraktu wątkowego (`PaWorker` w dedykowanym wątku, `std::atomic<bool>`, brak `volumecontroller.moc`).

### 35. Podział `pwutils.cpp`

**Status:** Zrealizowane. Rozdzielono warstwy za stabilną fasadą `pwutils.h`: `pwsession.{h,cpp}` (sesja libpipewire i snapshot registry), `pwclientfilter.{h,cpp}` (czysta filtracja klientów i mapowanie właściciela po `client.id`), `pwnodecontrol.{h,cpp}` (odczyt/zapis `SPA_PARAM_Props` i przeliczenia głośności) oraz cienkie `pwutils.cpp` (`PipeWireVolumeBackend` i helpery one-shot).

### 36. Konfigurowalne filtry aplikacji audio

**Status:** Zrealizowane. Sekcja `audio_app_filters` z kluczami `extra_system_binaries` / `remove_system_binaries` / `extra_skip_app_names` / `remove_skip_app_names` nakłada się na wbudowane stałe z `appaudiofilters`. `Config::effectiveSystemBinaries()` / `effectiveSkipAppNames()` zasilają `listPipeWireClients` i `VolumeController`. UI filtrów odłożone (edycja przez config JSON).

### 37. D-Bus `setActiveApp()` → tray refresh

**Status:** Zrealizowane. Ręczna zmiana przez D-Bus emituje `manualAppSelected`, czyści tymczasowy wybór Follow Focus i odbudowuje menu z konfiguracji. Test integracyjny obejmuje prywatną sesję D-Bus i menu Qt offscreen; nie stanowi ręcznej weryfikacji zasobnika środowiska graficznego.

### 38. Uzupełnienie pokrycia testowego

**Status:** Zrealizowane. Obok istniejących testów konfiguracji dodano szew `IPaBackend` (`cpp/src/pabackend.h`, `cpp/src/pabackend_pulse.cpp`) z deterministycznymi testami `test_paworker` (reconnect/backoff, parkowanie i replay pending, limity per profil, ducking przez reconnect) oraz headlessowe testy wiringu `App` w `cpp/tests/test_app.cpp` pod `dbus-run-session`.

### 39. File / journald logging z `--log-level`

**Status:** Zrealizowane. Dodano wątkowo bezpieczny moduł `cpp/src/logging.{h,cpp}` z flagami CLI `--log-level` (`off|warn|info|debug|trace`), `--log-file` (w tym `default` w `XDG_STATE_HOME/keyboard-volume-app/keyboard-volume-app.log` z rotacją 5 MB) oraz `--log-journal` / `--no-journal` z opcjonalnym wsparciem `libsystemd` (`sd_journal_send`) i testami `test_logging`.

### 40. D-Bus sygnały device/app lifecycle

**Status:** Zrealizowane. `InputHandler` obserwuje `/dev/input` przez `inotify` w pętli `epoll` (stabilne adresy `std::deque<PollSource>`) i emituje `deviceConnected(path, name)` / `deviceDisconnected(path, name)` przy otwarciu, hot-plugu i odłączeniu. `PaWorker::doListApps()` diffuje listę aplikacji po pierwszym skanowaniu i emituje `appStarted(name, binary)` / `appStopped(name, binary)` przez `VolumeController`. `DbusInterface` wystawia wszystkie 4 sygnały jako `Q_SCRIPTABLE` na `org.keyboardvolumeapp.VolumeControl`; pokryte testami `test_paworker`, `test_dbusinterface` i `test_app`.

### 41. Skróty „następna / poprzednia aplikacja"

**Status:** Zrealizowane. Dodano `AppCycler` (stabilny snapshot z oknem 500 ms), opcjonalne hotkeye profilowe `hotkeys.next_app` / `hotkeys.prev_app` (UI w `ProfileEditDialog`), metody D-Bus `NextActiveApp()` / `PreviousActiveApp()`, podgląd OSD przy przełączeniu oraz komendy CLI `kv-ctl next-app` / `kv-ctl prev-app` z pełnym zestawem testów jednostkowych.

### 42. Globalne skróty multimedialne (MPRIS dispatch)

**Status:** Zrealizowane. Globalne skróty MPRIS mają nieprzypisane wartości domyślne, a skróty profili mają pierwszeństwo. Dostępne są ustawienia, metody D-Bus i komendy `kv-ctl media`.

## Weryfikacja zmian

Dobierz sprawdzenia do zakresu zmiany: receptury builda, CTest i przypadki wymagające ręcznej weryfikacji opisuje [ARCHITECTURE.md](ARCHITECTURE.md#tests). Przy zmianach pakowania sprawdź właściwy proces budowania pakietu, a przy zmianach interfejsu — odpowiedni scenariusz ręczny.
