#pragma once

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
    QList<AppRule> rules() const { return m_store->rules(); }

    Error add(const QVariantMap &fields, QString *id);
    Error update(const QString &id, const QVariantMap &fields);
    Error remove(const QString &id);

    // Moves a process into an app's cgroup; an empty appId means the builtin "agv-run" group.
    Error moveProcess(pid_t pid, const QString &appId);

    QMap<QString, RoutingEngine::Traffic> traffic() { return m_engine->traffic(); }

signals:
    void changed();

private:
    QStringList activeIds() const;
    // Applies the store's current rules; on failure restores `previous` in the store and system.
    Error commit(const QList<AppRule> &previous);

    AppStore *m_store;
    RoutingEngine *m_engine;
    bool m_available = false;
};

} // namespace agv
