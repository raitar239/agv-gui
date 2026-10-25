#include "appexclusionservice.h"
#include "appstore.h"
#include "routingengine.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
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

// Records every command; answers from a table keyed by "program arg arg…".
class FakeCommands : public SystemCommands
{
public:
    struct Call {
        QString command;
        QByteArray input;
    };
    QList<Call> calls;
    QMap<QString, Result> answers;

    Result run(const QString &program, const QStringList &args, const QByteArray &input = {}) override
    {
        const QString command = (QStringList{program} + args).join(QLatin1Char(' '));
        calls.append({command, input});
        return answers.value(command, Result{0, {}});
    }

    QStringList commands() const
    {
        QStringList result;
        for (const Call &call : calls)
            result << call.command;
        return result;
    }
};

// A fake /sys/fs/cgroup + /proc/sys for a daemon living in system.slice/agvd.service.
struct FakeSystem {
    QTemporaryDir dir;
    RoutingPaths paths;

    FakeSystem()
    {
        paths.cgroupRoot = dir.path() + QStringLiteral("/cgroup");
        paths.procSys = dir.path() + QStringLiteral("/sys");
        paths.selfCgroup = dir.path() + QStringLiteral("/self-cgroup");
        paths.stateFile = dir.path() + QStringLiteral("/routing-state");
        writeFile(paths.cgroupRoot + QStringLiteral("/cgroup.controllers"), "cpu memory pids\n");
        writeFile(paths.cgroupRoot + QStringLiteral("/system.slice/agvd.service/cgroup.procs"), "");
        writeFile(paths.selfCgroup, "0::/system.slice/agvd.service\n");
        writeFile(srcValidMark(), "0\n");
    }

    QString srcValidMark() const { return paths.procSys + QStringLiteral("/net/ipv4/conf/all/src_valid_mark"); }
    QString appDir(const QString &id) const
    {
        return paths.cgroupRoot + QStringLiteral("/system.slice/agvd.service/novpn/") + id;
    }
    QByteArray ownProcs() const
    {
        return readFile(paths.cgroupRoot + QStringLiteral("/system.slice/agvd.service/cgroup.procs"));
    }
};

QString unshare()
{
    return QStandardPaths::findExecutable(QStringLiteral("unshare"));
}

// Runs a shell snippet as "root" in a private user+network namespace, where nftables works
// without privileges and without touching the host's firewall.
std::optional<QByteArray> runInNetns(const QString &script, const QByteArray &input)
{
    if (unshare().isEmpty() || QStandardPaths::findExecutable(QStringLiteral("nft")).isEmpty())
        return std::nullopt;
    QProcess process;
    process.start(unshare(), {QStringLiteral("-rn"), QStringLiteral("sh"), QStringLiteral("-c"), script});
    process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(10'000) || process.exitCode() != 0) {
        qWarning() << "netns run failed:" << process.readAllStandardError();
        return std::nullopt;
    }
    return process.readAllStandardOutput();
}

} // namespace

class TestRouting : public QObject
{
    Q_OBJECT

private slots:
    void rulesetShape()
    {
        const QString ruleset = QString::fromUtf8(
            RoutingEngine::buildRuleset(QStringLiteral("system.slice/agvd.service/novpn"),
                                        {QStringLiteral("run"), QStringLiteral("app-0000abcd")}));

        QVERIFY(ruleset.startsWith(QLatin1String("table inet agvd\ndelete table inet agvd\n")));
        QVERIFY(ruleset.contains(QLatin1String(
            "socket cgroupv2 level 4 \"system.slice/agvd.service/novpn/app-0000abcd\" counter "
            "meta mark set 0x61677664 ct mark set meta mark comment \"out:app-0000abcd\"")));
        QVERIFY(ruleset.contains(QLatin1String("comment \"in:run\"")));
        QVERIFY(ruleset.contains(QLatin1String("ct mark 0x61677664 meta mark set ct mark")));
        QVERIFY(ruleset.contains(QLatin1String("meta mark 0x61677664 oifname != \"lo\" masquerade")));
        QVERIFY(ruleset.contains(QLatin1String("oifname \"lo\" return")));
    }

    void rulesetLoadsIntoRealNftables()
    {
        // Point the rules at this test's own cgroup, which certainly exists.
        QString own;
        for (const QByteArray &line : readFile(QStringLiteral("/proc/self/cgroup")).split('\n')) {
            if (line.startsWith("0::/"))
                own = QString::fromUtf8(line.mid(4));
        }
        if (own.isEmpty() || !own.contains(QLatin1Char('/')))
            QSKIP("Not in a nested cgroup v2");
        const QString base = own.left(own.lastIndexOf(QLatin1Char('/')));
        const QString leaf = own.mid(own.lastIndexOf(QLatin1Char('/')) + 1);

        const QByteArray ruleset = RoutingEngine::buildRuleset(base, {leaf});
        // Load twice: the second load must replace the table, not fail on "exists".
        const auto json = runInNetns(QStringLiteral("nft -f - && nft -j list table inet agvd"),
                                     ruleset + ruleset);
        if (!json)
            QSKIP("unshare -rn with nftables is not available here");

        // traffic() must understand what real nft prints.
        FakeCommands commands;
        commands.answers.insert(QStringLiteral("nft -j list table inet agvd"), {0, *json});
        FakeSystem system;
        RoutingEngine engine(&commands, system.paths);
        const auto traffic = engine.traffic();
        QVERIFY2(traffic.contains(leaf), json->constData());
        QCOMPARE(traffic.value(leaf).bytesOut, 0u);
    }

    void initFindsOwnCgroup()
    {
        FakeSystem system;
        FakeCommands commands;
        RoutingEngine engine(&commands, system.paths);
        QVERIFY(engine.init());
        QCOMPARE(engine.baseCgroup(), QStringLiteral("system.slice/agvd.service/novpn"));
    }

    void initWithoutCgroupV2()
    {
        FakeSystem system;
        QFile::remove(system.paths.cgroupRoot + QStringLiteral("/cgroup.controllers"));
        FakeCommands commands;
        RoutingEngine engine(&commands, system.paths);
        QVERIFY(!engine.init());
        QVERIFY(!engine.isSupported());
        QVERIFY(engine.lastError().contains(QLatin1String("cgroup v2")));
    }

    void initWithoutNft()
    {
        FakeSystem system;
        FakeCommands commands;
        commands.answers.insert(QStringLiteral("nft --version"), {-1, "not found"});
        RoutingEngine engine(&commands, system.paths);
        QVERIFY(!engine.init());
        QVERIFY(engine.lastError().contains(QLatin1String("nft")));
    }

    void applyMoveAndRelease()
    {
        FakeSystem system;
        FakeCommands commands;
        RoutingEngine engine(&commands, system.paths);
        QVERIFY(engine.init());
        commands.calls.clear();

        QVERIFY(engine.apply({QStringLiteral("run"), QStringLiteral("app-00000001")}));
        QVERIFY(QFileInfo(system.appDir(QStringLiteral("app-00000001"))).isDir());
        QVERIFY(QFileInfo(system.appDir(QStringLiteral("run"))).isDir());

        // Cgroups exist before nft loads (it resolves paths at load time), then the ip rules.
        const QStringList commandsRun = commands.commands();
        QCOMPARE(commandsRun.first(), QStringLiteral("nft -f -"));
        QVERIFY(commands.calls.first().input.contains("app-00000001"));
        QVERIFY(commandsRun.contains(QStringLiteral("ip -4 rule add fwmark 0x61677664 lookup main priority 30700")));
        QVERIFY(commandsRun.contains(QStringLiteral("ip -6 rule add fwmark 0x61677664 lookup main priority 30700")));
        QVERIFY(commandsRun.indexOf(QStringLiteral("ip -4 rule del priority 30700"))
                < commandsRun.indexOf(QStringLiteral("ip -4 rule add fwmark 0x61677664 lookup main priority 30700")));

        QCOMPARE(readFile(system.srcValidMark()), QByteArray("1\n"));
        QVERIFY(readFile(system.paths.stateFile).contains("src_valid_mark=0"));

        QVERIFY(engine.moveProcess(4242, QStringLiteral("app-00000001")));
        QCOMPARE(readFile(system.appDir(QStringLiteral("app-00000001")) + QStringLiteral("/cgroup.procs")),
                 QByteArray("4242\n"));
        QVERIFY(!engine.moveProcess(4242, QStringLiteral("app-ffffffff")));
        QVERIFY(!engine.moveProcess(0, QStringLiteral("run")));

        // Dropping the app sends its processes back to the daemon's cgroup (through the VPN).
        QVERIFY(engine.apply({QStringLiteral("run")}));
        QCOMPARE(system.ownProcs(), QByteArray("4242\n"));
        // A second apply must keep the original sysctl value, not our own "1".
        QVERIFY(readFile(system.paths.stateFile).contains("src_valid_mark=0"));
    }

    void applyStopsWhenNftFails()
    {
        FakeSystem system;
        FakeCommands commands;
        commands.answers.insert(QStringLiteral("nft -f -"), {1, "Error: syntax error"});
        RoutingEngine engine(&commands, system.paths);
        QVERIFY(engine.init());
        commands.calls.clear();

        QVERIFY(!engine.apply({QStringLiteral("run")}));
        QVERIFY(engine.lastError().contains(QLatin1String("syntax error")));
        QCOMPARE(commands.commands(), QStringList{QStringLiteral("nft -f -")});
        QCOMPARE(readFile(system.srcValidMark()), QByteArray("0\n"));
    }

    void cleanupFromAnotherProcess()
    {
        FakeSystem system;
        {
            FakeCommands commands;
            RoutingEngine engine(&commands, system.paths);
            QVERIFY(engine.init());
            QVERIFY(engine.apply({QStringLiteral("run"), QStringLiteral("app-00000002")}));
            QVERIFY(engine.moveProcess(777, QStringLiteral("app-00000002")));
        }

        // "agvd --cleanup" runs in some other cgroup and never calls init().
        writeFile(system.paths.selfCgroup, "0::/user.slice/somewhere\n");
        FakeCommands commands;
        RoutingEngine cleanup(&commands, system.paths);
        cleanup.teardown();

        QVERIFY(commands.commands().contains(QStringLiteral("nft delete table inet agvd")));
        QVERIFY(commands.commands().contains(QStringLiteral("ip -4 rule del priority 30700")));
        QCOMPARE(readFile(system.srcValidMark()), QByteArray("0\n"));
        QCOMPARE(system.ownProcs(), QByteArray("777\n"));
        QVERIFY(!QFile::exists(system.paths.stateFile));
    }

    void trafficFromNftJson()
    {
        FakeSystem system;
        FakeCommands commands;
        commands.answers.insert(QStringLiteral("nft -j list table inet agvd"), {0, R"({"nftables": [
            {"metainfo": {"version": "1.1.7"}},
            {"rule": {"chain": "output", "comment": "out:app-1", "expr": [{"counter": {"packets": 3, "bytes": 1500}}]}},
            {"rule": {"chain": "input", "comment": "in:app-1", "expr": [{"counter": {"packets": 2, "bytes": 800}}]}},
            {"rule": {"chain": "output", "comment": "out:run", "expr": [{"counter": {"packets": 0, "bytes": 0}}]}},
            {"rule": {"chain": "prerouting", "expr": [{"mangle": {}}]}}
        ]})"});
        RoutingEngine engine(&commands, system.paths);

        const auto traffic = engine.traffic();
        QCOMPARE(traffic.size(), 2);
        QCOMPARE(traffic.value(QStringLiteral("app-1")).bytesOut, 1500u);
        QCOMPARE(traffic.value(QStringLiteral("app-1")).bytesIn, 800u);
        QCOMPARE(traffic.value(QStringLiteral("run")).bytesOut, 0u);
    }

    void storeRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/apps.json");
        AppStore store(path);
        QCOMPARE(store.load(), QString());

        QString error;
        auto rule = AppStore::fromMap({{QStringLiteral("name"), QStringLiteral(" Genshin Impact ")},
                                       {QStringLiteral("cmdline_match"), QStringLiteral("GenshinImpact.exe")}},
                                      &error);
        QVERIFY2(rule, qPrintable(error));
        const QString id = store.add(*rule);
        QVERIFY(AppStore::isValidId(id));
        QCOMPARE(store.save(), QString());
        QCOMPARE(QFile(path).permissions() & QFileDevice::ReadOther, QFileDevice::Permissions());

        AppStore reloaded(path);
        QCOMPARE(reloaded.load(), QString());
        QCOMPARE(reloaded.rules().size(), 1);
        QCOMPARE(reloaded.rules().first().id, id);
        QCOMPARE(reloaded.rules().first().name, QStringLiteral("Genshin Impact"));
        QVERIFY(reloaded.rules().first().enabled);
    }

    void storeValidation_data()
    {
        QTest::addColumn<QVariantMap>("fields");
        QTest::newRow("no name") << QVariantMap{};
        QTest::newRow("blank name") << QVariantMap{{QStringLiteral("name"), QStringLiteral("  ")}};
        QTest::newRow("unknown key") << QVariantMap{{QStringLiteral("name"), QStringLiteral("x")},
                                                    {QStringLiteral("cgroup"), QStringLiteral("/")}};
        QTest::newRow("relative exe") << QVariantMap{{QStringLiteral("name"), QStringLiteral("x")},
                                                     {QStringLiteral("exe"), QStringLiteral("firefox")}};
        QTest::newRow("bad desktop") << QVariantMap{{QStringLiteral("name"), QStringLiteral("x")},
                                                    {QStringLiteral("desktop_file"), QStringLiteral("x.sh")}};
        QTest::newRow("enabled type") << QVariantMap{{QStringLiteral("name"), QStringLiteral("x")},
                                                     {QStringLiteral("enabled"), QStringLiteral("yes")}};
        QTest::newRow("name type") << QVariantMap{{QStringLiteral("name"), 42}};
    }

    void storeValidation()
    {
        QFETCH(QVariantMap, fields);
        QString error;
        QVERIFY(!AppStore::fromMap(fields, &error));
        QVERIFY(!error.isEmpty());
    }

    void storeRejectsCorruptFile()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/apps.json");
        writeFile(path, R"({"version":1,"apps":[{"id":"../../etc","name":"x"}]})");
        AppStore store(path);
        QVERIFY(!store.load().isEmpty());
        QVERIFY(store.rules().isEmpty());
    }

    void serviceAppliesAndRollsBack()
    {
        FakeSystem system;
        FakeCommands commands;
        RoutingEngine engine(&commands, system.paths);
        QTemporaryDir dir;
        AppStore store(dir.path() + QStringLiteral("/apps.json"));
        AppExclusionService apps(&store, &engine);
        apps.start();
        QVERIFY(apps.isSupported());

        QString id;
        QVERIFY(apps.add({{QStringLiteral("name"), QStringLiteral("Firefox")}}, getuid(), &id).isOk());
        QVERIFY(QFileInfo(system.appDir(id)).isDir());
        QVERIFY(apps.moveProcess(100, id).isOk());
        QVERIFY(apps.moveProcess(101, QString()).isOk()); // builtin agv-run group
        QCOMPARE(apps.moveProcess(102, QStringLiteral("app-deadbeef")).name, errors::NotFound);

        // Disabled apps lose their cgroup and cannot take new processes.
        QVERIFY(apps.update(id, {{QStringLiteral("enabled"), false}}, getuid()).isOk());
        QCOMPARE(apps.moveProcess(103, id).name, errors::NotFound);
        QVERIFY(system.ownProcs().contains("100"));

        // A failing kernel update leaves both the store and the saved file unchanged.
        const QByteArray saved = readFile(dir.path() + QStringLiteral("/apps.json"));
        commands.answers.insert(QStringLiteral("nft -f -"), {1, "Error: boom"});
        QString second;
        const Error error = apps.add({{QStringLiteral("name"), QStringLiteral("Steam")}}, getuid(), &second);
        QCOMPARE(error.name, errors::SystemFailed);
        QCOMPARE(apps.rules().size(), 1);
        QCOMPARE(readFile(dir.path() + QStringLiteral("/apps.json")), saved);
    }

    void serviceWithoutSupport()
    {
        FakeSystem system;
        QFile::remove(system.paths.cgroupRoot + QStringLiteral("/cgroup.controllers"));
        FakeCommands commands;
        RoutingEngine engine(&commands, system.paths);
        QTemporaryDir dir;
        AppStore store(dir.path() + QStringLiteral("/apps.json"));
        AppExclusionService apps(&store, &engine);
        apps.start();

        QVERIFY(!apps.isSupported());
        QString id;
        QCOMPARE(apps.add({{QStringLiteral("name"), QStringLiteral("x")}}, getuid(), &id).name, errors::NotSupported);
        QVERIFY(commands.commands().filter(QStringLiteral("nft -f")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRouting)
#include "tst_routing.moc"
