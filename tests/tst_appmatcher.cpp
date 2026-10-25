#include "appexclusionservice.h"
#include "appmatcher.h"
#include "routingengine.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace agv;

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(data);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Adds a process to a fake /proc: cmdline, exe symlink, cgroup and status.
void addProcess(const QString &proc, int pid, const QStringList &argv, const QString &exe,
                const QString &cgroup = QStringLiteral("user.slice/user-1000.slice/session-1.scope"),
                uid_t uid = 1000, int ppid = 1)
{
    const QString dir = proc + QLatin1Char('/') + QString::number(pid);
    QByteArray cmdline;
    for (const QString &arg : argv)
        cmdline += arg.toUtf8() + '\0';
    writeFile(dir + QStringLiteral("/cmdline"), cmdline);
    writeFile(dir + QStringLiteral("/cgroup"), "0::/" + cgroup.toUtf8() + '\n');
    writeFile(dir + QStringLiteral("/status"), "Name:\tx\nPPid:\t" + QByteArray::number(ppid) + "\nUid:\t"
                                                   + QByteArray::number(uid) + "\t0\t0\t0\n");
    if (!exe.isEmpty())
        QFile::link(exe, dir + QStringLiteral("/exe"));
}

AppRule rule(const QString &id, const QString &exe, const QString &cmdline = {}, qint64 owner = -1)
{
    AppRule r;
    r.id = id;
    r.name = id;
    r.exe = exe;
    r.cmdlineMatch = cmdline;
    r.ownerUid = owner;
    return r;
}

class OkCommands : public SystemCommands
{
public:
    Result run(const QString &, const QStringList &, const QByteArray & = {}) override { return {0, {}}; }
};

const QString Base = QStringLiteral("system.slice/agvd.service/novpn");

} // namespace

class TestAppMatcher : public QObject
{
    Q_OBJECT

private slots:
    void readsProcess()
    {
        QTemporaryDir proc;
        addProcess(proc.path(), 10, {QStringLiteral("/usr/bin/firefox"), QStringLiteral("--new-window")},
                   QStringLiteral("/usr/lib/firefox/firefox (deleted)"));
        writeFile(proc.path() + QStringLiteral("/11/cmdline"), ""); // kernel thread

        const auto info = readProcess(10, proc.path());
        QVERIFY(info);
        QCOMPARE(info->exe, QStringLiteral("/usr/lib/firefox/firefox"));
        QCOMPARE(info->argv, QStringList({QStringLiteral("/usr/bin/firefox"), QStringLiteral("--new-window")}));
        QCOMPARE(info->cgroup, QStringLiteral("user.slice/user-1000.slice/session-1.scope"));
        QCOMPARE(info->uid, 1000);
        QVERIFY(!readProcess(11, proc.path()));
        QVERIFY(!readProcess(12, proc.path()));
        QCOMPARE(listProcesses(proc.path()).size(), 2);
    }

    void matching_data()
    {
        QTest::addColumn<QStringList>("argv");
        QTest::addColumn<QString>("exe");
        QTest::addColumn<QString>("cgroup");
        QTest::addColumn<int>("uid");
        QTest::addColumn<QString>("expected");

        const QString user = QStringLiteral("user.slice/user-1000.slice/app.slice/app-firefox.scope");
        QTest::newRow("binary") << QStringList{QStringLiteral("firefox")} << QStringLiteral("/usr/bin/firefox")
                                << user << 1000 << QStringLiteral("app-firefox");
        QTest::newRow("argv0") << QStringList{QStringLiteral("/usr/bin/firefox")} << QStringLiteral("/opt/ff/ff")
                               << user << 1000 << QStringLiteral("app-firefox");
        QTest::newRow("wrapper script") << QStringList{QStringLiteral("/bin/sh"), QStringLiteral("/usr/bin/firefox")}
                                        << QStringLiteral("/usr/bin/bash") << user << 1000 << QStringLiteral("app-firefox");
        QTest::newRow("not a script") << QStringList{QStringLiteral("xdg-open"), QStringLiteral("/usr/bin/firefox")}
                                      << QStringLiteral("/usr/bin/xdg-open") << user << 1000 << QString();
        QTest::newRow("wine game") << QStringList{QStringLiteral("C:\\Games\\Genshin\\GenshinImpact.exe")}
                                   << QStringLiteral("/usr/bin/wine64-preloader") << user << 1000
                                   << QStringLiteral("app-genshin");
        QTest::newRow("already excluded") << QStringList{QStringLiteral("firefox")} << QStringLiteral("/usr/bin/firefox")
                                          << Base + QStringLiteral("/app-firefox") << 1000 << QString();
        QTest::newRow("other user") << QStringList{QStringLiteral("firefox")} << QStringLiteral("/usr/bin/firefox")
                                    << user << 1001 << QString();
        QTest::newRow("unrelated") << QStringList{QStringLiteral("vim")} << QStringLiteral("/usr/bin/vim")
                                   << user << 1000 << QString();
        QTest::newRow("disabled") << QStringList{QStringLiteral("steam")} << QStringLiteral("/usr/bin/steam")
                                  << user << 1000 << QString();
    }

    void matching()
    {
        QFETCH(QStringList, argv);
        QFETCH(QString, exe);
        QFETCH(QString, cgroup);
        QFETCH(int, uid);
        QFETCH(QString, expected);

        AppRule steam = rule(QStringLiteral("app-steam"), QStringLiteral("/usr/bin/steam"), {}, 1000);
        steam.enabled = false;
        AppMatcher matcher;
        matcher.setRules({rule(QStringLiteral("app-firefox"), QStringLiteral("/usr/bin/firefox"), {}, 1000),
                          rule(QStringLiteral("app-genshin"), {}, QStringLiteral("GenshinImpact.exe"), 1000), steam},
                         Base);

        ProcessInfo process;
        process.pid = 100;
        process.argv = argv;
        process.exe = exe;
        process.cgroup = cgroup;
        process.uid = uid;
        QCOMPARE(matcher.match(process), expected);
    }

    void ruleExeSymlinkIsResolved()
    {
        QTemporaryDir dir;
        writeFile(dir.path() + QStringLiteral("/opt/app/real"), "");
        QDir().mkpath(dir.path() + QStringLiteral("/bin"));
        QVERIFY(QFile::link(dir.path() + QStringLiteral("/opt/app/real"), dir.path() + QStringLiteral("/bin/app")));

        AppMatcher matcher;
        matcher.setRules({rule(QStringLiteral("app-x"), dir.path() + QStringLiteral("/bin/app"))}, Base);

        ProcessInfo process;
        process.argv = {QStringLiteral("app")};
        process.exe = dir.path() + QStringLiteral("/opt/app/real");
        process.uid = 1000;
        QCOMPARE(matcher.match(process), QStringLiteral("app-x"));
    }

    void serviceCatchesRunningAndNewProcesses()
    {
        QTemporaryDir dir;
        RoutingPaths paths;
        paths.cgroupRoot = dir.path() + QStringLiteral("/cgroup");
        paths.procSys = dir.path() + QStringLiteral("/sys");
        paths.selfCgroup = dir.path() + QStringLiteral("/self-cgroup");
        writeFile(paths.cgroupRoot + QStringLiteral("/cgroup.controllers"), "memory\n");
        writeFile(paths.selfCgroup, "0::/system.slice/agvd.service\n");
        const QString proc = dir.path() + QStringLiteral("/proc");
        const uid_t me = getuid();

        // One firefox already running, plus something unrelated.
        addProcess(proc, 200, {QStringLiteral("firefox")}, QStringLiteral("/usr/bin/firefox"),
                   QStringLiteral("user.slice/u.scope"), me);
        addProcess(proc, 201, {QStringLiteral("vim")}, QStringLiteral("/usr/bin/vim"),
                   QStringLiteral("user.slice/u.scope"), me);

        OkCommands commands;
        RoutingEngine engine(&commands, paths);
        AppStore store(dir.path() + QStringLiteral("/apps.json"));
        AppExclusionService apps(&store, &engine);
        apps.setProcDir(proc);
        apps.start();
        QVERIFY(apps.isSupported());

        QString id;
        QVERIFY(apps.add({{QStringLiteral("name"), QStringLiteral("Firefox")},
                          {QStringLiteral("exe"), QStringLiteral("/usr/bin/firefox")}},
                         me, &id).isOk());
        const QString procs = paths.cgroupRoot + QLatin1Char('/') + Base + QLatin1Char('/') + id
            + QStringLiteral("/cgroup.procs");
        QCOMPARE(readFile(procs), QByteArray("200\n"));

        // A new firefox appears and the kernel reports its exec().
        addProcess(proc, 300, {QStringLiteral("/usr/bin/firefox")}, QStringLiteral("/usr/bin/firefox"),
                   QStringLiteral("user.slice/u.scope"), me);
        apps.onExecuted(300);
        apps.onExecuted(201);
        apps.onExecuted(1);
        QCOMPARE(readFile(procs), QByteArray("200\n300\n"));
    }

    void lineageAndGroups()
    {
        QTemporaryDir proc;
        addProcess(proc.path(), 40, {QStringLiteral("zsh")}, QStringLiteral("/usr/bin/zsh"),
                   QStringLiteral("user.slice/kitty.slice/kitty-39"), 1000, 39);
        const auto lineage = readLineage(40, proc.path());
        QVERIFY(lineage);
        QCOMPARE(lineage->ppid, pid_t(39));
        QCOMPARE(lineage->cgroup, QStringLiteral("user.slice/kitty.slice/kitty-39"));
        QVERIFY(!readLineage(41, proc.path()));

        AppMatcher matcher;
        matcher.setRules({}, Base);
        QCOMPARE(matcher.groupOf(Base + QStringLiteral("/app-1/sub")), QStringLiteral("app-1"));
        QCOMPARE(matcher.groupOf(Base + QStringLiteral("/run")), QStringLiteral("run"));
        QCOMPARE(matcher.groupOf(Base), QString());
        QCOMPARE(matcher.groupOf(QStringLiteral("user.slice/x")), QString());
    }

    void descendantsStayExcluded()
    {
        // kitty is excluded; it asks systemd to put each shell into its own scope.
        QTemporaryDir dir;
        RoutingPaths paths;
        paths.cgroupRoot = dir.path() + QStringLiteral("/cgroup");
        paths.procSys = dir.path() + QStringLiteral("/sys");
        paths.selfCgroup = dir.path() + QStringLiteral("/self-cgroup");
        writeFile(paths.cgroupRoot + QStringLiteral("/cgroup.controllers"), "memory\n");
        writeFile(paths.selfCgroup, "0::/system.slice/agvd.service\n");
        const QString proc = dir.path() + QStringLiteral("/proc");
        const uid_t me = getuid();

        OkCommands commands;
        RoutingEngine engine(&commands, paths);
        AppStore store(dir.path() + QStringLiteral("/apps.json"));
        AppExclusionService apps(&store, &engine);
        apps.setProcDir(proc);
        apps.start();

        QString id;
        QVERIFY(apps.add({{QStringLiteral("name"), QStringLiteral("kitty")},
                          {QStringLiteral("exe"), QStringLiteral("/usr/bin/kitty")}}, me, &id).isOk());
        const QString group = Base + QLatin1Char('/') + id;
        const QString procs = paths.cgroupRoot + QLatin1Char('/') + group + QStringLiteral("/cgroup.procs");

        // kitty itself, already moved; its shell starts in kitty's own scope.
        addProcess(proc, 500, {QStringLiteral("kitty")}, QStringLiteral("/usr/bin/kitty"), group, me, 1);
        writeFile(procs, "500\n");
        addProcess(proc, 501, {QStringLiteral("-zsh")}, QStringLiteral("/usr/bin/zsh"),
                   QStringLiteral("user.slice/user-1000.slice/user@1000.service/kitty.slice/kitty-500"), me, 500);
        // A grandchild started from the shell, and an unrelated process.
        addProcess(proc, 502, {QStringLiteral("curl")}, QStringLiteral("/usr/bin/curl"),
                   QStringLiteral("user.slice/user-1000.slice/user@1000.service/kitty.slice/kitty-500"), me, 501);
        addProcess(proc, 600, {QStringLiteral("vim")}, QStringLiteral("/usr/bin/vim"),
                   QStringLiteral("user.slice/u.scope"), me, 1);

        apps.onExecuted(501);
        QCOMPARE(readFile(procs), QByteArray("500\n501\n"));
        // What the kernel would now report for the moved shell.
        writeFile(proc + QStringLiteral("/501/cgroup"), "0::/" + group.toUtf8() + '\n');

        // The periodic pass finds the rest; the unrelated process stays where it is.
        apps.reclaimDescendants();
        QCOMPARE(readFile(procs), QByteArray("500\n501\n502\n"));
        apps.onExecuted(600);
        QVERIFY(!readFile(procs).contains("600"));
    }

    void usersOnlySeeTheirOwnRules()
    {
        QTemporaryDir dir;
        RoutingPaths paths;
        paths.cgroupRoot = dir.path() + QStringLiteral("/cgroup");
        paths.procSys = dir.path() + QStringLiteral("/sys");
        paths.selfCgroup = dir.path() + QStringLiteral("/self-cgroup");
        writeFile(paths.cgroupRoot + QStringLiteral("/cgroup.controllers"), "memory\n");
        writeFile(paths.selfCgroup, "0::/system.slice/agvd.service\n");

        OkCommands commands;
        RoutingEngine engine(&commands, paths);
        AppStore store(dir.path() + QStringLiteral("/apps.json"));
        AppExclusionService apps(&store, &engine);
        apps.setProcDir(dir.path() + QStringLiteral("/proc"));
        apps.start();

        QString alice;
        QString admin;
        QVERIFY(apps.add({{QStringLiteral("name"), QStringLiteral("Alice's game")}}, 1000, &alice).isOk());
        QVERIFY(apps.add({{QStringLiteral("name"), QStringLiteral("For everyone")}}, 0, &admin).isOk());

        QCOMPARE(apps.rules(1000).size(), 1);
        QCOMPARE(apps.rules(1001).size(), 0);
        QCOMPARE(apps.rules(0).size(), 2);
        QCOMPARE(store.find(admin)->ownerUid, -1);

        QCOMPARE(apps.remove(alice, 1001).name, errors::NotFound);
        QCOMPARE(apps.update(alice, {{QStringLiteral("enabled"), false}}, 1001).name, errors::NotFound);
        QVERIFY(apps.update(alice, {{QStringLiteral("enabled"), false}}, 1000).isOk());
        QVERIFY(apps.remove(alice, 0).isOk());

        // Ownership survives a restart.
        AppStore reloaded(dir.path() + QStringLiteral("/apps.json"));
        QCOMPARE(reloaded.load(), QString());
        QCOMPARE(reloaded.find(admin)->ownerUid, -1);
    }
};

QTEST_GUILESS_MAIN(TestAppMatcher)
#include "tst_appmatcher.moc"
