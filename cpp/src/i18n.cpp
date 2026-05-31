#include "i18n.h"

// ─── String tables ─────────────────────────────────────────────────────────────
static const QMap<QString, QMap<QString, QString>>& strings()
{
    static const QMap<QString, QMap<QString, QString>> s{
        {QStringLiteral("en"),
         {
             // tray
             {QStringLiteral("tray.section.audio_app"), QStringLiteral("Audio application")},
             {QStringLiteral("tray.action.refresh"), QStringLiteral("Refresh app list")},
             {QStringLiteral("tray.action.change_app"),
              QStringLiteral("Change default application...")},
             {QStringLiteral("tray.action.apply_scene"), QStringLiteral("Apply scene")},
             {QStringLiteral("tray.action.change_device"),
              QStringLiteral("Change input device...")},
             {QStringLiteral("tray.action.settings"), QStringLiteral("Settings...")},
             {QStringLiteral("tray.action.quit"), QStringLiteral("Quit")},
             // device selector
             {QStringLiteral("device.title"), QStringLiteral("Select input device")},
             {QStringLiteral("device.title.first_run"),
              QStringLiteral("Select input device (first launch)")},
             {QStringLiteral("device.label"),
              QStringLiteral("Devices with volume keys (Volume Up / Volume Down):")},
             {QStringLiteral("device.btn.refresh"), QStringLiteral("Refresh")},
             {QStringLiteral("device.no_devices"), QStringLiteral("No compatible devices found")},
             // settings
             {QStringLiteral("settings.title"), QStringLiteral("Settings")},
             {QStringLiteral("settings.osd_timeout"), QStringLiteral("OSD timeout:")},
             {QStringLiteral("settings.osd_position"), QStringLiteral("OSD position:")},
             {QStringLiteral("settings.volume_step"), QStringLiteral("Volume step:")},
             {QStringLiteral("settings.color_bg"), QStringLiteral("Background color:")},
             {QStringLiteral("settings.color_text"), QStringLiteral("Text color:")},
             {QStringLiteral("settings.color_bar"), QStringLiteral("Bar color:")},
             {QStringLiteral("settings.opacity"), QStringLiteral("Opacity:")},
             {QStringLiteral("settings.preview_btn"), QStringLiteral("Preview OSD")},
             {QStringLiteral("settings.language"), QStringLiteral("Language:")},
             {QStringLiteral("settings.osd_screen"), QStringLiteral("OSD screen:")},
             {QStringLiteral("settings.screen_primary"), QStringLiteral("primary")},
             {QStringLiteral("settings.hotkeys_section"), QStringLiteral("Hotkeys")},
             {QStringLiteral("settings.hotkey.volume_up"), QStringLiteral("Volume up:")},
             {QStringLiteral("settings.hotkey.volume_down"), QStringLiteral("Volume down:")},
             {QStringLiteral("settings.hotkey.mute"), QStringLiteral("Mute:")},
             {QStringLiteral("settings.hotkey.show"), QStringLiteral("Show volume:")},
             {QStringLiteral("settings.hotkey.capture_title"), QStringLiteral("Capture key")},
             {QStringLiteral("settings.hotkey.capture_prompt"),
              QStringLiteral("Press the key you want to bind.\nEsc or Cancel to abort.")},
             {QStringLiteral("settings.hotkey.capture_cancel"), QStringLiteral("Cancel")},
             {QStringLiteral("settings.hotkey.unassign"), QStringLiteral("Unassign")},
             // profiles
             {QStringLiteral("settings.profiles.section"), QStringLiteral("Profiles")},
             {QStringLiteral("settings.profiles.add"), QStringLiteral("Add")},
             {QStringLiteral("settings.profiles.remove"), QStringLiteral("Remove")},
             {QStringLiteral("settings.profiles.edit"), QStringLiteral("Edit")},
             {QStringLiteral("settings.profiles.set_default"), QStringLiteral("Set as default")},
             {QStringLiteral("settings.profiles.col_name"), QStringLiteral("Name")},
             {QStringLiteral("settings.profiles.col_app"), QStringLiteral("Apps")},
             {QStringLiteral("settings.profiles.col_modifiers"), QStringLiteral("Modifiers")},
             {QStringLiteral("settings.profiles.col_volume_up"), QStringLiteral("Vol+")},
             {QStringLiteral("settings.profiles.col_volume_down"), QStringLiteral("Vol-")},
             {QStringLiteral("settings.profiles.col_mute"), QStringLiteral("Mute")},
             {QStringLiteral("settings.profiles.col_ducking"), QStringLiteral("Ducking")},
             {QStringLiteral("settings.profiles.col_sink"), QStringLiteral("Sink")},
             {QStringLiteral("settings.profiles.sink_label"), QStringLiteral("Output device:")},
             {QStringLiteral("settings.profiles.sink_default"), QStringLiteral("(system default)")},
             {QStringLiteral("settings.profiles.sink_missing"), QStringLiteral("(missing) %1")},
             {QStringLiteral("settings.profiles.modifier_ctrl"), QStringLiteral("Ctrl")},
             {QStringLiteral("settings.profiles.modifier_shift"), QStringLiteral("Shift")},
             {QStringLiteral("settings.profiles.modifier_none"), QStringLiteral("(none)")},
             {QStringLiteral("settings.profiles.edit_title"), QStringLiteral("Edit profile")},
             {QStringLiteral("settings.profiles.name_label"), QStringLiteral("Name:")},
             {QStringLiteral("settings.profiles.app_add"), QStringLiteral("Add")},
             {QStringLiteral("settings.profiles.app_remove"), QStringLiteral("Remove")},
             {QStringLiteral("settings.profiles.app_up"), QStringLiteral("Move up")},
             {QStringLiteral("settings.profiles.app_down"), QStringLiteral("Move down")},
             {QStringLiteral("settings.profiles.duplicate_title"),
              QStringLiteral("Duplicate application")},
             {QStringLiteral("settings.profiles.duplicate_msg"),
              QStringLiteral("This application is already in the profile.")},
             {QStringLiteral("settings.profiles.ducking_enabled"),
              QStringLiteral("Focus audio: lower other apps")},
             {QStringLiteral("settings.profiles.ducking_volume"),
              QStringLiteral("Other apps volume:")},
             {QStringLiteral("settings.profiles.ducking_hotkey"),
              QStringLiteral("Focus audio hotkey:")},
             {QStringLiteral("settings.profiles.cannot_remove_last"),
              QStringLiteral("At least one profile must remain.")},
             {QStringLiteral("settings.profiles.auto_switch"),
              QStringLiteral("Auto-switch: participate in window-focus switching")},
             {QStringLiteral("settings.profiles.vol_limits"), QStringLiteral("Volume limits:")},
             {QStringLiteral("settings.profiles.vol_min_label"), QStringLiteral("Min:")},
             {QStringLiteral("settings.profiles.vol_max_label"), QStringLiteral("Max:")},
             // scenes
             {QStringLiteral("settings.scenes.section"), QStringLiteral("Scenes")},
             {QStringLiteral("settings.scenes.add"), QStringLiteral("Add")},
             {QStringLiteral("settings.scenes.edit"), QStringLiteral("Edit")},
             {QStringLiteral("settings.scenes.remove"), QStringLiteral("Remove")},
             {QStringLiteral("settings.scenes.duplicate"), QStringLiteral("Duplicate")},
             {QStringLiteral("settings.scenes.apply"), QStringLiteral("Apply")},
             {QStringLiteral("settings.scenes.col_name"), QStringLiteral("Name")},
             {QStringLiteral("settings.scenes.col_targets"), QStringLiteral("Targets")},
             {QStringLiteral("settings.scenes.col_hotkey"), QStringLiteral("Hotkey")},
             {QStringLiteral("settings.scenes.no_targets"), QStringLiteral("(no targets)")},
             {QStringLiteral("settings.scenes.edit_title"), QStringLiteral("Edit scene")},
             {QStringLiteral("settings.scenes.name_label"), QStringLiteral("Name:")},
             {QStringLiteral("settings.scenes.hotkey_label"), QStringLiteral("Hotkey:")},
             {QStringLiteral("settings.scenes.targets_label"), QStringLiteral("Targets")},
             {QStringLiteral("settings.scenes.target_add"), QStringLiteral("Add")},
             {QStringLiteral("settings.scenes.target_edit"), QStringLiteral("Edit")},
             {QStringLiteral("settings.scenes.target_remove"), QStringLiteral("Remove")},
             {QStringLiteral("settings.scenes.target_title"), QStringLiteral("Scene target")},
             {QStringLiteral("settings.scenes.col_match"), QStringLiteral("Application")},
             {QStringLiteral("settings.scenes.col_volume"), QStringLiteral("Volume")},
             {QStringLiteral("settings.scenes.col_mute"), QStringLiteral("Mute")},
             {QStringLiteral("settings.scenes.match_hint"),
              QStringLiteral("App or binary name (e.g. Spotify)")},
             {QStringLiteral("settings.scenes.set_volume"), QStringLiteral("Set volume")},
             {QStringLiteral("settings.scenes.vol_leave"), QStringLiteral("unchanged")},
             {QStringLiteral("settings.scenes.mute_leave"), QStringLiteral("Leave unchanged")},
             {QStringLiteral("settings.scenes.mute_on"), QStringLiteral("Mute")},
             {QStringLiteral("settings.scenes.mute_off"), QStringLiteral("Unmute")},
             {QStringLiteral("settings.scenes.copy_suffix"), QStringLiteral(" (copy)")},
             // auto profile switch
             {QStringLiteral("settings.auto_profile_switch"),
              QStringLiteral("Auto-switch profile by focused window")},
             // media hotkeys (global, MPRIS)
             {QStringLiteral("settings.media.section"), QStringLiteral("Media hotkeys")},
             {QStringLiteral("settings.media.play_pause"), QStringLiteral("Play / Pause:")},
             {QStringLiteral("settings.media.next"), QStringLiteral("Next track:")},
             {QStringLiteral("settings.media.previous"), QStringLiteral("Previous track:")},
             {QStringLiteral("settings.media.stop"), QStringLiteral("Stop:")},
             // progress / MPRIS
             {QStringLiteral("settings.progress.section"), QStringLiteral("Playback progress")},
             {QStringLiteral("settings.progress.enabled"),
              QStringLiteral("Show playback progress bar")},
             {QStringLiteral("settings.progress.interactive"),
              QStringLiteral("Allow seeking by clicking the bar")},
             {QStringLiteral("settings.progress.poll_ms"),
              QStringLiteral("Position poll interval:")},
             {QStringLiteral("settings.progress.label_mode"), QStringLiteral("Track label:")},
             {QStringLiteral("settings.progress.label_app"), QStringLiteral("App name")},
             {QStringLiteral("settings.progress.label_title_artist"),
              QStringLiteral("Title — Artist")},
             {QStringLiteral("settings.progress.label_artist_title"),
              QStringLiteral("Artist — Title")},
             {QStringLiteral("settings.progress.label_app_track"),
              QStringLiteral("App + Title — Artist")},
             {QStringLiteral("settings.progress.label_player_track"),
              QStringLiteral("Player + Title — Artist")},
             {QStringLiteral("settings.progress.label_player_track_art"),
              QStringLiteral("Player + Title — Artist + Album art")},
             {QStringLiteral("settings.progress.label_custom"), QStringLiteral("Custom…")},
             {QStringLiteral("settings.progress.custom_top"), QStringLiteral("Top line:")},
             {QStringLiteral("settings.progress.custom_bottom"), QStringLiteral("Bottom line:")},
             {QStringLiteral("settings.progress.custom_show_art"),
              QStringLiteral("Show album art")},
             {QStringLiteral("settings.progress.custom_tokens_hint"),
              QStringLiteral(
                  "Tokens: {app}, {player}, {title}, {artist}, {album}. Empty line = hidden.")},
             {QStringLiteral("settings.progress.tracked_players"),
              QStringLiteral("Tracked players (priority order):")},
             {QStringLiteral("settings.progress.tracked_players_hint"),
              QStringLiteral("Comma-separated substrings, e.g. spotify, youtube")},
             {QStringLiteral("settings.progress.media_controls"),
              QStringLiteral("Show media controls (play/pause/next/prev)")},
             {QStringLiteral("settings.progress.expose_mpris"),
              QStringLiteral("Expose fake MPRIS player endpoint (may interfere with Discord Music "
                             "Presence and similar apps)")},
             // osd
             {QStringLiteral("osd.preview"), QStringLiteral("OSD Preview")},
             {QStringLiteral("settings.osd_scale"), QStringLiteral("OSD scale:")},
             // app selector
             {QStringLiteral("app_selector.title"), QStringLiteral("Default application")},
             {QStringLiteral("app_selector.subtitle"),
              QStringLiteral("Select which audio application receives volume key events.")},
             {QStringLiteral("app_selector.no_default"), QStringLiteral("No default application")},
             {QStringLiteral("app_selector.no_apps"),
              QStringLiteral("No audio applications found")},
             {QStringLiteral("app_selector.refresh"), QStringLiteral("Refresh")},
             // wizard
             {QStringLiteral("wizard.welcome_title"), QStringLiteral("Welcome")},
             {QStringLiteral("wizard.welcome_text"),
              QStringLiteral(
                  "This application intercepts keyboard volume keys at the hardware level\n"
                  "and routes them to a single audio application instead of the system master "
                  "volume.\n\n"
                  "Let's quickly configure the basics. You can change everything later in "
                  "Settings.")},
             {QStringLiteral("wizard.lang_label"), QStringLiteral("Language:")},
             {QStringLiteral("wizard.device_title"), QStringLiteral("Input device")},
             {QStringLiteral("wizard.app_title"), QStringLiteral("Default application")},
             {QStringLiteral("wizard.app_subtitle"),
              QStringLiteral("Select which audio application receives volume key events.")},
             {QStringLiteral("wizard.app_empty"), QStringLiteral("No default application")},
             {QStringLiteral("wizard.app_no_apps"), QStringLiteral("No audio applications found")},
             {QStringLiteral("wizard.app_refresh"), QStringLiteral("Refresh")},
             // warnings
             {QStringLiteral("warn.no_device.title"), QStringLiteral("No device selected")},
             {QStringLiteral("warn.no_device.text"),
              QStringLiteral(
                  "No input device selected.\n"
                  "You can select one later from tray menu \u2192 \"Change input device...\"")},
         }},
        {QStringLiteral("pl"),
         {
             // tray
             {QStringLiteral("tray.section.audio_app"), QStringLiteral("Aplikacja audio")},
             {QStringLiteral("tray.action.refresh"),
              QStringLiteral("Od\u015bwie\u017c list\u0119 aplikacji")},
             {QStringLiteral("tray.action.change_app"),
              QStringLiteral("Zmie\u0144 domy\u015bln\u0105 aplikacj\u0119...")},
             {QStringLiteral("tray.action.apply_scene"), QStringLiteral("Zastosuj scen\u0119")},
             {QStringLiteral("tray.action.change_device"),
              QStringLiteral("Zmie\u0144 urz\u0105dzenie wej\u015bciowe...")},
             {QStringLiteral("tray.action.settings"), QStringLiteral("Ustawienia...")},
             {QStringLiteral("tray.action.quit"), QStringLiteral("Wyj\u015bcie")},
             // device selector
             {QStringLiteral("device.title"),
              QStringLiteral("Wybierz urz\u0105dzenie wej\u015bciowe")},
             {QStringLiteral("device.title.first_run"),
              QStringLiteral("Wybierz urz\u0105dzenie wej\u015bciowe (pierwsze uruchomienie)")},
             {QStringLiteral("device.label"),
              QStringLiteral("Urz\u0105dzenia z klawiszami g\u0142o\u015bno\u015bci (Volume Up / "
                             "Volume Down):")},
             {QStringLiteral("device.btn.refresh"), QStringLiteral("Od\u015bwie\u017c")},
             {QStringLiteral("device.no_devices"),
              QStringLiteral("Brak dost\u0119pnych urz\u0105dze\u0144")},
             // settings
             {QStringLiteral("settings.title"), QStringLiteral("Ustawienia")},
             {QStringLiteral("settings.osd_timeout"), QStringLiteral("Czas OSD:")},
             {QStringLiteral("settings.osd_position"), QStringLiteral("Pozycja OSD:")},
             {QStringLiteral("settings.volume_step"),
              QStringLiteral("Krok g\u0142o\u015bno\u015bci:")},
             {QStringLiteral("settings.color_bg"), QStringLiteral("Kolor t\u0142a:")},
             {QStringLiteral("settings.color_text"), QStringLiteral("Kolor tekstu:")},
             {QStringLiteral("settings.color_bar"), QStringLiteral("Kolor paska:")},
             {QStringLiteral("settings.opacity"), QStringLiteral("Przezroczysto\u015b\u0107:")},
             {QStringLiteral("settings.preview_btn"), QStringLiteral("Podgl\u0105d OSD")},
             {QStringLiteral("settings.language"), QStringLiteral("J\u0119zyk:")},
             {QStringLiteral("settings.osd_screen"), QStringLiteral("Ekran OSD:")},
             {QStringLiteral("settings.screen_primary"), QStringLiteral("g\u0142\u00f3wny")},
             {QStringLiteral("settings.hotkeys_section"), QStringLiteral("Skr\u00f3ty klawiszowe")},
             {QStringLiteral("settings.hotkey.volume_up"),
              QStringLiteral("G\u0142o\u015bno\u015b\u0107 w g\u00f3r\u0119:")},
             {QStringLiteral("settings.hotkey.volume_down"),
              QStringLiteral("G\u0142o\u015bno\u015b\u0107 w d\u00f3\u0142:")},
             {QStringLiteral("settings.hotkey.mute"), QStringLiteral("Wyciszenie:")},
             {QStringLiteral("settings.hotkey.show"),
              QStringLiteral("Poka\u017c g\u0142o\u015bno\u015b\u0107:")},
             {QStringLiteral("settings.hotkey.capture_title"), QStringLiteral("Przypisz klawisz")},
             {QStringLiteral("settings.hotkey.capture_prompt"),
              QStringLiteral("Naci\u015bnij klawisz, kt\u00f3ry chcesz przypisa\u0107.\nEsc lub "
                             "Anuluj, by przerwa\u0107.")},
             {QStringLiteral("settings.hotkey.capture_cancel"), QStringLiteral("Anuluj")},
             {QStringLiteral("settings.hotkey.unassign"), QStringLiteral("Wyczy\u015b\u0107")},
             // profiles
             {QStringLiteral("settings.profiles.section"), QStringLiteral("Profile")},
             {QStringLiteral("settings.profiles.add"), QStringLiteral("Dodaj")},
             {QStringLiteral("settings.profiles.remove"), QStringLiteral("Usu\u0144")},
             {QStringLiteral("settings.profiles.edit"), QStringLiteral("Edytuj")},
             {QStringLiteral("settings.profiles.set_default"),
              QStringLiteral("Ustaw jako domy\u015blny")},
             {QStringLiteral("settings.profiles.col_name"), QStringLiteral("Nazwa")},
             {QStringLiteral("settings.profiles.col_app"), QStringLiteral("Aplikacje")},
             {QStringLiteral("settings.profiles.col_modifiers"), QStringLiteral("Modyfikatory")},
             {QStringLiteral("settings.profiles.col_volume_up"),
              QStringLiteral("G\u0142o\u015bniej")},
             {QStringLiteral("settings.profiles.col_volume_down"), QStringLiteral("Ciszej")},
             {QStringLiteral("settings.profiles.col_mute"), QStringLiteral("Wycisz")},
             {QStringLiteral("settings.profiles.col_ducking"), QStringLiteral("Ducking")},
             {QStringLiteral("settings.profiles.col_sink"), QStringLiteral("Wyjście")},
             {QStringLiteral("settings.profiles.sink_label"),
              QStringLiteral("Urządzenie wyjściowe:")},
             {QStringLiteral("settings.profiles.sink_default"),
              QStringLiteral("(systemowe domyślne)")},
             {QStringLiteral("settings.profiles.sink_missing"), QStringLiteral("(niedostępne) %1")},
             {QStringLiteral("settings.profiles.modifier_ctrl"), QStringLiteral("Ctrl")},
             {QStringLiteral("settings.profiles.modifier_shift"), QStringLiteral("Shift")},
             {QStringLiteral("settings.profiles.modifier_none"), QStringLiteral("(brak)")},
             {QStringLiteral("settings.profiles.edit_title"), QStringLiteral("Edytuj profil")},
             {QStringLiteral("settings.profiles.name_label"), QStringLiteral("Nazwa:")},
             {QStringLiteral("settings.profiles.app_add"), QStringLiteral("Dodaj")},
             {QStringLiteral("settings.profiles.app_remove"), QStringLiteral("Usu\u0144")},
             {QStringLiteral("settings.profiles.app_up"),
              QStringLiteral("Przesu\u0144 w g\u00f3r\u0119")},
             {QStringLiteral("settings.profiles.app_down"),
              QStringLiteral("Przesu\u0144 w d\u00f3\u0142")},
             {QStringLiteral("settings.profiles.duplicate_title"),
              QStringLiteral("Duplikat aplikacji")},
             {QStringLiteral("settings.profiles.duplicate_msg"),
              QStringLiteral("Ta aplikacja jest ju\u017c w profilu.")},
             {QStringLiteral("settings.profiles.ducking_enabled"),
              QStringLiteral("\u015acisz inne aplikacje")},
             {QStringLiteral("settings.profiles.ducking_volume"),
              QStringLiteral("G\u0142o\u015bno\u015b\u0107 innych aplikacji:")},
             {QStringLiteral("settings.profiles.ducking_hotkey"),
              QStringLiteral("Skr\u00f3t trybu skupienia:")},
             {QStringLiteral("settings.profiles.cannot_remove_last"),
              QStringLiteral("Musi pozosta\u0107 co najmniej jeden profil.")},
             {QStringLiteral("settings.profiles.auto_switch"),
              QStringLiteral("Auto-prze\u0142\u0105czanie: reaguj na zmian\u0119 aktywnego okna")},
             {QStringLiteral("settings.profiles.vol_limits"),
              QStringLiteral("Limity g\u0142o\u015bno\u015bci:")},
             {QStringLiteral("settings.profiles.vol_min_label"), QStringLiteral("Min:")},
             {QStringLiteral("settings.profiles.vol_max_label"), QStringLiteral("Maks:")},
             // scenes
             {QStringLiteral("settings.scenes.section"), QStringLiteral("Sceny")},
             {QStringLiteral("settings.scenes.add"), QStringLiteral("Dodaj")},
             {QStringLiteral("settings.scenes.edit"), QStringLiteral("Edytuj")},
             {QStringLiteral("settings.scenes.remove"), QStringLiteral("Usu\u0144")},
             {QStringLiteral("settings.scenes.duplicate"), QStringLiteral("Duplikuj")},
             {QStringLiteral("settings.scenes.apply"), QStringLiteral("Zastosuj")},
             {QStringLiteral("settings.scenes.col_name"), QStringLiteral("Nazwa")},
             {QStringLiteral("settings.scenes.col_targets"), QStringLiteral("Cele")},
             {QStringLiteral("settings.scenes.col_hotkey"), QStringLiteral("Skr\u00f3t")},
             {QStringLiteral("settings.scenes.no_targets"), QStringLiteral("(brak cel\u00f3w)")},
             {QStringLiteral("settings.scenes.edit_title"), QStringLiteral("Edytuj scen\u0119")},
             {QStringLiteral("settings.scenes.name_label"), QStringLiteral("Nazwa:")},
             {QStringLiteral("settings.scenes.hotkey_label"), QStringLiteral("Skr\u00f3t:")},
             {QStringLiteral("settings.scenes.targets_label"), QStringLiteral("Cele")},
             {QStringLiteral("settings.scenes.target_add"), QStringLiteral("Dodaj")},
             {QStringLiteral("settings.scenes.target_edit"), QStringLiteral("Edytuj")},
             {QStringLiteral("settings.scenes.target_remove"), QStringLiteral("Usu\u0144")},
             {QStringLiteral("settings.scenes.target_title"), QStringLiteral("Cel sceny")},
             {QStringLiteral("settings.scenes.col_match"), QStringLiteral("Aplikacja")},
             {QStringLiteral("settings.scenes.col_volume"),
              QStringLiteral("G\u0142o\u015bno\u015b\u0107")},
             {QStringLiteral("settings.scenes.col_mute"), QStringLiteral("Wyciszenie")},
             {QStringLiteral("settings.scenes.match_hint"),
              QStringLiteral("Nazwa aplikacji lub pliku (np. Spotify)")},
             {QStringLiteral("settings.scenes.set_volume"),
              QStringLiteral("Ustaw g\u0142o\u015bno\u015b\u0107")},
             {QStringLiteral("settings.scenes.vol_leave"), QStringLiteral("bez zmian")},
             {QStringLiteral("settings.scenes.mute_leave"), QStringLiteral("Bez zmian")},
             {QStringLiteral("settings.scenes.mute_on"), QStringLiteral("Wycisz")},
             {QStringLiteral("settings.scenes.mute_off"),
              QStringLiteral("W\u0142\u0105cz d\u017awi\u0119k")},
             {QStringLiteral("settings.scenes.copy_suffix"), QStringLiteral(" (kopia)")},
             // auto profile switch
             {QStringLiteral("settings.auto_profile_switch"),
              QStringLiteral("Auto-prze\u0142\u0105czanie profilu wg aktywnego okna")},
             // media hotkeys (global, MPRIS)
             {QStringLiteral("settings.media.section"),
              QStringLiteral("Skr\u00f3ty multimedialne")},
             {QStringLiteral("settings.media.play_pause"), QStringLiteral("Odtw\u00f3rz / Pauza:")},
             {QStringLiteral("settings.media.next"), QStringLiteral("Nast\u0119pny utw\u00f3r:")},
             {QStringLiteral("settings.media.previous"), QStringLiteral("Poprzedni utw\u00f3r:")},
             {QStringLiteral("settings.media.stop"), QStringLiteral("Zatrzymaj:")},
             // progress / MPRIS
             {QStringLiteral("settings.progress.section"),
              QStringLiteral("Post\u0119p odtwarzania")},
             {QStringLiteral("settings.progress.enabled"),
              QStringLiteral("Pokazuj pasek post\u0119pu odtwarzania")},
             {QStringLiteral("settings.progress.interactive"),
              QStringLiteral("Zezwalaj na przewijanie klikni\u0119ciem paska")},
             {QStringLiteral("settings.progress.poll_ms"),
              QStringLiteral("Cz\u0119stotliwo\u015b\u0107 odpytywania pozycji:")},
             {QStringLiteral("settings.progress.label_mode"),
              QStringLiteral("Etykieta \u015bcie\u017cki:")},
             {QStringLiteral("settings.progress.label_app"), QStringLiteral("Nazwa aplikacji")},
             {QStringLiteral("settings.progress.label_title_artist"),
              QStringLiteral("Tytu\u0142 \u2014 Wykonawca")},
             {QStringLiteral("settings.progress.label_artist_title"),
              QStringLiteral("Wykonawca \u2014 Tytu\u0142")},
             {QStringLiteral("settings.progress.label_app_track"),
              QStringLiteral("Aplikacja + Tytu\u0142 \u2014 Wykonawca")},
             {QStringLiteral("settings.progress.label_player_track"),
              QStringLiteral("Player + Tytu\u0142 \u2014 Wykonawca")},
             {QStringLiteral("settings.progress.label_player_track_art"),
              QStringLiteral("Player + Tytu\u0142 \u2014 Wykonawca + Ok\u0142adka")},
             {QStringLiteral("settings.progress.label_custom"),
              QStringLiteral("W\u0142asny\u2026")},
             {QStringLiteral("settings.progress.custom_top"), QStringLiteral("G\u00f3rna linia:")},
             {QStringLiteral("settings.progress.custom_bottom"), QStringLiteral("Dolna linia:")},
             {QStringLiteral("settings.progress.custom_show_art"),
              QStringLiteral("Poka\u017c ok\u0142adk\u0119")},
             {QStringLiteral("settings.progress.custom_tokens_hint"),
              QStringLiteral("Tokeny: {app}, {player}, {title}, {artist}, {album}. "
                             "Pusta linia = ukryta.")},
             {QStringLiteral("settings.progress.tracked_players"),
              QStringLiteral("Obserwowane odtwarzacze (kolejno\u015b\u0107 priorytetu):")},
             {QStringLiteral("settings.progress.tracked_players_hint"),
              QStringLiteral("Podci\u0105gi oddzielone przecinkami, np. spotify, youtube")},
             {QStringLiteral("settings.progress.media_controls"),
              QStringLiteral(
                  "Poka\u017c przyciski sterowania (play/pause/nast\u0119pny/poprzedni)")},
             {QStringLiteral("settings.progress.expose_mpris"),
              QStringLiteral("Eksponuj fa\u0142szywy endpoint MPRIS (mo\u017ce kolidowa\u0107 z "
                             "Discord Music Presence i podobnymi aplikacjami)")},
             // osd
             {QStringLiteral("osd.preview"), QStringLiteral("Podgl\u0105d OSD")},
             {QStringLiteral("settings.osd_scale"), QStringLiteral("Skala OSD:")},
             // app selector
             {QStringLiteral("app_selector.title"), QStringLiteral("Domy\u015blna aplikacja")},
             {QStringLiteral("app_selector.subtitle"),
              QStringLiteral("Wybierz aplikacj\u0119 audio, kt\u00f3ra otrzymuje zdarzenia "
                             "klawiszy g\u0142o\u015bno\u015bci.")},
             {QStringLiteral("app_selector.no_default"),
              QStringLiteral("Bez domy\u015blnej aplikacji")},
             {QStringLiteral("app_selector.no_apps"),
              QStringLiteral("Nie znaleziono aplikacji audio")},
             {QStringLiteral("app_selector.refresh"), QStringLiteral("Od\u015bwie\u017c")},
             // wizard
             {QStringLiteral("wizard.welcome_title"), QStringLiteral("Witamy")},
             {QStringLiteral("wizard.welcome_text"),
              QStringLiteral(
                  "Ta aplikacja przechwytuje sprz\u0119towe klawisze g\u0142o\u015bno\u015bci\n"
                  "i kieruje je do jednej, wybranej aplikacji audio, zamiast zmienia\u0107 "
                  "g\u0142o\u015bno\u015b\u0107 systemow\u0105.\n\n"
                  "Skonfigurujmy podstawowe ustawienia. Wszystko mo\u017cna p\u00f3\u017aniej "
                  "zmieni\u0107 w Ustawieniach.")},
             {QStringLiteral("wizard.lang_label"), QStringLiteral("J\u0119zyk:")},
             {QStringLiteral("wizard.device_title"),
              QStringLiteral("Urz\u0105dzenie wej\u015bciowe")},
             {QStringLiteral("wizard.app_title"), QStringLiteral("Domy\u015blna aplikacja")},
             {QStringLiteral("wizard.app_subtitle"),
              QStringLiteral("Wybierz aplikacj\u0119 audio, kt\u00f3ra otrzymuje zdarzenia "
                             "klawiszy g\u0142o\u015bno\u015bci.")},
             {QStringLiteral("wizard.app_empty"), QStringLiteral("Bez domy\u015blnej aplikacji")},
             {QStringLiteral("wizard.app_no_apps"),
              QStringLiteral("Nie znaleziono aplikacji audio")},
             {QStringLiteral("wizard.app_refresh"), QStringLiteral("Od\u015bwie\u017c")},
             // warnings
             {QStringLiteral("warn.no_device.title"), QStringLiteral("Brak urz\u0105dzenia")},
             {QStringLiteral("warn.no_device.text"),
              QStringLiteral("Nie wybrano urz\u0105dzenia wej\u015bciowego.\n"
                             "Mo\u017cesz wybra\u0107 je p\u00f3\u017aniej z menu tray \u2192 "
                             "\"Zmie\u0144 urz\u0105dzenie wej\u015bciowe...\"")},
         }},
    };
    return s;
}

// ─── State ────────────────────────────────────────────────────────────────────
static QString s_current = QStringLiteral("en");

// ─── Public API ───────────────────────────────────────────────────────────────
const QMap<QString, QString>& languages()
{
    static const QMap<QString, QString> m{
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("pl"), QStringLiteral("Polski")},
    };
    return m;
}

void setLanguage(const QString& code)
{
    if (strings().contains(code)) s_current = code;
}

QString currentLanguage()
{
    return s_current;
}

QString tr(const QString& key)
{
    const auto& table = strings();
    auto langIt = table.find(s_current);
    if (langIt != table.end())
    {
        auto keyIt = langIt->find(key);
        if (keyIt != langIt->end()) return keyIt.value();
    }
    // Fallback to English
    auto enIt = table.find(QStringLiteral("en"));
    if (enIt != table.end())
    {
        auto keyIt = enIt->find(key);
        if (keyIt != enIt->end()) return keyIt.value();
    }
    return key;
}
