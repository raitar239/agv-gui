#include "statistics.h"

#include <QJsonArray>
#include <QLocale>
#include <QTest>

using namespace agv;

namespace {

const QDateTime Now(QDate(2026, 10, 26), QTime(15, 30));

QJsonObject day(double in, double out, double seconds, const QString &location, const QString &app = {})
{
    QJsonObject result{{QStringLiteral("in"), in}, {QStringLiteral("out"), out},
                       {QStringLiteral("seconds"), seconds}, {QStringLiteral("connections"), 1},
                       {QStringLiteral("locations"), QJsonObject{{location, QJsonObject{{QStringLiteral("in"), in},
                                                                                       {QStringLiteral("out"), out},
                                                                                       {QStringLiteral("seconds"), seconds}}}}}};
    if (!app.isEmpty())
        result.insert(QStringLiteral("apps"), QJsonObject{{app, QJsonObject{{QStringLiteral("in"), 50}, {QStringLiteral("out"), 5}}}});
    return result;
}

QJsonObject report()
{
    const auto at = [](int daysAgo, int hour) {
        return double(QDateTime(Now.date().addDays(-daysAgo), QTime(hour, 0)).toSecsSinceEpoch());
    };
    return {
        {QStringLiteral("days"), QJsonObject{
            {QStringLiteral("2026-10-26"), day(300, 30, 600, QStringLiteral("TOKYO"), QStringLiteral("app-00000001"))},
            {QStringLiteral("2026-10-24"), day(1000, 100, 3600, QStringLiteral("FRANKFURT"))},
            {QStringLiteral("2026-09-01"), day(5000, 500, 7200, QStringLiteral("FRANKFURT"))},
        }},
        {QStringLiteral("hours"), QJsonObject{{QString::number(qint64(at(0, 14))),
                                               QJsonObject{{QStringLiteral("in"), 300}, {QStringLiteral("out"), 30}}}}},
        {QStringLiteral("sessions"), QJsonArray{
            QJsonObject{{QStringLiteral("start"), at(64, 9)}, {QStringLiteral("end"), at(64, 11)}, {QStringLiteral("location"), QStringLiteral("FRANKFURT")}},
            QJsonObject{{QStringLiteral("start"), at(2, 9)}, {QStringLiteral("end"), at(2, 10)}, {QStringLiteral("location"), QStringLiteral("FRANKFURT")}},
        }},
        {QStringLiteral("current"), QJsonObject{{QStringLiteral("start"), at(0, 15)}, {QStringLiteral("location"), QStringLiteral("TOKYO")}}},
        {QStringLiteral("apps"), QJsonObject{{QStringLiteral("app-00000001"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Game")}}}}},
    };
}

} // namespace

class TestStatistics : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QLocale::setDefault(QLocale(QLocale::English)); }

    void today()
    {
        const QVariantMap stats = stats::summarize(report(), QStringLiteral("today"), Now);
        QCOMPARE(stats.value(QStringLiteral("in")).toDouble(), 300.0);
        const QVariantList chart = stats.value(QStringLiteral("chart")).toList();
        QCOMPARE(chart.size(), 24);
        QCOMPARE(chart.at(14).toMap().value(QStringLiteral("in")).toDouble(), 300.0);
        QCOMPARE(stats.value(QStringLiteral("peak")).toMap().value(QStringLiteral("bytes")).toDouble(), 330.0);
        // Only the session in progress started today; it lasts until now.
        const QVariantList sessions = stats.value(QStringLiteral("sessions")).toList();
        QCOMPARE(sessions.size(), 1);
        QVERIFY(sessions.first().toMap().value(QStringLiteral("current")).toBool());
        QCOMPARE(sessions.first().toMap().value(QStringLiteral("seconds")).toDouble(), 30.0 * 60);
        QCOMPARE(stats.value(QStringLiteral("apps")).toList().first().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Game"));
    }

    void week()
    {
        const QVariantMap stats = stats::summarize(report(), QStringLiteral("week"), Now);
        QCOMPARE(stats.value(QStringLiteral("in")).toDouble(), 1300.0);
        QCOMPARE(stats.value(QStringLiteral("connections")).toInt(), 2);
        QCOMPARE(stats.value(QStringLiteral("chart")).toList().size(), 7);
        QCOMPARE(stats.value(QStringLiteral("chart")).toList().last().toMap().value(QStringLiteral("label")).toString(),
                 QStringLiteral("Mon"));
        // Locations by connected time.
        const QVariantList locations = stats.value(QStringLiteral("locations")).toList();
        QCOMPARE(locations.first().toMap().value(QStringLiteral("location")).toString(), QStringLiteral("FRANKFURT"));
        QCOMPARE(stats.value(QStringLiteral("sessions")).toList().size(), 2);
    }

    void allTime()
    {
        const QVariantMap stats = stats::summarize(report(), QStringLiteral("all"), Now);
        QCOMPARE(stats.value(QStringLiteral("in")).toDouble(), 6300.0);
        // At least six months, ending with this one.
        const QVariantList chart = stats.value(QStringLiteral("chart")).toList();
        QCOMPARE(chart.size(), 6);
        QCOMPARE(chart.at(4).toMap().value(QStringLiteral("in")).toDouble(), 5000.0);
        QCOMPARE(chart.at(5).toMap().value(QStringLiteral("in")).toDouble(), 1300.0);
        QCOMPARE(stats.value(QStringLiteral("sessions")).toList().size(), 3);
    }

    void empty()
    {
        const QVariantMap stats = stats::summarize({}, QStringLiteral("month"), Now);
        QVERIFY(!stats.value(QStringLiteral("hasData")).toBool());
        QCOMPARE(stats.value(QStringLiteral("chart")).toList().size(), 30);
    }
};

QTEST_GUILESS_MAIN(TestStatistics)
#include "tst_statistics.moc"
