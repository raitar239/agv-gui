#include "statsrecorder.h"

#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

using namespace agv;

namespace {

const QDateTime Start = QDateTime(QDate(2026, 10, 26), QTime(12, 0)).toUTC();

StatsRecorder::Sample connectedAt(int seconds, quint64 in, quint64 out, const QString &location = QStringLiteral("FRANKFURT"))
{
    StatsRecorder::Sample sample;
    sample.time = Start.addSecs(seconds);
    sample.connected = true;
    sample.location = location;
    sample.tunDevice = QStringLiteral("tun0");
    sample.tunnel = StatsRecorder::Bytes{in, out};
    return sample;
}

StatsRecorder::Sample disconnectedAt(int seconds)
{
    StatsRecorder::Sample sample;
    sample.time = Start.addSecs(seconds);
    return sample;
}

QJsonObject today(const QJsonObject &report)
{
    return report.value(QStringLiteral("days")).toObject().value(Start.toLocalTime().date().toString(Qt::ISODate)).toObject();
}

double number(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toDouble();
}

} // namespace

class TestStats : public QObject
{
    Q_OBJECT

private slots:
    void countsTunnelTrafficAndTime()
    {
        QTemporaryDir dir;
        StatsRecorder stats(dir.path() + QStringLiteral("/stats.json"));
        stats.record(connectedAt(0, 1000, 500));       // first sample: only a baseline
        stats.record(connectedAt(5, 3000, 700));
        stats.record(connectedAt(10, 4000, 900));

        const QJsonObject report = stats.report(-1);
        const QJsonObject day = today(report);
        QCOMPARE(number(day, "in"), 3000.0);
        QCOMPARE(number(day, "out"), 400.0);
        QCOMPARE(number(day, "seconds"), 10.0);
        QCOMPARE(number(day, "connections"), 1.0);
        const QJsonObject frankfurt = day.value(QStringLiteral("locations")).toObject().value(QStringLiteral("FRANKFURT")).toObject();
        QCOMPARE(number(frankfurt, "in"), 3000.0);

        const QJsonObject current = report.value(QStringLiteral("current")).toObject();
        QCOMPARE(current.value(QStringLiteral("location")).toString(), QStringLiteral("FRANKFURT"));
        QCOMPARE(number(current, "seconds"), 10.0);
        QCOMPARE(report.value(QStringLiteral("hours")).toObject().size(), 1);
    }

    void sessionsEndAndSwitchLocation()
    {
        QTemporaryDir dir;
        StatsRecorder stats(dir.path() + QStringLiteral("/stats.json"));
        stats.record(connectedAt(0, 0, 0));
        stats.record(connectedAt(5, 100, 10));
        stats.record(connectedAt(10, 200, 20, QStringLiteral("TOKYO")));   // reconnected elsewhere
        stats.record(connectedAt(15, 300, 30, QStringLiteral("TOKYO")));
        stats.record(disconnectedAt(20));

        const QJsonObject report = stats.report(-1);
        QVERIFY(!report.contains(QStringLiteral("current")));
        const QJsonArray sessions = report.value(QStringLiteral("sessions")).toArray();
        QCOMPARE(sessions.size(), 2);
        QCOMPARE(sessions[0].toObject().value(QStringLiteral("location")).toString(), QStringLiteral("FRANKFURT"));
        QCOMPARE(sessions[1].toObject().value(QStringLiteral("location")).toString(), QStringLiteral("TOKYO"));
        QCOMPARE(number(sessions[1].toObject(), "in"), 200.0);
        QCOMPARE(number(sessions[1].toObject(), "end") - number(sessions[1].toObject(), "start"), 10.0);
        QCOMPARE(number(today(report), "connections"), 2.0);
    }

    void survivesCounterResetsAndSleep()
    {
        QTemporaryDir dir;
        StatsRecorder stats(dir.path() + QStringLiteral("/stats.json"));
        stats.record(connectedAt(0, 5000, 5000));
        stats.record(connectedAt(5, 100, 50));          // tunnel recreated: counters restarted
        stats.record(connectedAt(3600, 200, 60));       // an hour asleep: no connected time for it
        const QJsonObject day = today(stats.report(-1));
        QCOMPARE(number(day, "in"), 200.0);
        QCOMPARE(number(day, "out"), 60.0);
        QCOMPARE(number(day, "seconds"), 5.0);
    }

    void appsBelongToTheirOwners()
    {
        QTemporaryDir dir;
        StatsRecorder stats(dir.path() + QStringLiteral("/stats.json"));
        auto sample = disconnectedAt(0);
        sample.apps = {{QStringLiteral("app-00000001"), {0, 0}}, {QStringLiteral("app-00000002"), {0, 0}}};
        sample.appInfo = {{QStringLiteral("app-00000001"), {QStringLiteral("Game"), QString(), 1000}},
                          {QStringLiteral("app-00000002"), {QStringLiteral("Other"), QString(), 1001}}};
        stats.record(sample);
        sample.time = Start.addSecs(5);
        sample.apps = {{QStringLiteral("app-00000001"), {700, 70}}, {QStringLiteral("app-00000002"), {10, 1}}};
        stats.record(sample);

        const QJsonObject mine = stats.report(1000);
        const QJsonObject apps = today(mine).value(QStringLiteral("apps")).toObject();
        QCOMPARE(apps.keys(), QStringList{QStringLiteral("app-00000001")});
        QCOMPARE(number(apps.value(QStringLiteral("app-00000001")).toObject(), "in"), 700.0);
        QCOMPARE(mine.value(QStringLiteral("apps")).toObject().value(QStringLiteral("app-00000001")).toObject()
                     .value(QStringLiteral("name")).toString(), QStringLiteral("Game"));
        QCOMPARE(today(stats.report(0)).value(QStringLiteral("apps")).toObject().size(), 2);
    }

    void socksCountsTheCli()
    {
        QTemporaryDir dir;
        StatsRecorder stats(dir.path() + QStringLiteral("/stats.json"));
        auto sample = connectedAt(0, 0, 0);
        sample.tunnel.reset();
        sample.tunDevice.clear();
        sample.apps = {{QStringLiteral("agvd"), {1000, 100}}, {QStringLiteral("app-00000001"), {0, 0}}};
        stats.record(sample);
        sample.time = Start.addSecs(5);
        sample.apps = {{QStringLiteral("agvd"), {5000, 300}}, {QStringLiteral("app-00000001"), {70, 7}}};
        stats.record(sample);

        const QJsonObject report = stats.report(-1);
        const QJsonObject day = today(report);
        QCOMPARE(number(day, "in"), 4000.0);
        QCOMPARE(number(day, "out"), 200.0);
        // agvd itself is not an app; real apps are kept per hour too.
        QCOMPARE(day.value(QStringLiteral("apps")).toObject().keys(), QStringList{QStringLiteral("app-00000001")});
        const QJsonObject hour = report.value(QStringLiteral("hours")).toObject().constBegin()->toObject();
        QCOMPARE(number(hour.value(QStringLiteral("apps")).toObject().value(QStringLiteral("app-00000001")).toObject(), "in"), 70.0);
    }

    void savesAndLoads()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/stats.json");
        {
            StatsRecorder stats(path);
            stats.record(connectedAt(0, 0, 0));
            stats.record(connectedAt(5, 100, 10));
            stats.record(disconnectedAt(10));       // closing a session saves
        }
        QVERIFY(QFile::exists(path));
        QCOMPARE(QFile(path).permissions() & QFileDevice::ReadOther, QFileDevice::Permissions());
        StatsRecorder loaded(path);
        QCOMPARE(loaded.load(), QString());
        const QJsonObject report = loaded.report(-1);
        QCOMPARE(number(today(report), "in"), 100.0);
        QCOMPARE(report.value(QStringLiteral("sessions")).toArray().size(), 1);

        loaded.reset();
        QVERIFY(loaded.report(-1).value(QStringLiteral("days")).toObject().isEmpty());
        StatsRecorder again(path);
        QCOMPARE(again.load(), QString());
        QVERIFY(again.report(-1).value(QStringLiteral("sessions")).toArray().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestStats)
#include "tst_stats.moc"
