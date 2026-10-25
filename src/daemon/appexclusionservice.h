#pragma once

#include "appmatcher.h"
#include "appstore.h"
#include "daemonerror.h"
#include "routingengine.h"

#include <QObject>

namespace agv {

// App exclusions as the daemon exposes them: the persisted rules plus the kernel state that
// enforces them. Every change is applied to the system first and saved only when that worked.
class AppExclusionService : public QObject
{
    Q_OBJECT

public:
    AppExclusionService(AppStore *store, RoutingEngine *engine, QObject *parent = nullptr);

    // Loads the rules and brings the system in line with them.
    void start();

    // False when the kernel or tools lack what the engine needs (checked once in start()).
    bool isSupported() const { return m_available; }
    QString unsupportedReason() const { return m_engine->lastError(); }
    // The rules a caller may see and change: their own, or all of them for root (or callerUid -1).
    QList<AppRule> rules(qint64 callerUid = -1) const;

    Error add(const QVariantMap &fields, qint64 callerUid, QString *id);
    Error update(const QString &id, const QVariantMap &fields, qint64 callerUid);
    Error remove(const QString &id, qint64 callerUid);

    // Moves a process into an app's cgroup; an empty appId means the builtin "agv-run" group.
    Error moveProcess(pid_t pid, const QString &appId);

    QMap<QString, RoutingEngine::Traffic> traffic() { return m_engine->traffic(); }

    // A process just called exec(): move it if it belongs to an excluded app.
    void onExecuted(pid_t pid);
    // Looks through all running processes (at start, after rule changes, after lost events).
    void scanRunning();

    // For tests: where to read processes from (default /proc).
    void setProcDir(const QString &procDir) { m_procDir = procDir; }

signals:
    void changed();

private:
    QStringList activeIds() const;
    void claim(pid_t pid);
    // Applies the store's current rules; on failure restores `previous` in the store and system.
    Error commit(const QList<AppRule> &previous);

    AppStore *m_store;
    RoutingEngine *m_engine;
    bool m_available = false;
    AppMatcher m_matcher;
    QString m_procDir = QStringLiteral("/proc");
};

} // namespace agv
