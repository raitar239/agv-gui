#include "statsrecorder.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>

namespace agv {

namespace {

constexpr int FormatVersion = 1;
constexpr int MaxSessions = 200;
constexpr qint64 KeepHours = 48;
// A longer gap between samples means the computer slept: that time is not "connected".
constexpr qint64 MaxGapSeconds = 30;

// Counters only grow until their device or ruleset is recreated, then start again from zero.
quint64 delta(quint64 before, quint64 now)
{
    return now >= before ? now - before : now;
}

QJsonObject bytesJson(const StatsRecorder::Bytes &bytes)
{
    return {{QStringLiteral("in"), double(bytes.in)}, {QStringLiteral("out"), double(bytes.out)}};
}

StatsRecorder::Bytes bytesFrom(const QJsonObject &object)
{
    return {quint64(object.value(QStringLiteral("in")).toDouble()),
            quint64(object.value(QStringLiteral("out")).toDouble())};
}

void add(StatsRecorder::Bytes &target, const StatsRecorder::Bytes &more)
{
    target.in += more.in;
    target.out += more.out;
}

QString dayKey(const QDateTime &time)
{
    return time.toLocalTime().date().toString(Qt::ISODate);
}

QDateTime fromEpoch(const QJsonValue &value)
{
    return QDateTime::fromSecsSinceEpoch(qint64(value.toDouble())).toUTC();
}

qint64 hourKey(const QDateTime &time)
{
    const qint64 seconds = time.toSecsSinceEpoch();
    return seconds - seconds % 3600;
}

} // namespace

StatsRecorder::StatsRecorder(QString path, QObject *parent)
    : QObject(parent)
    , m_path(std::move(path))
{
}

StatsRecorder::~StatsRecorder()
{
    if (m_source) {
        closeSession(QDateTime::currentDateTimeUtc());
        save();
    }
}

void StatsRecorder::start(std::function<Sample()> source, int intervalMs)
{
    m_source = std::move(source);
    m_timer = new QTimer(this);
    m_timer->setInterval(intervalMs);
    connect(m_timer, &QTimer::timeout, this, &StatsRecorder::sampleNow);
    m_timer->start();
    m_saveTimer = new QTimer(this);
    m_saveTimer->setInterval(60 * 1000);
    connect(m_saveTimer, &QTimer::timeout, this, [this] { save(); });
    m_saveTimer->start();
    sampleNow();
}

void StatsRecorder::sampleNow()
{
    if (m_source)
        record(m_source());
}

void StatsRecorder::record(const Sample &sample)
{
    const QDateTime now = sample.time.toUTC();
    if (!m_since.isValid())
        m_since = now;
    qint64 elapsed = m_lastTime.isValid() ? m_lastTime.secsTo(now) : 0;
    if (elapsed < 0 || elapsed > MaxGapSeconds)
        elapsed = 0;

    // Tunnel bytes since the last sample; a new tunnel starts counting from its first sample.
    Bytes tunnel;
    if (sample.connected && sample.tunnel && m_lastTunnel && sample.tunDevice == m_lastTunDevice) {
        tunnel.in = delta(m_lastTunnel->in, sample.tunnel->in);
        tunnel.out = delta(m_lastTunnel->out, sample.tunnel->out);
    }
    m_lastTunnel = sample.connected ? sample.tunnel : std::nullopt;
    m_lastTunDevice = sample.tunDevice;

    // Sessions: one per connection, a new one when the location changes.
    if (m_current && (!sample.connected || m_current->location != sample.location)) {
        closeSession(now);
        elapsed = 0;
    }
    Day &day = m_days[dayKey(now)];
    if (sample.connected && !m_current) {
        m_current = Session{now, {}, sample.location, {}};
        ++day.connections;
        m_dirty = true;
    }

    if (sample.connected && (elapsed > 0 || tunnel.in > 0 || tunnel.out > 0)) {
        Totals &hour = m_hours[hourKey(now)];
        Totals &location = day.locations[sample.location];
        for (Totals *totals : {&day.vpn, &hour, &location, &m_current->totals}) {
            add(totals->bytes, tunnel);
            totals->seconds += elapsed;
        }
        m_dirty = true;
    }

    for (auto it = sample.apps.cbegin(); it != sample.apps.cend(); ++it) {
        const auto last = m_lastApps.constFind(it.key());
        if (last == m_lastApps.cend())
            continue;
        const Bytes moved{delta(last->in, it->in), delta(last->out, it->out)};
        if (moved.in == 0 && moved.out == 0)
            continue;
        add(day.apps[it.key()], moved);
        if (sample.appInfo.contains(it.key()))
            m_appInfo.insert(it.key(), sample.appInfo.value(it.key()));
        m_dirty = true;
    }
    m_lastApps = sample.apps;
    m_lastTime = now;

    const qint64 oldestHour = hourKey(now) - (KeepHours - 1) * 3600;
    while (!m_hours.isEmpty() && m_hours.firstKey() < oldestHour)
        m_hours.erase(m_hours.begin());
}

void StatsRecorder::closeSession(const QDateTime &at)
{
    if (!m_current)
        return;
    m_current->end = at;
    m_sessions.append(*m_current);
    m_current.reset();
    while (m_sessions.size() > MaxSessions)
        m_sessions.removeFirst();
    m_dirty = true;
    save();
}

void StatsRecorder::reset()
{
    m_days.clear();
    m_hours.clear();
    m_sessions.clear();
    m_appInfo.clear();
    m_since = QDateTime::currentDateTimeUtc();
    // A session in progress goes on, counted from now.
    if (m_current)
        m_current = Session{m_since, {}, m_current->location, {}};
    m_dirty = true;
    save();
}

QJsonObject StatsRecorder::report(qint64 callerUid) const
{
    return toJson(callerUid, false);
}

QJsonObject StatsRecorder::toJson(qint64 callerUid, bool forFile) const
{
    // Apps belong to the user who added them; root and the file see all of them.
    const auto visible = [&](const QString &id) {
        if (forFile || callerUid == 0)
            return true;
        const qint64 owner = m_appInfo.value(id).ownerUid;
        return owner == -1 || owner == callerUid;
    };
    const auto totalsJson = [](const Totals &totals) {
        QJsonObject object = bytesJson(totals.bytes);
        object.insert(QStringLiteral("seconds"), double(totals.seconds));
        return object;
    };
    const auto sessionJson = [&](const Session &session) {
        QJsonObject object = totalsJson(session.totals);
        object.insert(QStringLiteral("start"), double(session.start.toSecsSinceEpoch()));
        if (session.end.isValid())
            object.insert(QStringLiteral("end"), double(session.end.toSecsSinceEpoch()));
        object.insert(QStringLiteral("location"), session.location);
        return object;
    };

    QJsonObject days;
    for (auto it = m_days.cbegin(); it != m_days.cend(); ++it) {
        QJsonObject day = totalsJson(it->vpn);
        day.insert(QStringLiteral("connections"), it->connections);
        QJsonObject locations;
        for (auto location = it->locations.cbegin(); location != it->locations.cend(); ++location)
            locations.insert(location.key(), totalsJson(*location));
        day.insert(QStringLiteral("locations"), locations);
        QJsonObject apps;
        for (auto app = it->apps.cbegin(); app != it->apps.cend(); ++app) {
            if (visible(app.key()))
                apps.insert(app.key(), bytesJson(*app));
        }
        day.insert(QStringLiteral("apps"), apps);
        days.insert(it.key(), day);
    }

    QJsonObject hours;
    for (auto it = m_hours.cbegin(); it != m_hours.cend(); ++it)
        hours.insert(QString::number(it.key()), totalsJson(*it));

    QJsonArray sessions;
    for (const Session &session : m_sessions)
        sessions.append(sessionJson(session));

    QJsonObject apps;
    for (auto it = m_appInfo.cbegin(); it != m_appInfo.cend(); ++it) {
        if (!visible(it.key()))
            continue;
        QJsonObject app{{QStringLiteral("name"), it->name}, {QStringLiteral("icon"), it->icon}};
        if (forFile)
            app.insert(QStringLiteral("owner_uid"), double(it->ownerUid));
        apps.insert(it.key(), app);
    }

    QJsonObject root{{QStringLiteral("version"), FormatVersion},
                     {QStringLiteral("days"), days},
                     {QStringLiteral("hours"), hours},
                     {QStringLiteral("sessions"), sessions},
                     {QStringLiteral("apps"), apps}};
    if (m_since.isValid())
        root.insert(QStringLiteral("since"), double(m_since.toSecsSinceEpoch()));
    if (m_current && !forFile)
        root.insert(QStringLiteral("current"), sessionJson(*m_current));
    return root;
}

QString StatsRecorder::load()
{
    QFile file(m_path);
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("Cannot read %1").arg(m_path);
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != FormatVersion)
        return QStringLiteral("%1 has an unsupported version").arg(m_path);

    const auto totalsFrom = [](const QJsonObject &object) {
        return Totals{bytesFrom(object), qint64(object.value(QStringLiteral("seconds")).toDouble())};
    };
    const QJsonObject days = root.value(QStringLiteral("days")).toObject();
    for (auto it = days.constBegin(); it != days.constEnd(); ++it) {
        const QJsonObject object = it->toObject();
        Day day;
        day.vpn = totalsFrom(object);
        day.connections = object.value(QStringLiteral("connections")).toInt();
        const QJsonObject locations = object.value(QStringLiteral("locations")).toObject();
        for (auto location = locations.constBegin(); location != locations.constEnd(); ++location)
            day.locations.insert(location.key(), totalsFrom(location->toObject()));
        const QJsonObject apps = object.value(QStringLiteral("apps")).toObject();
        for (auto app = apps.constBegin(); app != apps.constEnd(); ++app)
            day.apps.insert(app.key(), bytesFrom(app->toObject()));
        m_days.insert(it.key(), day);
    }
    const QJsonObject hours = root.value(QStringLiteral("hours")).toObject();
    for (auto it = hours.constBegin(); it != hours.constEnd(); ++it)
        m_hours.insert(it.key().toLongLong(), totalsFrom(it->toObject()));
    for (const QJsonValue &value : root.value(QStringLiteral("sessions")).toArray()) {
        const QJsonObject object = value.toObject();
        m_sessions.append({fromEpoch(object.value(QStringLiteral("start"))),
                           fromEpoch(object.value(QStringLiteral("end"))), object.value(QStringLiteral("location")).toString(), totalsFrom(object)});
    }
    const QJsonObject apps = root.value(QStringLiteral("apps")).toObject();
    for (auto it = apps.constBegin(); it != apps.constEnd(); ++it) {
        const QJsonObject object = it->toObject();
        m_appInfo.insert(it.key(), {object.value(QStringLiteral("name")).toString(),
                                    object.value(QStringLiteral("icon")).toString(),
                                    qint64(object.value(QStringLiteral("owner_uid")).toDouble(-1))});
    }
    if (root.contains(QStringLiteral("since")))
        m_since = fromEpoch(root.value(QStringLiteral("since")));
    return {};
}

QString StatsRecorder::save()
{
    if (!m_dirty)
        return {};
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot write %1").arg(m_path);
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(toJson(-1, true)).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return QStringLiteral("Cannot write %1").arg(m_path);
    m_dirty = false;
    return {};
}

} // namespace agv
