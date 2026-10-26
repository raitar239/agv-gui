#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>

#include <functional>
#include <optional>

class QTimer;

namespace agv {

// Traffic and connection history for the Statistics page. Samples counters every few seconds and
// keeps per-day totals (traffic through the tunnel, connected time, locations, excluded apps),
// per-hour totals for the last two days and the latest sessions. Stays on this computer, in a
// root-only JSON file next to the other daemon data.
class StatsRecorder : public QObject
{
    Q_OBJECT

public:
    struct Bytes {
        quint64 in = 0;
        quint64 out = 0;
    };
    struct AppInfo {
        QString name;
        QString icon;
        qint64 ownerUid = -1;
    };
    // What the counters said at one moment.
    struct Sample {
        QDateTime time;
        bool connected = false;
        QString location;
        QString tunDevice;
        std::optional<Bytes> tunnel;        // the tunnel's counters, when there is a tunnel
        QMap<QString, Bytes> apps;          // app id → counters (nftables)
        QHash<QString, AppInfo> appInfo;    // names for the ids above
    };

    explicit StatsRecorder(QString path, QObject *parent = nullptr);
    ~StatsRecorder() override;

    QString load();
    QString save();

    // Polls source every interval and saves now and then; source is called on the event loop.
    void start(std::function<Sample()> source, int intervalMs = 5000);
    // Takes a sample right away (state changes, so sessions start and end on time).
    void sampleNow();

    void record(const Sample &sample);
    // Everything the caller may see: apps of other users are left out.
    QJsonObject report(qint64 callerUid) const;
    void reset();

private:
    struct Totals {
        Bytes bytes;
        qint64 seconds = 0;
    };
    struct Day {
        Totals vpn;
        int connections = 0;
        QHash<QString, Totals> locations;
        QHash<QString, Bytes> apps;
    };
    struct Session {
        QDateTime start;
        QDateTime end;  // invalid while it lasts
        QString location;
        Totals totals;
    };

    void closeSession(const QDateTime &at);
    QJsonObject toJson(qint64 callerUid, bool forFile) const;

    QString m_path;
    QMap<QString, Day> m_days;          // "yyyy-MM-dd" (local time)
    QMap<qint64, Totals> m_hours;       // hour start, seconds since the epoch
    QList<Session> m_sessions;          // oldest first
    std::optional<Session> m_current;
    QHash<QString, AppInfo> m_appInfo;
    QDateTime m_since;

    // Last sample, to turn counters into deltas.
    QDateTime m_lastTime;
    std::optional<Bytes> m_lastTunnel;
    QString m_lastTunDevice;
    QMap<QString, Bytes> m_lastApps;

    bool m_dirty = false;
    std::function<Sample()> m_source;
    QTimer *m_timer = nullptr;
    QTimer *m_saveTimer = nullptr;
};

} // namespace agv
