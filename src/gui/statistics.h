#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QVariantMap>

namespace agv::stats {

// Turns agvd's GetStatistics report into what the Statistics page shows for one period
// ("today", "week", "month" or "all"): totals, chart bars, locations, apps and sessions.
// Labels use the default QLocale; now is the local time the period ends at.
QVariantMap summarize(const QJsonObject &report, const QString &period, const QDateTime &now);

} // namespace agv::stats
