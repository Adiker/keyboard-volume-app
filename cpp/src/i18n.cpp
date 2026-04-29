#include "i18n.h"

// ─── String tables ─────────────────────────────────────────────────────────────
static const QMap<QString, QMap<QString, QString>> &strings()
{
    static const QMap<QString, QMap<QString, QString>> s {
        { QStringLiteral("en"), {
            // tray
            { QStringLiteral("tray.section.audio_app"),    QStringLiteral("Audio application") },
            { QStringLiteral("tray.action.refresh"),       QStringLiteral("Refresh app list") },
            { QStringLiteral("tray.action.change_device"), QStringLiteral("Change input device...") },
            { QStringLiteral("tray.action.settings"),      QStringLiteral("Settings...") },
            { QStringLiteral("tray.action.quit"),          QStringLiteral("Quit") },
            // device selector
            { QStringLiteral("device.title"),              QStringLiteral("Select input device") },
            { QStringLiteral("device.title.first_run"),    QStringLiteral("Select input device (first launch)") },
            { QStringLiteral("device.label"),              QStringLiteral("Devices with volume keys (Volume Up / Volume Down):") },
            { QStringLiteral("device.btn.refresh"),        QStringLiteral("Refresh") },
            { QStringLiteral("device.no_devices"),         QStringLiteral("No compatible devices found") },
            // settings
            { QStringLiteral("settings.title"),                    QStringLiteral("Settings") },
            { QStringLiteral("settings.osd_timeout"),              QStringLiteral("OSD timeout:") },
            { QStringLiteral("settings.osd_position"),             QStringLiteral("OSD position:") },
            { QStringLiteral("settings.volume_step"),              QStringLiteral("Volume step:") },
            { QStringLiteral("settings.color_bg"),                 QStringLiteral("Background color:") },
            { QStringLiteral("settings.color_text"),               QStringLiteral("Text color:") },
            { QStringLiteral("settings.color_bar"),                QStringLiteral("Bar color:") },
            { QStringLiteral("settings.opacity"),                  QStringLiteral("Opacity:") },
            { QStringLiteral("settings.preview_btn"),              QStringLiteral("Preview OSD") },
            { QStringLiteral("settings.language"),                 QStringLiteral("Language:") },
            { QStringLiteral("settings.osd_screen"),               QStringLiteral("OSD screen:") },
            { QStringLiteral("settings.screen_primary"),           QStringLiteral("primary") },
            { QStringLiteral("settings.hotkeys_section"),          QStringLiteral("Hotkeys") },
            { QStringLiteral("settings.hotkey.volume_up"),         QStringLiteral("Volume up:") },
            { QStringLiteral("settings.hotkey.volume_down"),       QStringLiteral("Volume down:") },
            { QStringLiteral("settings.hotkey.mute"),              QStringLiteral("Mute:") },
            { QStringLiteral("settings.hotkey.capture_title"),     QStringLiteral("Capture key") },
            { QStringLiteral("settings.hotkey.capture_prompt"),    QStringLiteral("Press the key you want to bind.\nEsc or Cancel to abort.") },
            { QStringLiteral("settings.hotkey.capture_cancel"),    QStringLiteral("Cancel") },
            // osd
            { QStringLiteral("osd.preview"),                       QStringLiteral("OSD Preview") },
            // wizard
            { QStringLiteral("wizard.welcome_title"), QStringLiteral("Welcome") },
            { QStringLiteral("wizard.welcome_text"),  QStringLiteral(
                "This application intercepts keyboard volume keys at the hardware level\n"
                "and routes them to a single audio application instead of the system master volume.\n\n"
                "Let's quickly configure the basics. You can change everything later in Settings.") },
            { QStringLiteral("wizard.lang_label"),    QStringLiteral("Language:") },
            { QStringLiteral("wizard.device_title"),  QStringLiteral("Input device") },
            { QStringLiteral("wizard.app_title"),     QStringLiteral("Default application") },
            { QStringLiteral("wizard.app_subtitle"),  QStringLiteral("Select which audio application receives volume key events.") },
            { QStringLiteral("wizard.app_empty"),     QStringLiteral("No default application") },
            { QStringLiteral("wizard.app_no_apps"),   QStringLiteral("No audio applications found") },
            { QStringLiteral("wizard.app_refresh"),   QStringLiteral("Refresh") },
            // warnings
            { QStringLiteral("warn.no_device.title"), QStringLiteral("No device selected") },
            { QStringLiteral("warn.no_device.text"),  QStringLiteral(
                "No input device selected.\n"
                "You can select one later from tray menu \u2192 \"Change input device...\"") },
        }},
        { QStringLiteral("pl"), {
            // tray
            { QStringLiteral("tray.section.audio_app"),    QStringLiteral("Aplikacja audio") },
            { QStringLiteral("tray.action.refresh"),       QStringLiteral("Od\u015bwie\u017c list\u0119 aplikacji") },
            { QStringLiteral("tray.action.change_device"), QStringLiteral("Zmie\u0144 urz\u0105dzenie wej\u015bciowe...") },
            { QStringLiteral("tray.action.settings"),      QStringLiteral("Ustawienia...") },
            { QStringLiteral("tray.action.quit"),          QStringLiteral("Wyj\u015bcie") },
            // device selector
            { QStringLiteral("device.title"),              QStringLiteral("Wybierz urz\u0105dzenie wej\u015bciowe") },
            { QStringLiteral("device.title.first_run"),    QStringLiteral("Wybierz urz\u0105dzenie wej\u015bciowe (pierwsze uruchomienie)") },
            { QStringLiteral("device.label"),              QStringLiteral("Urz\u0105dzenia z klawiszami g\u0142o\u015bno\u015bci (Volume Up / Volume Down):") },
            { QStringLiteral("device.btn.refresh"),        QStringLiteral("Od\u015bwie\u017c") },
            { QStringLiteral("device.no_devices"),         QStringLiteral("Brak dost\u0119pnych urz\u0105dze\u0144") },
            // settings
            { QStringLiteral("settings.title"),                    QStringLiteral("Ustawienia") },
            { QStringLiteral("settings.osd_timeout"),              QStringLiteral("Czas OSD:") },
            { QStringLiteral("settings.osd_position"),             QStringLiteral("Pozycja OSD:") },
            { QStringLiteral("settings.volume_step"),              QStringLiteral("Krok g\u0142o\u015bno\u015bci:") },
            { QStringLiteral("settings.color_bg"),                 QStringLiteral("Kolor t\u0142a:") },
            { QStringLiteral("settings.color_text"),               QStringLiteral("Kolor tekstu:") },
            { QStringLiteral("settings.color_bar"),                QStringLiteral("Kolor paska:") },
            { QStringLiteral("settings.opacity"),                  QStringLiteral("Przezroczysto\u015b\u0107:") },
            { QStringLiteral("settings.preview_btn"),              QStringLiteral("Podgl\u0105d OSD") },
            { QStringLiteral("settings.language"),                 QStringLiteral("J\u0119zyk:") },
            { QStringLiteral("settings.osd_screen"),               QStringLiteral("Ekran OSD:") },
            { QStringLiteral("settings.screen_primary"),           QStringLiteral("g\u0142\u00f3wny") },
            { QStringLiteral("settings.hotkeys_section"),          QStringLiteral("Skr\u00f3ty klawiszowe") },
            { QStringLiteral("settings.hotkey.volume_up"),         QStringLiteral("G\u0142o\u015bno\u015b\u0107 w g\u00f3r\u0119:") },
            { QStringLiteral("settings.hotkey.volume_down"),       QStringLiteral("G\u0142o\u015bno\u015b\u0107 w d\u00f3\u0142:") },
            { QStringLiteral("settings.hotkey.mute"),              QStringLiteral("Wyciszenie:") },
            { QStringLiteral("settings.hotkey.capture_title"),     QStringLiteral("Przypisz klawisz") },
            { QStringLiteral("settings.hotkey.capture_prompt"),    QStringLiteral("Naci\u015bnij klawisz, kt\u00f3ry chcesz przypisa\u0107.\nEsc lub Anuluj, by przerwa\u0107.") },
            { QStringLiteral("settings.hotkey.capture_cancel"),    QStringLiteral("Anuluj") },
            // osd
            { QStringLiteral("osd.preview"),                       QStringLiteral("Podgl\u0105d OSD") },
            // wizard
            { QStringLiteral("wizard.welcome_title"), QStringLiteral("Witamy") },
            { QStringLiteral("wizard.welcome_text"),  QStringLiteral(
                "Ta aplikacja przechwytuje sprz\u0119towe klawisze g\u0142o\u015bno\u015bci\n"
                "i kieruje je do jednej, wybranej aplikacji audio, zamiast zmienia\u0107 g\u0142o\u015bno\u015b\u0107 systemow\u0105.\n\n"
                "Skonfigurujmy podstawowe ustawienia. Wszystko mo\u017cna p\u00f3\u017aniej zmieni\u0107 w Ustawieniach.") },
            { QStringLiteral("wizard.lang_label"),    QStringLiteral("J\u0119zyk:") },
            { QStringLiteral("wizard.device_title"),  QStringLiteral("Urz\u0105dzenie wej\u015bciowe") },
            { QStringLiteral("wizard.app_title"),     QStringLiteral("Domy\u015blna aplikacja") },
            { QStringLiteral("wizard.app_subtitle"),  QStringLiteral("Wybierz aplikacj\u0119 audio, kt\u00f3ra otrzymuje zdarzenia klawiszy g\u0142o\u015bno\u015bci.") },
            { QStringLiteral("wizard.app_empty"),     QStringLiteral("Bez domy\u015blnej aplikacji") },
            { QStringLiteral("wizard.app_no_apps"),   QStringLiteral("Nie znaleziono aplikacji audio") },
            { QStringLiteral("wizard.app_refresh"),   QStringLiteral("Od\u015bwie\u017c") },
            // warnings
            { QStringLiteral("warn.no_device.title"), QStringLiteral("Brak urz\u0105dzenia") },
            { QStringLiteral("warn.no_device.text"),  QStringLiteral(
                "Nie wybrano urz\u0105dzenia wej\u015bciowego.\n"
                "Mo\u017cesz wybra\u0107 je p\u00f3\u017aniej z menu tray \u2192 \"Zmie\u0144 urz\u0105dzenie wej\u015bciowe...\"") },
        }},
    };
    return s;
}

// ─── State ────────────────────────────────────────────────────────────────────
static QString s_current = QStringLiteral("en");

// ─── Public API ───────────────────────────────────────────────────────────────
const QMap<QString, QString> &languages()
{
    static const QMap<QString, QString> m {
        { QStringLiteral("en"), QStringLiteral("English") },
        { QStringLiteral("pl"), QStringLiteral("Polski")  },
    };
    return m;
}

void setLanguage(const QString &code)
{
    if (strings().contains(code))
        s_current = code;
}

QString currentLanguage()
{
    return s_current;
}

QString tr(const QString &key)
{
    const auto &table = strings();
    auto langIt = table.find(s_current);
    if (langIt != table.end()) {
        auto keyIt = langIt->find(key);
        if (keyIt != langIt->end())
            return keyIt.value();
    }
    // Fallback to English
    auto enIt = table.find(QStringLiteral("en"));
    if (enIt != table.end()) {
        auto keyIt = enIt->find(key);
        if (keyIt != enIt->end())
            return keyIt.value();
    }
    return key;
}
