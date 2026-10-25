#include "cliparser.h"

#include <QFile>
#include <QTest>

using namespace agv::cli;

namespace {

QString fixture(const QString &name)
{
    QFile file(QStringLiteral(AGV_FIXTURES_DIR "/1.8.1/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("cannot open fixture %s", qPrintable(file.fileName()));
    return QString::fromUtf8(file.readAll());
}

} // namespace

class TestCliParser : public QObject
{
    Q_OBJECT

private slots:
    void stripsAnsi()
    {
        QCOMPARE(stripAnsi(QStringLiteral("a \x1b[1mbold\x1b[0m b")), QStringLiteral("a bold b"));
    }

    void detectsUsageError()
    {
        QVERIFY(isUsageError(fixture(QStringLiteral("error-subcommand-required.txt"))));
        QVERIFY(!isUsageError(fixture(QStringLiteral("status-connected.txt"))));
    }

    void statusDisconnected()
    {
        const auto status = parseStatus(fixture(QStringLiteral("status-disconnected.txt")));
        QVERIFY(status);
        QCOMPARE(status->state, Status::State::Disconnected);
        QVERIFY(status->location.isEmpty());
    }

    void statusConnected()
    {
        const auto status = parseStatus(fixture(QStringLiteral("status-connected.txt")));
        QVERIFY(status);
        QCOMPARE(status->state, Status::State::Connected);
        QCOMPARE(status->location, QStringLiteral("ATHENS"));
        QCOMPARE(status->mode, QStringLiteral("TUN"));
        QCOMPARE(status->tunDevice, QStringLiteral("tun0"));
    }

    void statusConnectedWithAnsiAndSocks()
    {
        const auto status = parseStatus(QStringLiteral(
            "Connected to \x1b[1mNEW YORK\x1b[0m in \x1b[1mSOCKS\x1b[0m mode\n"));
        QVERIFY(status);
        QCOMPARE(status->location, QStringLiteral("NEW YORK"));
        QCOMPARE(status->mode, QStringLiteral("SOCKS"));
        QVERIFY(status->tunDevice.isEmpty());
    }

    void notLoggedIn()
    {
        const auto status = parseStatus(fixture(QStringLiteral("status-not-logged-in.txt")));
        QVERIFY(status);
        QCOMPARE(status->state, Status::State::NotLoggedIn);

        QVERIFY(isNotLoggedIn(ExitNotLoggedIn, QString()));
        QVERIFY(isNotLoggedIn(1, fixture(QStringLiteral("list-locations-not-logged-in.txt"))));
        QVERIFY(!isNotLoggedIn(0, fixture(QStringLiteral("status-connected.txt"))));
        QVERIFY(!parseLocations(fixture(QStringLiteral("list-locations-not-logged-in.txt"))));
    }

    void loginUrl()
    {
        QCOMPARE(parseLoginUrl(fixture(QStringLiteral("login-prompt.txt"))),
                 QStringLiteral("https://auth.example.com/device_code?user_code=ABCD-EFGH"));
        QVERIFY(parseLoginUrl(QStringLiteral("You need to authorize in your browser.")).isEmpty());
    }

    void statusUnknown()
    {
        QVERIFY(!parseStatus(QStringLiteral("Something new\n")));
    }

    void locations()
    {
        const auto locations = parseLocations(fixture(QStringLiteral("list-locations-5.txt")));
        QVERIFY(locations);
        QCOMPARE(locations->size(), 5);
        QCOMPARE(locations->at(0).iso, QStringLiteral("FR"));
        QCOMPARE(locations->at(0).country, QStringLiteral("France"));
        QCOMPARE(locations->at(0).city, QStringLiteral("Marseille"));
        QVERIFY(locations->at(0).ping.has_value());
        QCOMPARE(locations->at(1).city, QStringLiteral("Tallinn"));
        QCOMPARE(locations->at(4).city, QStringLiteral("Paris"));
    }

    void locationsEdgeCases()
    {
        // A city with digits in its name and a row without a ping.
        const QString text = QStringLiteral(
            "ISO   COUNTRY              CITY                           PING ESTIMATE\n"
            "XX    Testland             Area 51                        120       \n"
            "SG    Singapore            Singapore                                \n"
            "\n"
            "You can connect to a location by running ...\n");
        const auto locations = parseLocations(text);
        QVERIFY(locations);
        QCOMPARE(locations->size(), 2);
        QCOMPARE(locations->at(0).city, QStringLiteral("Area 51"));
        QCOMPARE(locations->at(0).ping, 120);
        QCOMPARE(locations->at(1).city, QStringLiteral("Singapore"));
        QVERIFY(!locations->at(1).ping.has_value());
    }

    void locationsAll()
    {
        const auto locations = parseLocations(fixture(QStringLiteral("list-locations-all.txt")));
        QVERIFY(locations);
        QCOMPARE(locations->size(), 80);

        bool sawVirtual = false;
        bool sawMultiWordCountry = false;
        for (const Location &location : *locations) {
            QCOMPARE(location.iso.size(), 2);
            QVERIFY(!location.country.isEmpty());
            QVERIFY(!location.city.isEmpty());
            QVERIFY(location.ping.has_value());
            sawVirtual |= location.city == QLatin1String("Moscow (Virtual)");
            sawMultiWordCountry |= location.country == QLatin1String("United Kingdom");
        }
        QVERIFY(sawVirtual);
        QVERIFY(sawMultiWordCountry);
    }

    void locationsRejectsGarbage()
    {
        QVERIFY(!parseLocations(fixture(QStringLiteral("error-subcommand-required.txt"))));
    }

    void config()
    {
        const auto config = parseConfig(fixture(QStringLiteral("config-show.txt")));
        QVERIFY(config);
        QCOMPARE(config->value(QStringLiteral("Mode")).value, QStringLiteral("tun"));
        QVERIFY(!config->value(QStringLiteral("Mode")).isDefault);
        QCOMPARE(config->value(QStringLiteral("Tunnel routing mode")).value, QStringLiteral("auto"));
        QCOMPARE(config->value(QStringLiteral("SOCKS port")).value, QStringLiteral("1080"));
        QCOMPARE(config->value(QStringLiteral("Data directory")).value,
                 QStringLiteral("/home/raitar/.local/share/adguardvpn-cli"));

        const auto postQuantum = config->value(QStringLiteral("Post-quantum cryptography"));
        QCOMPARE(postQuantum.value, QStringLiteral("on"));
        QVERIFY(postQuantum.isDefault);

        const auto override = config->value(QStringLiteral("Outbound interface override"));
        QVERIFY(override.value.isEmpty());
        QVERIFY(override.isDefault);

        QVERIFY(config->contains(QStringLiteral("SOCKS username")));
        QVERIFY(config->value(QStringLiteral("SOCKS username")).value.isEmpty());
    }

    void exclusionMode()
    {
        QCOMPARE(parseExclusionMode(fixture(QStringLiteral("site-exclusions-mode.txt"))),
                 ExclusionMode::General);
        QCOMPARE(parseExclusionMode(QStringLiteral("Current exclusion mode is \x1b[1mSELECTIVE\x1b[0m")),
                 ExclusionMode::Selective);
        QVERIFY(!parseExclusionMode(QStringLiteral("nonsense")));
    }

    void siteExclusions()
    {
        const auto exclusions = parseSiteExclusions(fixture(QStringLiteral("site-exclusions-show.txt")));
        QVERIFY(exclusions);
        QCOMPARE(exclusions->mode, ExclusionMode::General);
        QCOMPARE(exclusions->domains,
                 QStringList({QStringLiteral("*.ozon.ru"), QStringLiteral("*.ru"),
                              QStringLiteral("*.space"), QStringLiteral("ozon.ru")}));
    }

    void siteExclusionsEmpty()
    {
        const auto exclusions = parseSiteExclusions(fixture(QStringLiteral("site-exclusions-show-empty.txt")));
        QVERIFY(exclusions);
        QCOMPARE(exclusions->mode, ExclusionMode::General);
        QVERIFY(exclusions->domains.isEmpty());

        const auto selective = parseSiteExclusions(QStringLiteral("Exclusions for SELECTIVE mode:\n"));
        QVERIFY(selective);
        QCOMPARE(selective->mode, ExclusionMode::Selective);
    }

    void license()
    {
        const auto license = parseLicense(fixture(QStringLiteral("license-premium.txt")));
        QVERIFY(license);
        QCOMPARE(license->account, QStringLiteral("user@example.com"));
        QCOMPARE(license->plan, QStringLiteral("PREMIUM"));
        QCOMPARE(license->maxDevices, 10);
        QCOMPARE(license->validUntil, QDate(2027, 1, 11));
    }
};

QTEST_GUILESS_MAIN(TestCliParser)
#include "tst_cliparser.moc"
