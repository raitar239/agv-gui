#include "autostart.h"
#include "daemonclient.h"
#include "installedapps.h"
#include "language.h"
#include "locationmodel.h"
#include "systemtheme.h"
#include "trayicon.h"
#include "windowcontroller.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDir>
#include <QIcon>
#include <QSettings>
#include <QQmlEngine>
#include <QQuickStyle>

using namespace agv;

namespace {

// Outside a full desktop (Hyprland, sway…) Qt often knows no icon theme; GTK's setting usually
// names the one the user picked.
void pickIconTheme()
{
    const QString current = QIcon::themeName();
    if (!current.isEmpty() && current != QLatin1String("hicolor"))
        return;
    for (const QString &file : {QDir::homePath() + QStringLiteral("/.config/gtk-4.0/settings.ini"),
                                QDir::homePath() + QStringLiteral("/.config/gtk-3.0/settings.ini")}) {
        const QString theme = QSettings(file, QSettings::IniFormat).value(QStringLiteral("Settings/gtk-icon-theme-name")).toString();
        if (!theme.isEmpty()) {
            QIcon::setThemeName(theme);
            return;
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // QApplication only for the tray (QSystemTrayIcon and its menu are widgets).
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("agv-gui"));
    QCoreApplication::setApplicationName(QStringLiteral("agv-gui"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AGV_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("AGV"));
    QGuiApplication::setDesktopFileName(QStringLiteral("agv-gui"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AGV: VPN connection with per-app exclusions"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption hiddenOption(QStringLiteral("hidden"),
                                          QStringLiteral("Start in the tray without opening the window."));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Development: render the window into a PNG and exit."), QStringLiteral("file"));
    const QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Screenshot size, WxH."),
                                        QStringLiteral("size"), QStringLiteral("900x640"));
    const QCommandLineOption pageOption(QStringLiteral("page"), QStringLiteral("Screenshot page index."),
                                        QStringLiteral("index"), QStringLiteral("0"));
    parser.addOptions({hiddenOption, screenshotOption, sizeOption, pageOption});
    parser.process(app);

    const bool screenshot = parser.isSet(screenshotOption);
    SingleInstance instance;
    if (!screenshot && !instance.claim())
        return 0;

    // UI language: the system's, or the one picked in Settings. Before anything shows text.
    Language language;

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    pickIconTheme();
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));

    // AGV_SESSION_BUS=1 talks to "agvd --session" during development.
    const bool sessionBus = qEnvironmentVariableIntValue("AGV_SESSION_BUS") == 1;
    DaemonClient daemon(sessionBus ? QDBusConnection::sessionBus() : QDBusConnection::systemBus());
    SystemTheme theme;
    Autostart autostart;
    qmlRegisterSingletonInstance("Agv.Backend", 1, 0, "Daemon", &daemon);
    qmlRegisterSingletonInstance("Agv.Backend", 1, 0, "SystemTheme", &theme);
    qmlRegisterSingletonInstance("Agv.Backend", 1, 0, "Autostart", &autostart);
    qmlRegisterSingletonInstance("Agv.Backend", 1, 0, "Language", &language);
    qmlRegisterType<InstalledApps>("Agv.Backend", 1, 0, "InstalledApps");

    const bool tray = !screenshot && TrayIcon::isAvailable();
    // Without a tray, closing the window is the only way out, so it quits.
    QApplication::setQuitOnLastWindowClosed(false);
    WindowController window(!tray);

    TrayIcon *trayIcon = tray ? new TrayIcon(&daemon, &app) : nullptr;
    if (trayIcon) {
        QObject::connect(trayIcon, &TrayIcon::showWindowRequested, &window, &WindowController::show);
        QObject::connect(trayIcon, &TrayIcon::quitRequested, &app, &QCoreApplication::quit);
    }
    QObject::connect(&instance, &SingleInstance::showRequested, &window, &WindowController::show);
    QObject::connect(&language, &Language::changed, &app, [&] {
        daemon.locationModel()->reloadNames();
        window.retranslate();
        if (trayIcon)
            trayIcon->retranslate();
    });

    if (screenshot) {
        const QStringList size = parser.value(sizeOption).split(QLatin1Char('x'));
        window.screenshot(parser.value(screenshotOption),
                          QSize(size.value(0).toInt(), size.value(1).toInt()),
                          parser.value(pageOption).toInt(), qEnvironmentVariableIntValue("AGV_SCREENSHOT_DELAY") > 0 ? qEnvironmentVariableIntValue("AGV_SCREENSHOT_DELAY") : 2500);
    } else if (!parser.isSet(hiddenOption) || !tray) {
        window.show();
    }

    return app.exec();
}
