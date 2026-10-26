#include "siteexclusion.h"

#include <QTest>

using namespace agv;

class TestSiteExclusion : public QObject
{
    Q_OBJECT

private slots:
    void valid_data()
    {
        QTest::addColumn<QString>("entry");
        QTest::addColumn<bool>("valid");

        // What the CLI accepts
        QTest::newRow("domain") << "example.com" << true;
        QTest::newRow("www") << "www.example.com" << true;
        QTest::newRow("wildcard") << "*.example.com" << true;
        QTest::newRow("zone") << "*.ru" << true;
        QTest::newRow("deep") << "a.b.c.example.co.uk" << true;
        QTest::newRow("punycode") << "xn--e1afmkfd.xn--p1ai" << true;
        QTest::newRow("hyphen") << "my-site.example" << true;
        QTest::newRow("ipv4") << "8.211.26.21" << true;
        QTest::newRow("ipv4 port") << "8.211.26.21:22102" << true;
        QTest::newRow("ipv6") << "2001:db8::1" << true;
        QTest::newRow("ipv6 brackets") << "[2001:db8::1]" << true;
        QTest::newRow("ipv6 port") << "[2001:db8::1]:443" << true;
        QTest::newRow("cidr4") << "8.211.0.0/16" << true;
        QTest::newRow("cidr6") << "2001:db8::/32" << true;

        // What it does not
        QTest::newRow("star no dot") << "*example.com" << false;
        QTest::newRow("star inside") << "a*.example.com" << false;
        QTest::newRow("star end") << "example.*" << false;
        QTest::newRow("double star") << "*.*.example.com" << false;
        QTest::newRow("bare zone") << "ru" << false;
        QTest::newRow("empty label") << "example..com" << false;
        QTest::newRow("leading hyphen") << "-example.com" << false;
        QTest::newRow("underscore") << "ex_ample.com" << false;
        QTest::newRow("upper case") << "Example.com" << false;
        QTest::newRow("bad octet") << "8.211.26.256" << false;
        QTest::newRow("numeric tld") << "example.123" << false;
        QTest::newRow("port zero") << "8.8.8.8:0" << false;
        QTest::newRow("port big") << "8.8.8.8:70000" << false;
        QTest::newRow("mask big") << "8.0.0.0/33" << false;
        QTest::newRow("domain port") << "example.com:443" << false;
        QTest::newRow("path") << "example.com/page" << false;
        QTest::newRow("option") << "--help" << false;
        QTest::newRow("space") << "a b.com" << false;
    }

    void valid()
    {
        QFETCH(QString, entry);
        QFETCH(bool, valid);
        QCOMPARE(site::isValid(entry), valid);
    }

    void parseNormalizes_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("value");
        QTest::newRow("case") << "  Example.COM " << "example.com";
        QTest::newRow("url") << "https://www.Example.com/page?q=1#x" << "www.example.com";
        QTest::newRow("url with port") << "https://example.com:8443/" << "example.com";
        QTest::newRow("ip url with port") << "http://8.211.26.21:22102/x" << "8.211.26.21:22102";
        QTest::newRow("trailing dot") << "example.com." << "example.com";
        QTest::newRow("zone with dot") << ".ru" << "*.ru";
        QTest::newRow("idn") << "пример.рф" << "xn--e1afmkfd.xn--p1ai";
        QTest::newRow("idn wildcard") << "*.пример.рф" << "*.xn--e1afmkfd.xn--p1ai";
        QTest::newRow("idn zone") << ".рф" << "*.xn--p1ai";
    }

    void parseNormalizes()
    {
        QFETCH(QString, input);
        QFETCH(QString, value);
        const auto parsed = site::parse(input);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error));
        QCOMPARE(parsed.value, value);
    }

    void parseExplains()
    {
        auto parsed = site::parse(QStringLiteral("*domain.net"));
        QCOMPARE(parsed.problem, site::Problem::StarWithoutDot);
        QCOMPARE(parsed.suggestion, QStringLiteral("*.domain.net"));

        parsed = site::parse(QStringLiteral("ru"));
        QCOMPARE(parsed.problem, site::Problem::BareZone);
        QCOMPARE(parsed.suggestion, QStringLiteral("*.ru"));

        parsed = site::parse(QStringLiteral("a*.b.com"));
        QCOMPARE(parsed.problem, site::Problem::StarInside);

        parsed = site::parse(QStringLiteral("not a site"));
        QVERIFY(!parsed.ok());
    }

    void parseList()
    {
        const auto list = site::parseList(QStringLiteral("a.com, b.org;\n*.c.net  bad*"));
        QCOMPARE(list.size(), 4);
        QVERIFY(list[0].ok() && list[1].ok() && list[2].ok());
        QVERIFY(!list[3].ok());
    }

    void expand()
    {
        QCOMPARE(site::expand(site::parse(QStringLiteral("domain.net")), true),
                 QStringList({QStringLiteral("domain.net"), QStringLiteral("*.domain.net")}));
        QCOMPARE(site::expand(site::parse(QStringLiteral("domain.net")), false),
                 QStringList({QStringLiteral("domain.net")}));
        QCOMPARE(site::expand(site::parse(QStringLiteral("www.domain.net")), true),
                 QStringList({QStringLiteral("www.domain.net"), QStringLiteral("*.domain.net")}));
        QCOMPARE(site::expand(site::parse(QStringLiteral("*.domain.net")), true),
                 QStringList({QStringLiteral("*.domain.net")}));
        QCOMPARE(site::expand(site::parse(QStringLiteral("8.8.8.8")), true), QStringList({QStringLiteral("8.8.8.8")}));
        QVERIFY(site::expand(site::parse(QStringLiteral("bad*")), true).isEmpty());
    }

    void sortGroupsRelatedEntries()
    {
        QStringList entries{QStringLiteral("*.b.com"), QStringLiteral("8.8.8.8"), QStringLiteral("a.com"),
                            QStringLiteral("b.com"), QStringLiteral("*.a.com"), QStringLiteral("sub.a.com"),
                            QStringLiteral("*.ru")};
        std::sort(entries.begin(), entries.end(),
                  [](const QString &l, const QString &r) { return site::sortKey(l) < site::sortKey(r); });
        QCOMPARE(entries, QStringList({QStringLiteral("a.com"), QStringLiteral("*.a.com"), QStringLiteral("sub.a.com"),
                                       QStringLiteral("b.com"), QStringLiteral("*.b.com"), QStringLiteral("*.ru"),
                                       QStringLiteral("8.8.8.8")}));
    }
};

QTEST_GUILESS_MAIN(TestSiteExclusion)
#include "tst_siteexclusion.moc"
