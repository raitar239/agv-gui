#include "clirunner.h"
#include "hostsystem.h"
#include "vpnservice.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace agv;

namespace {

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

// Adds a process to a fake /proc.
void addProcess(const QString &procDir, int pid, const QList<QByteArray> &argv, const QByteArray &home,
                uid_t uid = 0)
{
    const QString dir = procDir + QLatin1Char('/') + QString::number(pid);
    QByteArray cmdline;
    for (const QByteArray &arg : argv)
        cmdline += arg + '\0';
    writeFile(dir + QStringLiteral("/cmdline"), cmdline);
    writeFile(dir + QStringLiteral("/environ"),
              home.isEmpty() ? QByteArray() : QByteArray("PATH=/usr/bin\0HOME=", 19) + home + '\0');
    writeFile(dir + QStringLiteral("/status"),
              "Name:\tadguardvpn-cli\nUid:\t" + QByteArray::number(uid) + "\t0\t0\t0\n");
}

} // namespace

class TestHostSystem : public QObject
{
    Q_OBJECT

private slots:
    void findsOnlyForeignConnectProcesses()
    {
        QTemporaryDir proc;
        const QString ours = QStringLiteral("/var/lib/agvd");
        addProcess(proc.path(), 100, {"adguardvpn-cli", "connect", "--no-fork"}, "/root", 0);
        addProcess(proc.path(), 101, {"/usr/local/bin/adguardvpn-cli", "connect", "--no-fork"}, "/home/alice", 1000);
        addProcess(proc.path(), 102, {"adguardvpn-cli", "connect", "--no-fork"}, ours.toUtf8(), 0);
        addProcess(proc.path(), 103, {"adguardvpn-cli", "status"}, "/root", 0);
        addProcess(proc.path(), 104, {"bash", "-c", "adguardvpn-cli connect"}, "/root", 0);
        addProcess(proc.path(), 105, {"adguardvpn-cli", "connect"}, QByteArray(), 0); // environ unreadable
        QDir(proc.path()).mkpath(QStringLiteral("self"));

        QList<ExternalVpn> found = findExternalVpns(ours, proc.path());
        std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.pid < b.pid; });

        QCOMPARE(found.size(), 3);
        QCOMPARE(found[0], (ExternalVpn{100, 0, QStringLiteral("/root")}));
        QCOMPARE(found[1], (ExternalVpn{101, 1000, QStringLiteral("/home/alice")}));
        QCOMPARE(found[2], (ExternalVpn{105, 0, QString()}));

        QVERIFY(isVpnProcess(100, proc.path()));
        QVERIFY(!isVpnProcess(103, proc.path()));
        QVERIFY(!isVpnProcess(999, proc.path()));
    }

    void serviceReportsExternalVpnOnlyWhileDisconnected()
    {
        QTemporaryDir proc;
        QTemporaryDir home;
        addProcess(proc.path(), 200, {"adguardvpn-cli", "connect", "--no-fork"}, "/root", 0);

        QDir(home.path()).mkpath(QStringLiteral("responses"));
        auto respond = [&](const QByteArray &status) {
            writeFile(home.path() + QStringLiteral("/responses/status.out"), status);
            writeFile(home.path() + QStringLiteral("/responses/status.code"), "0");
        };

        CliRunner cli(QStringLiteral(AGV_FAKE_CLI), home.path());
        VpnService vpn(&cli);
        vpn.setProcDir(proc.path());

        respond("VPN is disconnected\n");
        vpn.refreshStatus();
        QTRY_COMPARE(vpn.externalVpns().size(), 1);
        QCOMPARE(vpn.externalVpns().first().pid, pid_t(200));

        respond("Connected to ATHENS in TUN mode, running on tun0\n");
        vpn.refreshStatus();
        QTRY_VERIFY(vpn.externalVpns().isEmpty());
    }

    void importCopiesConfigWithPrivatePermissions()
    {
        QTemporaryDir from;
        QTemporaryDir to;
        writeFile(cliConfigPath(from.path()), "encrypted-settings");

        QCOMPARE(importCliConfig(from.path(), getuid(), to.path()), QString());

        QFile copy(cliConfigPath(to.path()));
        QVERIFY(copy.open(QIODevice::ReadOnly));
        QCOMPARE(copy.readAll(), QByteArray("encrypted-settings"));
        QCOMPARE(copy.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup
                                       | QFileDevice::WriteOther),
                 QFileDevice::Permissions());
        const QFileInfo dir(QFileInfo(cliConfigPath(to.path())).path());
        QCOMPARE(dir.permissions() & (QFileDevice::ReadOther | QFileDevice::ExeOther), QFileDevice::Permissions());
    }

    void importRejectsSymlink()
    {
        QTemporaryDir from;
        QTemporaryDir to;
        writeFile(from.path() + QStringLiteral("/secret"), "not yours");
        QDir().mkpath(QFileInfo(cliConfigPath(from.path())).path());
        QVERIFY(QFile::link(from.path() + QStringLiteral("/secret"), cliConfigPath(from.path())));

        QVERIFY(!importCliConfig(from.path(), getuid(), to.path()).isEmpty());
        QVERIFY(!QFile::exists(cliConfigPath(to.path())));
    }

    void importRejectsForeignOwner()
    {
        QTemporaryDir from;
        QTemporaryDir to;
        writeFile(cliConfigPath(from.path()), "settings");

        QVERIFY(!importCliConfig(from.path(), getuid() + 1, to.path()).isEmpty());
        QVERIFY(!QFile::exists(cliConfigPath(to.path())));
    }

    void importReportsMissingLogin()
    {
        QTemporaryDir from;
        QTemporaryDir to;
        QVERIFY(!importCliConfig(from.path(), getuid(), to.path()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestHostSystem)
#include "tst_hostsystem.moc"
