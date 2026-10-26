#include "clirunner.h"
#include "vpnservice.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace agv;

namespace {

QString fixture(const QString &name)
{
    QFile file(QStringLiteral(AGV_FIXTURES_DIR "/1.8.1/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("cannot open fixture %s", qPrintable(file.fileName()));
    return QString::fromUtf8(file.readAll());
}

// A HOME for the fake CLI with canned responses keyed by the joined arguments.
class Scenario
{
public:
    Scenario() { QDir(m_home.path()).mkpath(QStringLiteral("responses")); }

    QString home() const { return m_home.path(); }

    void respond(const QString &key, const QString &output, int exitCode = 0, double sleepSeconds = 0)
    {
        write(key + QStringLiteral(".out"), output);
        write(key + QStringLiteral(".code"), QString::number(exitCode));
        if (sleepSeconds > 0)
            write(key + QStringLiteral(".sleep"), QString::number(sleepSeconds));
    }

    QStringList calls() const
    {
        QFile file(m_home.path() + QStringLiteral("/calls.log"));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

private:
    void write(const QString &name, const QString &content)
    {
        QFile file(m_home.path() + QStringLiteral("/responses/") + name);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content.toUtf8());
    }

    QTemporaryDir m_home;
};

const QString FakeCli = QStringLiteral(AGV_FAKE_CLI);

} // namespace

class TestVpnService : public QObject
{
    Q_OBJECT

private slots:
    void runnerReportsExitCodeAndOutput()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("status"), QStringLiteral("hello\n"), 3);
        CliRunner cli(FakeCli, scenario.home());

        std::optional<CliRunner::Result> result;
        cli.run({QStringLiteral("status")}, [&](const CliRunner::Result &r) { result = r; });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->exitCode, 3);
        QCOMPARE(result->output, QStringLiteral("hello\n"));
        QVERIFY(!result->ok());
    }

    void runnerTimesOut()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("status"), QString(), 0, 5);
        CliRunner cli(FakeCli, scenario.home());

        std::optional<CliRunner::Result> result;
        cli.run({QStringLiteral("status")}, [&](const CliRunner::Result &r) { result = r; }, 200);
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 3000);
        QVERIFY(result->timedOut);
    }

    void runnerRunsCommandsInOrder()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("a"), QStringLiteral("1"), 0, 0.2);
        scenario.respond(QStringLiteral("b"), QStringLiteral("2"));
        CliRunner cli(FakeCli, scenario.home());

        QStringList order;
        cli.run({QStringLiteral("a")}, [&](const CliRunner::Result &r) { order << r.output; });
        cli.run({QStringLiteral("b")}, [&](const CliRunner::Result &r) { order << r.output; });
        QTRY_COMPARE(order, QStringList({QStringLiteral("1"), QStringLiteral("2")}));
    }

    void runnerRunsCommandQueuedFromCallbackOnce()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("a"), QStringLiteral("1"));
        scenario.respond(QStringLiteral("b"), QStringLiteral("2"));
        CliRunner cli(FakeCli, scenario.home());

        QStringList outputs;
        cli.run({QStringLiteral("a")}, [&](const CliRunner::Result &r) {
            outputs << r.output;
            cli.run({QStringLiteral("b")}, [&](const CliRunner::Result &r2) { outputs << r2.output; });
        });
        QTRY_COMPARE(outputs, QStringList({QStringLiteral("1"), QStringLiteral("2")}));
        QTest::qWait(200);
        QCOMPARE(scenario.calls(), QStringList({QStringLiteral("a"), QStringLiteral("b")}));
    }

    void startReadsStatusAndExclusionMode()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("status"), fixture(QStringLiteral("status-connected.txt")));
        scenario.respond(QStringLiteral("site-exclusions_mode"), fixture(QStringLiteral("site-exclusions-mode.txt")));
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);

        vpn.start();
        QTRY_COMPARE(vpn.state(), VpnService::State::Connected);
        QCOMPARE(vpn.location(), QStringLiteral("ATHENS"));
        QCOMPARE(vpn.tunDevice(), QStringLiteral("tun0"));
        QVERIFY(vpn.loggedIn());
        QTRY_COMPARE(vpn.siteExclusionMode(), QStringLiteral("general"));
    }

    void autoConnect_data()
    {
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<QString>("statusFixture");
        QTest::addColumn<int>("statusCode");
        QTest::addColumn<bool>("connects");
        QTest::newRow("off") << false << "status-disconnected.txt" << 0 << false;
        QTest::newRow("on, disconnected") << true << "status-disconnected.txt" << 0 << true;
        QTest::newRow("on, already connected") << true << "status-connected.txt" << 0 << false;
        QTest::newRow("on, not logged in") << true << "status-not-logged-in.txt" << 11 << false;
    }

    void autoConnect()
    {
        QFETCH(bool, enabled);
        QFETCH(QString, statusFixture);
        QFETCH(int, statusCode);
        QFETCH(bool, connects);

        Scenario scenario;
        scenario.respond(QStringLiteral("status"), fixture(statusFixture), statusCode);
        scenario.respond(QStringLiteral("site-exclusions_mode"), fixture(QStringLiteral("site-exclusions-mode.txt")));
        scenario.respond(QStringLiteral("connect_-y_--no-progress"), QStringLiteral("Connected\n"));
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);
        vpn.setProcDir(scenario.home()); // no foreign VPN processes in there
        vpn.setAutoConnect(enabled);

        vpn.start();
        QTRY_VERIFY(scenario.calls().contains(QStringLiteral("site-exclusions|mode")));
        QTest::qWait(300);
        QCOMPARE(scenario.calls().contains(QStringLiteral("connect|-y|--no-progress")), connects);
    }

    void notLoggedIn()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("status"), fixture(QStringLiteral("status-not-logged-in.txt")), 11);
        scenario.respond(QStringLiteral("list-locations"),
                         fixture(QStringLiteral("list-locations-not-logged-in.txt")), 11);
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);

        vpn.refreshStatus();
        QTRY_COMPARE(vpn.state(), VpnService::State::NotLoggedIn);
        QVERIFY(!vpn.loggedIn());

        std::optional<Error> error;
        vpn.listLocations([&](const QList<cli::Location> &, const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QCOMPARE(error->name, errors::NotLoggedIn);
    }

    void connectPassesLocationAndGoesThroughConnecting()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("status"), fixture(QStringLiteral("status-connected.txt")));
        scenario.respond(QStringLiteral("connect_-y_--no-progress_-l_New York"), QStringLiteral("Connected\n"));
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);

        QStringList states;
        connect(&vpn, &VpnService::statusChanged, this, [&] {
            if (states.isEmpty() || states.last() != vpn.stateName())
                states << vpn.stateName();
        });

        std::optional<Error> error;
        vpn.connectTo(QStringLiteral("  New   York "), [&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QVERIFY(error->isOk());
        QCOMPARE(states, QStringList({QStringLiteral("connecting"), QStringLiteral("connected")}));
        QVERIFY(scenario.calls().contains(QStringLiteral("connect|-y|--no-progress|-l|New York")));
    }

    void connectFailureSetsError()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("status"), fixture(QStringLiteral("status-disconnected.txt")));
        scenario.respond(QStringLiteral("connect_-y_--no-progress_-f"),
                         QStringLiteral("Connecting...\nFailed to connect: no route\n"), 1);
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);

        std::optional<Error> error;
        vpn.connectFastest([&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QCOMPARE(error->name, errors::CliFailed);
        QCOMPARE(error->message, QStringLiteral("Failed to connect: no route"));
        QCOMPARE(vpn.state(), VpnService::State::Error);
        QCOMPARE(vpn.lastError(), QStringLiteral("Failed to connect: no route"));
    }

    void secondConnectIsBusy()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("status"), fixture(QStringLiteral("status-connected.txt")));
        scenario.respond(QStringLiteral("connect_-y_--no-progress_-f"), QString(), 0, 0.3);
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);

        std::optional<Error> first;
        std::optional<Error> second;
        vpn.connectFastest([&](const Error &e) { first = e; });
        vpn.disconnectVpn([&](const Error &e) { second = e; });
        QVERIFY(second.has_value());
        QCOMPARE(second->name, errors::Busy);
        QTRY_VERIFY(first.has_value());
        QVERIFY(first->isOk());
    }

    void missingCli()
    {
        Scenario scenario;
        CliRunner cli(QStringLiteral("/nonexistent/adguardvpn-cli"), scenario.home());
        VpnService vpn(&cli);

        std::optional<Error> error;
        vpn.siteExclusions([&](const QStringList &, const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QCOMPARE(error->name, errors::CliNotFound);
    }

    void siteExclusionsPassItemsAsSeparateArguments()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("site-exclusions_add_a.com_*.b.org"),
                         fixture(QStringLiteral("site-exclusions-add.txt")));
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);
        QSignalSpy changed(&vpn, &VpnService::siteExclusionsChanged);

        std::optional<Error> error;
        vpn.addSiteExclusions({QStringLiteral("a.com"), QStringLiteral("*.b.org")},
                              [&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QVERIFY(error->isOk());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(scenario.calls(), QStringList({QStringLiteral("site-exclusions|add|a.com|*.b.org")}));
    }

    void siteEntriesCanBeSwitchedOff()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("site-exclusions_show"),
                         QStringLiteral("Exclusions for GENERAL mode:\na.com\n"));
        scenario.respond(QStringLiteral("site-exclusions_remove_*.a.com"), QStringLiteral("Successfully removed 1 exclusions\n"));
        scenario.respond(QStringLiteral("site-exclusions_add_*.a.com"), QStringLiteral("Successfully added 1 exclusions\n"));
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);

        std::optional<Error> error;
        vpn.setSiteExclusionEnabled(QStringLiteral("*.a.com"), false, {}, [&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QVERIFY(error->isOk());
        QVERIFY(scenario.calls().contains(QStringLiteral("site-exclusions|remove|*.a.com")));

        // Listed as off, next to the active entries from the CLI.
        std::optional<QList<QPair<QString, bool>>> list;
        vpn.listSiteExclusions([&](const QList<QPair<QString, bool>> &items, const Error &) { list = items; });
        QTRY_VERIFY(list.has_value());
        QCOMPARE(*list, (QList<QPair<QString, bool>>{{QStringLiteral("a.com"), true}, {QStringLiteral("*.a.com"), false}}));

        // Back on: added to the CLI again and no longer remembered as off.
        error.reset();
        vpn.setSiteExclusionEnabled(QStringLiteral("*.a.com"), true, {}, [&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QVERIFY(scenario.calls().contains(QStringLiteral("site-exclusions|add|*.a.com")));
        list.reset();
        vpn.listSiteExclusions([&](const QList<QPair<QString, bool>> &items, const Error &) { list = items; });
        QTRY_VERIFY(list.has_value());
        QCOMPARE(list->size(), 1);

        // Removing a switched-off entry only touches agvd's own store.
        error.reset();
        vpn.setSiteExclusionEnabled(QStringLiteral("*.a.com"), false, {}, [&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        const int callsBefore = int(scenario.calls().size());
        error.reset();
        vpn.removeSiteExclusions({QStringLiteral("*.a.com")}, [&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QVERIFY(error->isOk());
        QCOMPARE(int(scenario.calls().size()), callsBefore);
    }

    void siteGroupsSwitchTogether()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("site-exclusions_show"),
                         QStringLiteral("Exclusions for GENERAL mode:\na.com\n*.a.com\n"));
        scenario.respond(QStringLiteral("site-exclusions_remove_cs.a.com"), QStringLiteral("Successfully removed 1 exclusions\n"));
        scenario.respond(QStringLiteral("site-exclusions_remove_a.com_*.a.com"), QStringLiteral("Successfully removed 2 exclusions\n"));
        scenario.respond(QStringLiteral("site-exclusions_add_a.com_*.a.com"), QStringLiteral("Successfully added 2 exclusions\n"));
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);
        const QStringList members{QStringLiteral("*.a.com"), QStringLiteral("cs.a.com")};

        // cs.a.com is switched off on its own first.
        std::optional<Error> error;
        vpn.setSiteExclusionEnabled(QStringLiteral("cs.a.com"), false, {}, [&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());

        // The site goes off with the members that were still on; those keep their own "on".
        error.reset();
        vpn.setSiteExclusionEnabled(QStringLiteral("a.com"), false, members, [&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QVERIFY(error->isOk());
        QVERIFY(scenario.calls().contains(QStringLiteral("site-exclusions|remove|a.com|*.a.com")));

        // Back on: only the members it held come back, cs.a.com stays off.
        error.reset();
        vpn.setSiteExclusionEnabled(QStringLiteral("a.com"), true, members, [&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QVERIFY(error->isOk());
        QVERIFY(scenario.calls().contains(QStringLiteral("site-exclusions|add|a.com|*.a.com")));
        std::optional<QList<QPair<QString, bool>>> list;
        vpn.listSiteExclusions([&](const QList<QPair<QString, bool>> &items, const Error &) { list = items; });
        QTRY_VERIFY(list.has_value());
        QCOMPARE(*list, (QList<QPair<QString, bool>>{{QStringLiteral("a.com"), true},
                                                     {QStringLiteral("*.a.com"), true},
                                                     {QStringLiteral("cs.a.com"), false}}));
    }

    void reconnectsAfterADrop()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("status"), fixture(QStringLiteral("status-connected.txt")));
        scenario.respond(QStringLiteral("connect_-y_--no-progress"), QStringLiteral("Connected\n"));
        scenario.respond(QStringLiteral("disconnect"), QStringLiteral("Disconnected\n"));
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);

        vpn.refreshStatus();
        QTRY_COMPARE(vpn.state(), VpnService::State::Connected);
        QVERIFY(vpn.wanted());
        QCOMPARE(vpn.lastTunDevice(), QStringLiteral("tun0"));

        // The tunnel goes away on its own: agvd notices and connects again a few seconds later.
        scenario.respond(QStringLiteral("status"), fixture(QStringLiteral("status-disconnected.txt")));
        vpn.refreshStatus();
        QTRY_VERIFY(vpn.reconnecting());
        QVERIFY(vpn.wanted());
        scenario.respond(QStringLiteral("status"), fixture(QStringLiteral("status-connected.txt")));
        QTRY_VERIFY_WITH_TIMEOUT(!vpn.reconnecting(), 10000);
        QCOMPARE(vpn.state(), VpnService::State::Connected);
        QVERIFY(scenario.calls().contains(QStringLiteral("connect|-y|--no-progress")));

        // An explicit disconnect is not a drop.
        scenario.respond(QStringLiteral("status"), fixture(QStringLiteral("status-disconnected.txt")));
        std::optional<Error> error;
        vpn.disconnectVpn([&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QCOMPARE(vpn.state(), VpnService::State::Disconnected);
        QVERIFY(!vpn.wanted());
        QVERIFY(!vpn.reconnecting());
    }

    void failedConnectDoesNotRetry()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("status"), fixture(QStringLiteral("status-disconnected.txt")));
        scenario.respond(QStringLiteral("connect_-y_--no-progress"), QStringLiteral("Error: no network\n"), 1);
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);
        std::optional<Error> error;
        vpn.connectTo(QString(), [&](const Error &e) { error = e; });
        QTRY_VERIFY(error.has_value());
        QVERIFY(!error->isOk());
        QVERIFY(!vpn.wanted());
        QVERIFY(!vpn.reconnecting());
    }

    void strictFormatOnAdd()
    {
        Scenario scenario;
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);
        std::optional<Error> error;
        vpn.addSiteExclusions({QStringLiteral("*domain.net")}, [&](const Error &e) { error = e; });
        QCOMPARE(error->name, errors::InvalidArgument);
        error.reset();
        vpn.addSiteExclusions({QStringLiteral("ru")}, [&](const Error &e) { error = e; });
        QCOMPARE(error->name, errors::InvalidArgument);
        QVERIFY(scenario.calls().isEmpty());
    }

    void invalidInputNeverReachesCli()
    {
        Scenario scenario;
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);

        std::optional<Error> error;
        vpn.addSiteExclusions({QStringLiteral("ok.com"), QStringLiteral("--help")},
                              [&](const Error &e) { error = e; });
        QCOMPARE(error->name, errors::InvalidArgument);

        error.reset();
        vpn.connectTo(QStringLiteral("-f"), [&](const Error &e) { error = e; });
        QCOMPARE(error->name, errors::InvalidArgument);

        error.reset();
        vpn.setConfig(QStringLiteral("tun-routing-mode"), QStringLiteral("script"),
                      [&](const Error &e) { error = e; });
        QCOMPARE(error->name, errors::InvalidArgument);

        QVERIFY(scenario.calls().isEmpty());
    }

    void exclusionValidation_data()
    {
        QTest::addColumn<QString>("item");
        QTest::addColumn<bool>("valid");
        QTest::newRow("domain") << QStringLiteral("example.com") << true;
        QTest::newRow("wildcard") << QStringLiteral("*.example.com") << true;
        QTest::newRow("idn") << QStringLiteral("пример.рф") << true;
        QTest::newRow("ipv4 port") << QStringLiteral("8.211.26.21:22102") << true;
        QTest::newRow("ipv6") << QStringLiteral("[2001:db8::1]:443") << true;
        QTest::newRow("cidr") << QStringLiteral("8.211.0.0/16") << true;
        QTest::newRow("ipv6 cidr") << QStringLiteral("2001:db8::/32") << true;
        QTest::newRow("slash first") << QStringLiteral("/etc/passwd") << false;
        QTest::newRow("option") << QStringLiteral("--help") << false;
        QTest::newRow("space") << QStringLiteral("a.com b.com") << false;
        QTest::newRow("empty") << QString() << false;
    }

    void exclusionValidation()
    {
        QFETCH(QString, item);
        QFETCH(bool, valid);
        QCOMPARE(VpnService::isValidExclusion(item), valid);
    }

    void configValidation_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("valid");
        QTest::newRow("protocol") << QStringLiteral("protocol") << QStringLiteral("quic") << true;
        QTest::newRow("bad protocol") << QStringLiteral("protocol") << QStringLiteral("tcp") << false;
        QTest::newRow("on/off") << QStringLiteral("post-quantum") << QStringLiteral("off") << true;
        QTest::newRow("bool word") << QStringLiteral("post-quantum") << QStringLiteral("true") << false;
        QTest::newRow("port") << QStringLiteral("socks-port") << QStringLiteral("1080") << true;
        QTest::newRow("port zero") << QStringLiteral("socks-port") << QStringLiteral("0") << false;
        QTest::newRow("port big") << QStringLiteral("socks-port") << QStringLiteral("70000") << false;
        QTest::newRow("dns") << QStringLiteral("dns") << QStringLiteral("tls://1.1.1.1") << true;
        QTest::newRow("dns option") << QStringLiteral("dns") << QStringLiteral("-x") << false;
        QTest::newRow("iface clear") << QStringLiteral("bound-if-override") << QString() << true;
        QTest::newRow("iface") << QStringLiteral("wlan0") << QStringLiteral("x") << false;
        QTest::newRow("routing") << QStringLiteral("tun-routing-mode") << QStringLiteral("auto") << false;
        QTest::newRow("ports") << QStringLiteral("exclusions-scannable-ports") << QStringLiteral("443,80") << true;
    }

    void configValidation()
    {
        QFETCH(QString, key);
        QFETCH(QString, value);
        QFETCH(bool, valid);
        QCOMPARE(VpnService::isValidConfig(key, value), valid);
    }

    void loginDeliversUrl()
    {
        Scenario scenario;
        scenario.respond(QStringLiteral("login"), fixture(QStringLiteral("login-prompt.txt")));
        scenario.respond(QStringLiteral("status"), fixture(QStringLiteral("status-disconnected.txt")));
        CliRunner cli(FakeCli, scenario.home());
        VpnService vpn(&cli);
        QSignalSpy url(&vpn, &VpnService::loginUrlReady);
        QSignalSpy finished(&vpn, &VpnService::loginFinished);

        std::optional<Error> error;
        vpn.startLogin([&](const Error &e) { error = e; });
        QVERIFY(error && error->isOk());
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(url.count(), 1);
        QCOMPARE(url.at(0).at(0).toString(),
                 QStringLiteral("https://auth.example.com/device_code?user_code=ABCD-EFGH"));
        QCOMPARE(finished.at(0).at(0).toBool(), true);
        QTRY_COMPARE(vpn.state(), VpnService::State::Disconnected);
    }
};

QTEST_GUILESS_MAIN(TestVpnService)
#include "tst_vpnservice.moc"
