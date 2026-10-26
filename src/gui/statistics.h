#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QVariantMap>

namespace agv::stats {

// Turns agvd's GetStatistics report into what the Statistics page shows for one period
// ("today", "week", "month" or "all"): totals, chart bars, locations, apps and sessions.
// Labels use the default QLocale; now is the local time the period ends at. With appId the
// chart shows that app's traffic instead of the VPN's.
QVariantMap summarize(const QJsonObject &report, const QString &period, const QDateTime &now,
                      const QString &appId = {});

// Every day, location, app and session as CSV rows, for spreadsheets.
QByteArray toCsv(const QJsonObject &report);

} // namespace agv::stats
