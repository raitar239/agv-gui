#include "daemonclient.h"
#include "systemtheme.h"
#include "trayicon.h"
#include "windowcontroller.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QIcon>
#include <QQmlEngine>
#include <QQuickStyle>

using namespace agv;

int main(int argc, char *argv[])
{
    // QApplication only for the tray (QSystemTrayIcon and its menu are widgets).
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("agv-gui"));
    QCoreApplication::setApplicationName(QStringLiteral("agv-gui"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AGV_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("AdGuard VPN"));
    QGuiApplication::setDesktopFileName(QStringLiteral("agv-gui"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AdGuard VPN client for Linux"));
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

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // AGV_SESSION_BUS=1 talks to "agvd --session" during development.
    const bool sessionBus = qEnvironmentVariableIntValue("AGV_SESSION_BUS") == 1;
    DaemonClient daemon(sessionBus ? QDBusConnection::sessionBus() : QDBusConnection::systemBus());
    SystemTheme theme;
    qmlRegisterSingletonInstance("Agv.Backend", 1, 0, "Daemon", &daemon);
    qmlRegisterSingletonInstance("Agv.Backend", 1, 0, "SystemTheme", &theme);

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
