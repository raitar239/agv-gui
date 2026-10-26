#include "appexclusionservice.h"
#include "appstore.h"
#include "clirunner.h"
#include "daemon1adaptor.h"
#include "daemonservice.h"
#include "daemonsettings.h"
#include "execwatcher.h"
#include "dnsguard.h"
#include "killswitch.h"
#include "dbustypes.h"
#include "polkitauthorizer.h"
#include "routingengine.h"
#include "runsocketserver.h"
#include "statsrecorder.h"
#include "systemdunit.h"
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

QString defaultRuntimeDir(bool session)
{
    // systemd sets RUNTIME_DIRECTORY from RuntimeDirectory=agvd.
    const QString fromSystemd = qEnvironmentVariable("RUNTIME_DIRECTORY");
    if (!fromSystemd.isEmpty())
        return fromSystemd.split(QLatin1Char(':')).first();
    if (session && !qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR"))
        return qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/agvd");
    return QStringLiteral("/run/agvd");
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
    const QCommandLineOption runtimeDirOption(
        QStringLiteral("runtime-dir"),
        QStringLiteral("Directory for the agv-run socket (default: $RUNTIME_DIRECTORY or /run/agvd)."),
        QStringLiteral("dir"));
    const QCommandLineOption cleanupOption(
        QStringLiteral("cleanup"),
        QStringLiteral("Remove all routing state for app exclusions (nftables, ip rules, cgroups) and exit."));
    parser.addOptions({sessionOption, dataDirOption, cliOption, runtimeDirOption, cleanupOption});
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

    ProcessCommands commands;
    RoutingPaths routingPaths;
    routingPaths.stateFile = dataDir + QStringLiteral("/routing-state");
    RoutingEngine routing(&commands, routingPaths);

    if (parser.isSet(cleanupOption)) {
        // Run from the uninstall path after agvd has stopped; init() only finds the cgroup.
        routing.init();
        routing.teardown();
        KillSwitch(&commands).release();
        DnsGuard(&commands).release();
        return 0;
    }

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
    AppStore appStore(dataDir + QStringLiteral("/app-exclusions.json"));
    AppExclusionService apps(&appStore, &routing);
    PolkitAuthorizer polkit(bus, !session);
    DaemonService service(&vpn, &apps, &polkit, bus);

    DaemonSettings settings(dataDir + QStringLiteral("/settings.json"));
    settings.load();
    vpn.setAutoConnect(settings.autoConnect());
    // systemd lives on the system bus even when agvd itself runs on the session bus for development.
    SystemdUnit unit(QDBusConnection::systemBus(), QStringLiteral("agvd.service"));
    service.setStartup(&settings, session ? nullptr : &unit);
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

    apps.start();

    // Catch excluded apps however they are started (menu, terminal, Lutris, Steam…).
    ExecWatcher execWatcher;
    if (apps.isSupported()) {
        if (execWatcher.start()) {
            QObject::connect(&execWatcher, &ExecWatcher::executed, &apps, &AppExclusionService::onExecuted);
            QObject::connect(&execWatcher, &ExecWatcher::overflowed, &apps, &AppExclusionService::scanRunning);
        } else {
            qWarning("agvd: exec watcher unavailable (%s); apps are excluded only through agv-run",
                     qPrintable(execWatcher.errorString()));
        }
    }

    const QString runtimeDir = parser.isSet(runtimeDirOption) ? parser.value(runtimeDirOption)
                                                              : defaultRuntimeDir(session);
    QDir().mkpath(runtimeDir);
    RunSocketServer runSocket(&apps, runtimeDir + QStringLiteral("/run.sock"));
    if (!runSocket.listen())
        qWarning("agvd: agv-run socket unavailable: %s", qPrintable(runSocket.errorString()));

    // Statistics page: counters every few seconds, and right away on every state change so
    // sessions start and end on time.
    StatsRecorder stats(dataDir + QStringLiteral("/stats.json"));
    if (const QString error = stats.load(); !error.isEmpty())
        qWarning("agvd: %s; statistics start over", qPrintable(error));
    stats.start([&vpn, &apps] {
        StatsRecorder::Sample sample;
        sample.time = QDateTime::currentDateTimeUtc();
        sample.connected = vpn.state() == VpnService::State::Connected;
        sample.location = vpn.location();
        sample.tunDevice = vpn.tunDevice();
        // SOCKS5 mode has no tunnel: connected time still counts, traffic does not.
        if (sample.connected && !sample.tunDevice.isEmpty() && !sample.tunDevice.contains(QLatin1Char('/'))) {
            const auto read = [&sample](const char *counter) -> std::optional<quint64> {
                QFile file(QStringLiteral("/sys/class/net/%1/statistics/%2").arg(sample.tunDevice, QLatin1String(counter)));
                bool ok = false;
                const quint64 value = file.open(QIODevice::ReadOnly) ? file.readAll().trimmed().toULongLong(&ok) : 0;
                return ok ? std::optional(value) : std::nullopt;
            };
            const auto in = read("rx_bytes");
            const auto out = read("tx_bytes");
            if (in && out)
                sample.tunnel = StatsRecorder::Bytes{*in, *out};
        }
        if (apps.isSupported()) {
            const auto traffic = apps.traffic();
            for (auto it = traffic.cbegin(); it != traffic.cend(); ++it)
                sample.apps.insert(it.key(), {it->bytesIn, it->bytesOut});
            for (const AppRule &rule : apps.rules())
                sample.appInfo.insert(rule.id, {rule.name, rule.icon, rule.ownerUid});
            sample.appInfo.insert(RoutingEngine::BuiltinRunId, {QStringLiteral("agv-run"), QString(), -1});
        }
        return sample;
    });
    QObject::connect(&vpn, &VpnService::statusChanged, &stats, &StatsRecorder::sampleNow);
    service.setStatistics(&stats);

    // A table left by a crashed agvd goes; it comes back below if the VPN is still meant to be up.
    KillSwitch killSwitch(&commands);
    killSwitch.release();
    DnsGuard dnsGuard(&commands);
    dnsGuard.release();
    if (apps.isSupported())
        service.setProtection(&killSwitch, &dnsGuard, &routing);

    vpn.start();
    const int code = app.exec();
    // Stopping agvd must never leave the computer cut off.
    killSwitch.release();
    dnsGuard.release();
    return code;
}
