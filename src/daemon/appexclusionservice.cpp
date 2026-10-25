#include "appexclusionservice.h"

namespace agv {

AppExclusionService::AppExclusionService(AppStore *store, RoutingEngine *engine, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_engine(engine)
{
}

void AppExclusionService::start()
{
    const QString loadError = m_store->load();
    if (!loadError.isEmpty())
        qWarning("agvd: %s; starting with no app exclusions", qPrintable(loadError));

    if (!m_engine->init()) {
        qWarning("agvd: app exclusions unavailable: %s", qPrintable(m_engine->lastError()));
        return;
    }
    // Also adopts cgroups (and the processes in them) left by a previous agvd.
    m_available = m_engine->apply(activeIds());
    if (!m_available)
        qWarning("agvd: cannot apply app exclusions: %s", qPrintable(m_engine->lastError()));
}

Error AppExclusionService::add(const QVariantMap &fields, QString *id)
{
    if (!isSupported())
        return {errors::NotSupported, unsupportedReason()};

    QString error;
    const auto rule = AppStore::fromMap(fields, &error);
    if (!rule)
        return {errors::InvalidArgument, error};

    const QList<AppRule> previous = m_store->rules();
    *id = m_store->add(*rule);
    return commit(previous);
}

Error AppExclusionService::update(const QString &id, const QVariantMap &fields)
{
    const AppRule *existing = m_store->find(id);
    if (!existing)
        return {errors::NotFound, QStringLiteral("No app exclusion %1").arg(id)};
    if (!isSupported())
        return {errors::NotSupported, unsupportedReason()};

    QString error;
    const auto rule = AppStore::fromMap(fields, &error, existing);
    if (!rule)
        return {errors::InvalidArgument, error};

    const QList<AppRule> previous = m_store->rules();
    m_store->replace(*rule);
    return commit(previous);
}

Error AppExclusionService::remove(const QString &id)
{
    if (!m_store->find(id))
        return {errors::NotFound, QStringLiteral("No app exclusion %1").arg(id)};
    if (!isSupported())
        return {errors::NotSupported, unsupportedReason()};

    const QList<AppRule> previous = m_store->rules();
    m_store->remove(id);
    return commit(previous);
}

Error AppExclusionService::moveProcess(pid_t pid, const QString &appId)
{
    if (!isSupported())
        return {errors::NotSupported, unsupportedReason()};

    const QString id = appId.isEmpty() ? RoutingEngine::BuiltinRunId : appId;
    if (!activeIds().contains(id))
        return {errors::NotFound, QStringLiteral("No enabled app exclusion %1").arg(id)};
    if (!m_engine->moveProcess(pid, id))
        return {errors::InvalidArgument, m_engine->lastError()};
    return {};
}

QStringList AppExclusionService::activeIds() const
{
    // The builtin group is always there so "agv-run <cmd>" works without configuring an app.
    QStringList ids{RoutingEngine::BuiltinRunId};
    for (const AppRule &rule : m_store->rules()) {
        if (rule.enabled)
            ids.append(rule.id);
    }
    return ids;
}

Error AppExclusionService::commit(const QList<AppRule> &previous)
{
    if (!m_engine->apply(activeIds())) {
        const QString reason = m_engine->lastError();
        m_store->setRules(previous);
        m_engine->apply(activeIds());
        return {errors::SystemFailed, reason};
    }

    const QString saveError = m_store->save();
    emit changed();
    if (!saveError.isEmpty())
        return {errors::SystemFailed, saveError};
    return {};
}

} // namespace agv
