#include "appexclusionservice.h"
#include "appstore.h"
#include "routingengine.h"
#include "runsocketserver.h"

#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace agv;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(data);
}

class OkCommands : public SystemCommands
{
public:
    Result run(const QString &, const QStringList &, const QByteArray & = {}) override { return {0, {}}; }
};

struct Run {
    int exitCode = -1;
    QByteArray out;
    QByteArray err;
};

} // namespace

class TestAgvRun : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dir.reset(new QTemporaryDir);
        m_paths.cgroupRoot = m_dir->path() + QStringLiteral("/cgroup");
        m_paths.procSys = m_dir->path() + QStringLiteral("/sys");
        m_paths.selfCgroup = m_dir->path() + QStringLiteral("/self-cgroup");
        writeFile(m_paths.cgroupRoot + QStringLiteral("/cgroup.controllers"), "memory\n");
        writeFile(m_paths.selfCgroup, "0::/system.slice/agvd.service\n");

        m_engine.reset(new RoutingEngine(&m_commands, m_paths));
        m_store.reset(new AppStore(m_dir->path() + QStringLiteral("/apps.json")));
        m_apps.reset(new AppExclusionService(m_store.get(), m_engine.get()));
        m_apps->start();
        QVERIFY(m_apps->isSupported());

        m_socket = m_dir->path() + QStringLiteral("/run.sock");
        m_server.reset(new RunSocketServer(m_apps.get(), m_socket));
        QVERIFY2(m_server->listen(), qPrintable(m_server->errorString()));
    }

    void cleanup()
    {
        m_server.reset();
        m_apps.reset();
        m_store.reset();
        m_engine.reset();
        m_dir.reset();
    }

    void commandKeepsThePidThatWasMoved()
    {
        // "exec" in agv-run means the shell reports the very pid agvd put into the cgroup.
        const Run run = agvRun({QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("echo $$")});
        QCOMPARE(run.exitCode, 0);
        const QByteArray pid = run.out.trimmed();
        QVERIFY(!pid.isEmpty());
        QCOMPARE(readFile(appProcs(QStringLiteral("run"))), pid + '\n');
    }

    void namedApp()
    {
        QString id;
        QVERIFY(m_apps->add({{QStringLiteral("name"), QStringLiteral("Game")}}, getuid(), &id).isOk());

        const Run run = agvRun({QStringLiteral("--app"), id, QStringLiteral("--"), QStringLiteral("sh"),
                                QStringLiteral("-c"), QStringLiteral("echo $$")});
        QCOMPARE(run.exitCode, 0);
        QCOMPARE(readFile(appProcs(id)), run.out.trimmed() + '\n');
        QVERIFY(readFile(appProcs(QStringLiteral("run"))).isEmpty());
    }

    void refusedAppDoesNotRunTheCommand()
    {
        const Run run = agvRun({QStringLiteral("--app"), QStringLiteral("app-deadbeef"), QStringLiteral("sh"),
                                QStringLiteral("-c"), QStringLiteral("echo ran")});
        QCOMPARE(run.exitCode, 1);
        QVERIFY(!run.out.contains("ran"));
        QVERIFY(run.err.contains("No enabled app exclusion"));
    }

    void daemonNotRunning()
    {
        m_server.reset();
        const Run run = agvRun({QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("echo ran")});
        QCOMPARE(run.exitCode, 1);
        QVERIFY(!run.out.contains("ran"));
        QVERIFY(run.err.contains("cannot reach agvd"));
    }

    void missingCommand()
    {
        QCOMPARE(agvRun({}).exitCode, 2);
        QCOMPARE(agvRun({QStringLiteral("--app"), QStringLiteral("bad id"), QStringLiteral("true")}).exitCode, 2);
    }

    void commandNotFound()
    {
        const Run run = agvRun({QStringLiteral("/nonexistent/program")});
        QCOMPARE(run.exitCode, 127);
    }

    void garbageRequestIsRejected()
    {
        QLocalSocket socket;
        socket.connectToServer(m_socket);
        QVERIFY(socket.waitForConnected(1000));
        socket.write("MOVE 1\n");
        QTRY_VERIFY(socket.bytesAvailable() > 0 || socket.state() == QLocalSocket::UnconnectedState);
        QVERIFY(socket.readAll().startsWith("ERR"));
    }

private:
    QString appProcs(const QString &id) const
    {
        return m_paths.cgroupRoot + QStringLiteral("/system.slice/agvd.service/novpn/") + id
            + QStringLiteral("/cgroup.procs");
    }

    // Runs the real agv-run binary while this test's event loop serves its request.
    Run agvRun(const QStringList &args)
    {
        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("AGV_RUN_SOCKET"), m_socket);
        process.setProcessEnvironment(env);
        QSignalSpy finished(&process, &QProcess::finished);
        process.start(QStringLiteral(AGV_RUN_BINARY), args);
        if (!finished.wait(5000))
            return {};
        return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    RoutingPaths m_paths;
    OkCommands m_commands;
    std::unique_ptr<RoutingEngine> m_engine;
    std::unique_ptr<AppStore> m_store;
    std::unique_ptr<AppExclusionService> m_apps;
    std::unique_ptr<RunSocketServer> m_server;
    QString m_socket;
};

QTEST_GUILESS_MAIN(TestAgvRun)
#include "tst_agvrun.moc"
