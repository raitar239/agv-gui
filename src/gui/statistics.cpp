#include "statistics.h"

#include <QHash>
#include <QJsonArray>
#include <QLocale>
#include <QVariantList>

#include <algorithm>

namespace agv::stats {

namespace {

struct Totals {
    double in = 0;
    double out = 0;
    double seconds = 0;

    void add(const QJsonObject &object)
    {
        in += object.value(QStringLiteral("in")).toDouble();
        out += object.value(QStringLiteral("out")).toDouble();
        seconds += object.value(QStringLiteral("seconds")).toDouble();
    }
};

QVariantMap bar(const QString &label, const QString &tip, const Totals &totals, bool showLabel)
{
    return {{QStringLiteral("label"), label},
            {QStringLiteral("tip"), tip},
            {QStringLiteral("in"), totals.in},
            {QStringLiteral("out"), totals.out},
            {QStringLiteral("seconds"), totals.seconds},
            {QStringLiteral("showLabel"), showLabel}};
}

QString capitalized(QString text)
{
    if (!text.isEmpty())
        text[0] = text[0].toUpper();
    return text;
}

} // namespace

QVariantMap summarize(const QJsonObject &report, const QString &period, const QDateTime &now, const QString &appId)
{
    // What a chart bar counts: the VPN's traffic in a day or hour, or one app's.
    const auto chartPart = [&appId](const QJsonObject &bucket) {
        return appId.isEmpty() ? bucket
                               : bucket.value(QStringLiteral("apps")).toObject().value(appId).toObject();
    };
    const QLocale locale;
    const QDate today = now.date();
    const QJsonObject days = report.value(QStringLiteral("days")).toObject();

    QDate first = today;
    if (period == QLatin1String("week"))
        first = today.addDays(-6);
    else if (period == QLatin1String("month"))
        first = today.addDays(-29);
    else if (period == QLatin1String("all"))
        first = days.isEmpty() ? today : std::min(today, QDate::fromString(days.keys().first(), Qt::ISODate));

    // Totals, locations and apps over the period's days.
    Totals total;
    int connections = 0;
    QHash<QString, Totals> locations;
    QHash<QString, Totals> apps;
    for (auto it = days.constBegin(); it != days.constEnd(); ++it) {
        const QDate date = QDate::fromString(it.key(), Qt::ISODate);
        if (!date.isValid() || date < first || date > today)
            continue;
        const QJsonObject day = it->toObject();
        total.add(day);
        connections += day.value(QStringLiteral("connections")).toInt();
        const QJsonObject dayLocations = day.value(QStringLiteral("locations")).toObject();
        for (auto location = dayLocations.constBegin(); location != dayLocations.constEnd(); ++location)
            locations[location.key()].add(location->toObject());
        const QJsonObject dayApps = day.value(QStringLiteral("apps")).toObject();
        for (auto app = dayApps.constBegin(); app != dayApps.constEnd(); ++app)
            apps[app.key()].add(app->toObject());
    }

    // Chart: hours of today, days of the week or month, months of all time.
    QVariantList chart;
    if (period == QLatin1String("today")) {
        Totals hours[24];
        const QJsonObject hourly = report.value(QStringLiteral("hours")).toObject();
        for (auto it = hourly.constBegin(); it != hourly.constEnd(); ++it) {
            const QDateTime start = QDateTime::fromSecsSinceEpoch(it.key().toLongLong()).toLocalTime();
            if (start.date() == today)
                hours[start.time().hour()].add(chartPart(it->toObject()));
        }
        for (int hour = 0; hour < 24; ++hour) {
            const QString from = locale.toString(QTime(hour, 0), QLocale::ShortFormat);
            const QString to = locale.toString(QTime((hour + 1) % 24, 0), QLocale::ShortFormat);
            chart.append(bar(QString::number(hour), QStringLiteral("%1–%2").arg(from, to), hours[hour], hour % 6 == 0));
        }
    } else if (period == QLatin1String("week") || period == QLatin1String("month")) {
        const bool week = period == QLatin1String("week");
        for (QDate date = first; date <= today; date = date.addDays(1)) {
            Totals totals;
            totals.add(chartPart(days.value(date.toString(Qt::ISODate)).toObject()));
            const QString label = week ? capitalized(locale.dayName(date.dayOfWeek(), QLocale::ShortFormat))
                                       : QString::number(date.day());
            const bool showLabel = week || date == today || first.daysTo(date) % 5 == 0;
            chart.append(bar(label, locale.toString(date, QStringLiteral("d MMMM")), totals, showLabel));
        }
    } else {
        // At most the last 12 months, and at least six so a new install still looks like a chart.
        QDate month(today.year(), today.month(), 1);
        QDate start(first.year(), first.month(), 1);
        start = std::clamp(start, month.addMonths(-11), month.addMonths(-5));
        for (QDate date = start; date <= month; date = date.addMonths(1)) {
            Totals totals;
            for (QDate day = date; day.month() == date.month() && day <= today; day = day.addDays(1))
                totals.add(chartPart(days.value(day.toString(Qt::ISODate)).toObject()));
            const QString label = capitalized(locale.standaloneMonthName(date.month(), QLocale::ShortFormat));
            const QString tip = capitalized(locale.standaloneMonthName(date.month())) + QLatin1Char(' ')
                                + QString::number(date.year());
            chart.append(bar(label, tip, totals, true));
        }
    }
    double chartMax = 0;
    QVariantMap peak;
    for (const QVariant &entry : std::as_const(chart)) {
        const QVariantMap map = entry.toMap();
        const double bytes = map.value(QStringLiteral("in")).toDouble() + map.value(QStringLiteral("out")).toDouble();
        if (bytes > chartMax) {
            chartMax = bytes;
            peak = {{QStringLiteral("tip"), map.value(QStringLiteral("tip"))}, {QStringLiteral("bytes"), bytes}};
        }
    }

    QVariantList locationList;
    for (auto it = locations.cbegin(); it != locations.cend(); ++it) {
        if (it.key().isEmpty())
            continue;
        locationList.append(QVariantMap{{QStringLiteral("location"), it.key()},
                                        {QStringLiteral("in"), it->in},
                                        {QStringLiteral("out"), it->out},
                                        {QStringLiteral("seconds"), it->seconds}});
    }
    std::sort(locationList.begin(), locationList.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("seconds")).toDouble() > b.toMap().value(QStringLiteral("seconds")).toDouble();
    });

    const QJsonObject appInfo = report.value(QStringLiteral("apps")).toObject();
    QVariantList appList;
    Totals appsTotal;
    for (auto it = apps.cbegin(); it != apps.cend(); ++it) {
        const QJsonObject info = appInfo.value(it.key()).toObject();
        appsTotal.in += it->in;
        appsTotal.out += it->out;
        appList.append(QVariantMap{{QStringLiteral("id"), it.key()},
                                   {QStringLiteral("name"), info.value(QStringLiteral("name")).toString(it.key())},
                                   {QStringLiteral("icon"), info.value(QStringLiteral("icon")).toString()},
                                   {QStringLiteral("in"), it->in},
                                   {QStringLiteral("out"), it->out}});
    }
    const auto bytesOf = [](const QVariant &entry) {
        const QVariantMap map = entry.toMap();
        return map.value(QStringLiteral("in")).toDouble() + map.value(QStringLiteral("out")).toDouble();
    };
    std::sort(appList.begin(), appList.end(),
              [&bytesOf](const QVariant &a, const QVariant &b) { return bytesOf(a) > bytesOf(b); });

    // Sessions that started in the period, newest first; the one in progress on top.
    QJsonArray sessions = report.value(QStringLiteral("sessions")).toArray();
    const bool live = report.contains(QStringLiteral("current"));
    if (live)
        sessions.append(report.value(QStringLiteral("current")));
    QVariantList sessionList;
    const QDateTime periodStart = first.startOfDay();
    for (qsizetype i = sessions.size() - 1; i >= 0 && sessionList.size() < 20; --i) {
        const QJsonObject session = sessions.at(i).toObject();
        const QDateTime start = QDateTime::fromSecsSinceEpoch(qint64(session.value(QStringLiteral("start")).toDouble())).toLocalTime();
        if (period != QLatin1String("all") && start < periodStart)
            continue;
        const bool current = live && i == sessions.size() - 1;
        const QDateTime end = current ? now
                                      : QDateTime::fromSecsSinceEpoch(qint64(session.value(QStringLiteral("end")).toDouble())).toLocalTime();
        const QString when = start.date() == today
            ? locale.toString(start.time(), QLocale::ShortFormat)
            : locale.toString(start.date(), QStringLiteral("d MMM")) + QStringLiteral(", ")
                  + locale.toString(start.time(), QLocale::ShortFormat);
        sessionList.append(QVariantMap{{QStringLiteral("when"), when},
                                       {QStringLiteral("location"), session.value(QStringLiteral("location")).toString()},
                                       {QStringLiteral("seconds"), double(std::max<qint64>(0, start.secsTo(end)))},
                                       {QStringLiteral("in"), session.value(QStringLiteral("in")).toDouble()},
                                       {QStringLiteral("out"), session.value(QStringLiteral("out")).toDouble()},
                                       {QStringLiteral("current"), current}});
    }

    QString since;
    if (report.contains(QStringLiteral("since"))) {
        const QDate date = QDateTime::fromSecsSinceEpoch(qint64(report.value(QStringLiteral("since")).toDouble())).toLocalTime().date();
        since = locale.toString(date, QStringLiteral("d MMMM yyyy"));
    }

    return {{QStringLiteral("in"), total.in},
            {QStringLiteral("out"), total.out},
            {QStringLiteral("seconds"), total.seconds},
            {QStringLiteral("connections"), connections},
            {QStringLiteral("chart"), chart},
            {QStringLiteral("chartMax"), chartMax},
            {QStringLiteral("peak"), peak},
            {QStringLiteral("locations"), locationList},
            {QStringLiteral("apps"), appList},
            {QStringLiteral("appsIn"), appsTotal.in},
            {QStringLiteral("appsOut"), appsTotal.out},
            {QStringLiteral("sessions"), sessionList},
            {QStringLiteral("since"), since},
            {QStringLiteral("hasData"), total.in + total.out + total.seconds + appsTotal.in + appsTotal.out > 0
                                            || !sessionList.isEmpty()}};
}

QByteArray toCsv(const QJsonObject &report)
{
    const auto field = [](QString text) {
        if (text.contains(QLatin1Char(',')) || text.contains(QLatin1Char('"')) || text.contains(QLatin1Char('\n')))
            text = QLatin1Char('"') + text.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
        return text;
    };
    const auto number = [](const QJsonObject &object, const char *key) {
        return QString::number(qint64(object.value(QLatin1String(key)).toDouble()));
    };
    const QJsonObject appInfo = report.value(QStringLiteral("apps")).toObject();

    QStringList rows{QStringLiteral("date,type,name,download_bytes,upload_bytes,seconds,connections")};
    const auto row = [&](const QString &date, const QString &type, const QString &name, const QJsonObject &values,
                         const QString &connections = {}) {
        rows.append(QStringList{date, type, field(name), number(values, "in"), number(values, "out"),
                                number(values, "seconds"), connections}.join(QLatin1Char(',')));
    };

    const QJsonObject days = report.value(QStringLiteral("days")).toObject();
    for (auto it = days.constBegin(); it != days.constEnd(); ++it) {
        const QJsonObject day = it->toObject();
        row(it.key(), QStringLiteral("vpn"), QString(), day, number(day, "connections"));
        const QJsonObject locations = day.value(QStringLiteral("locations")).toObject();
        for (auto location = locations.constBegin(); location != locations.constEnd(); ++location)
            row(it.key(), QStringLiteral("location"), location.key(), location->toObject());
        const QJsonObject apps = day.value(QStringLiteral("apps")).toObject();
        for (auto app = apps.constBegin(); app != apps.constEnd(); ++app) {
            const QString name = appInfo.value(app.key()).toObject().value(QStringLiteral("name")).toString(app.key());
            row(it.key(), QStringLiteral("app"), name, app->toObject());
        }
    }
    for (const QJsonValue &value : report.value(QStringLiteral("sessions")).toArray()) {
        const QJsonObject session = value.toObject();
        const QDateTime start = QDateTime::fromSecsSinceEpoch(qint64(session.value(QStringLiteral("start")).toDouble()));
        row(start.toString(Qt::ISODate), QStringLiteral("session"), session.value(QStringLiteral("location")).toString(), session);
    }
    return (rows.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
}

} // namespace agv::stats
