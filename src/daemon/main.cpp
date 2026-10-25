#include "clirunner.h"
#include "daemon1adaptor.h"
#include "daemonservice.h"
#include "dbustypes.h"
#include "polkitauthorizer.h"
#include "vpnservice.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDir>
#include <QFile>
#include <QSocketNotifier>

#include <csignal>
#include <sys/signalfd.h>
#include <unistd.h>

using namespace agv;

namespace {

QString defaultDataDir()
{
    // systemd sets STATE_DIRECTORY from StateDirectory=agvd.
    const QString fromSystemd = qEnvironmentVariable("STATE_DIRECTORY");
    if (!fromSystemd.isEmpty())
        return fromSystemd.split(QLatin1Char(':')).first();
    return QStringLiteral("/var/lib/agvd");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("agvd"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AGV_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AdGuard VPN GUI daemon"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption sessionOption(
        QStringLiteral("session"),
        QStringLiteral("Use the session bus and skip polkit checks (development only)."));
    const QCommandLineOption dataDirOption(
        QStringLiteral("data-dir"),
        QStringLiteral("HOME used for adguardvpn-cli (default: $STATE_DIRECTORY or /var/lib/agvd)."),
        QStringLiteral("dir"));
    const QCommandLineOption cliOption(QStringLiteral("cli"),
                                       QStringLiteral("Path to adguardvpn-cli."),
                                       QStringLiteral("path"));
    parser.addOptions({sessionOption, dataDirOption, cliOption});
    parser.process(app);

    const bool session = parser.isSet(sessionOption);
    if (!session && geteuid() != 0)
        qWarning("agvd: not running as root; connecting and routing will fail");

    const QString dataDir = parser.isSet(dataDirOption) ? parser.value(dataDirOption) : defaultDataDir();
    if (!QDir().mkpath(dataDir)) {
        qCritical("agvd: cannot create data directory %s", qPrintable(dataDir));
        return 1;
    }
    QFile::setPermissions(dataDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const QString cliPath = parser.isSet(cliOption) ? parser.value(cliOption) : CliRunner::findProgram();
    if (cliPath.isEmpty())
        qWarning("agvd: adguardvpn-cli not found; every call will fail with CliNotFound");

    // Let systemd's SIGTERM run destructors instead of killing mid-command. signalfd keeps the
    // handling on the event loop, so nothing unsafe runs in signal context.
    sigset_t stopSignals;
    sigemptyset(&stopSignals);
    sigaddset(&stopSignals, SIGTERM);
    sigaddset(&stopSignals, SIGINT);
    sigprocmask(SIG_BLOCK, &stopSignals, nullptr);
    const int signalFd = signalfd(-1, &stopSignals, SFD_CLOEXEC | SFD_NONBLOCK);
    QSocketNotifier signalNotifier(signalFd, QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app, [signalFd] {
        signalfd_siginfo info;
        while (read(signalFd, &info, sizeof(info)) == sizeof(info)) {
        }
        QCoreApplication::quit();
    });

    registerDBusTypes();
    QDBusConnection bus = session ? QDBusConnection::sessionBus() : QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        qCritical("agvd: cannot connect to the D-Bus %s bus", session ? "session" : "system");
        return 1;
    }

    CliRunner cli(cliPath, dataDir);
    VpnService vpn(&cli);
    PolkitAuthorizer polkit(bus, !session);
    DaemonService service(&vpn, &polkit, bus);
    new Daemon1Adaptor(&service);

    if (!bus.registerObject(DBusPath, &service)) {
        qCritical("agvd: cannot register %s: %s", qPrintable(DBusPath),
                  qPrintable(bus.lastError().message()));
        return 1;
    }
    if (!bus.registerService(DBusService)) {
        qCritical("agvd: cannot own %s: %s", qPrintable(DBusService),
                  qPrintable(bus.lastError().message()));
        return 1;
    }

    vpn.start();
    return app.exec();
}
