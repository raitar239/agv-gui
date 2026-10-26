#include "cliupdates.h"

#include <QTest>

using namespace agv;

class TestCliUpdates : public QObject
{
    Q_OBJECT

private slots:
    void tags()
    {
        QCOMPARE(CliUpdates::versionFromTag(QStringLiteral("v1.8.1-release")), QStringLiteral("1.8.1"));
        QCOMPARE(CliUpdates::versionFromTag(QStringLiteral("v1.10.0")), QStringLiteral("1.10.0"));
        QVERIFY(CliUpdates::versionFromTag(QStringLiteral("v1.9.0-beta")).isEmpty());
        QVERIFY(CliUpdates::versionFromTag(QStringLiteral("nightly")).isEmpty());
    }

    void newer()
    {
        QVERIFY(CliUpdates::isNewer(QStringLiteral("1.8.1"), QStringLiteral("1.10.0")));
        QVERIFY(!CliUpdates::isNewer(QStringLiteral("1.8.1"), QStringLiteral("1.8.1")));
        QVERIFY(!CliUpdates::isNewer(QStringLiteral("1.9.0"), QStringLiteral("1.8.1")));
        QVERIFY(!CliUpdates::isNewer(QString(), QStringLiteral("1.8.1")));
    }
};

QTEST_GUILESS_MAIN(TestCliUpdates)
#include "tst_cliupdates.moc"
