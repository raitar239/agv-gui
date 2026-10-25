#include "appexclusionservice.h"

#include <unistd.h>

namespace agv {

namespace {

RoutingEngine::Mode routingMode(const QString &mode)
{
    return mode == QLatin1String("vpn-only") ? RoutingEngine::Mode::VpnOnly : RoutingEngine::Mode::Bypass;
}

bool mayChange(const AppRule &rule, qint64 callerUid)
{
    // root manages everything; users manage their own rules.
    return callerUid == 0 || rule.ownerUid == callerUid;
}

} // namespace

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
    m_available = m_engine->apply(activeIds(), routingMode(m_store->mode()));
    if (!m_available) {
        qWarning("agvd: cannot apply app exclusions: %s", qPrintable(m_engine->lastError()));
        return;
    }
    m_matcher.setRules(m_store->rules(), m_engine->baseCgroup());
    scanRunning();
}

QList<AppRule> AppExclusionService::rules(qint64 callerUid) const
{
    QList<AppRule> result;
    for (const AppRule &rule : m_store->rules()) {
        if (callerUid < 0 || mayChange(rule, callerUid))
            result.append(rule);
    }
    return result;
}

Error AppExclusionService::add(const QVariantMap &fields, qint64 callerUid, QString *id)
{
    if (!isSupported())
        return {errors::NotSupported, unsupportedReason()};

    QString error;
    auto rule = AppStore::fromMap(fields, &error);
    if (!rule)
        return {errors::InvalidArgument, error};
    // Rules made by root (e.g. from a script) apply to every user.
    rule->ownerUid = callerUid == 0 ? -1 : callerUid;

    const QList<AppRule> previous = m_store->rules();
    *id = m_store->add(*rule);
    return commit(previous);
}

Error AppExclusionService::update(const QString &id, const QVariantMap &fields, qint64 callerUid)
{
    const AppRule *existing = m_store->find(id);
    if (!existing || !mayChange(*existing, callerUid))
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

Error AppExclusionService::remove(const QString &id, qint64 callerUid)
{
    const AppRule *existing = m_store->find(id);
    if (!existing || !mayChange(*existing, callerUid))
        return {errors::NotFound, QStringLiteral("No app exclusion %1").arg(id)};
    if (!isSupported())
        return {errors::NotSupported, unsupportedReason()};

    const QList<AppRule> previous = m_store->rules();
    m_store->remove(id);
    return commit(previous);
}

Error AppExclusionService::setMode(const QString &mode)
{
    if (!AppStore::isValidMode(mode))
        return {errors::InvalidArgument, QStringLiteral("Mode must be bypass or vpn-only")};
    if (!isSupported())
        return {errors::NotSupported, unsupportedReason()};
    if (mode == m_store->mode())
        return {};

    const QString previous = m_store->mode();
    m_store->setMode(mode);
    const Error error = commit(m_store->rules());
    if (!error.isOk()) {
        // Keep memory, kernel and the saved file on the old mode.
        m_store->setMode(previous);
        m_engine->apply(activeIds(), routingMode(previous));
    }
    return error;
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

void AppExclusionService::onExecuted(pid_t pid)
{
    if (!m_available || !m_matcher.hasRules())
        return;
    claim(pid);
}

void AppExclusionService::scanRunning()
{
    if (!m_available || !m_matcher.hasRules())
        return;
    for (const pid_t pid : listProcesses(m_procDir))
        claim(pid);
}

void AppExclusionService::claim(pid_t pid)
{
    // Never the init process or the daemon itself.
    if (pid <= 1 || pid == ::getpid())
        return;
    const auto process = readProcess(pid, m_procDir);
    if (!process)
        return;
    const QString id = m_matcher.match(*process);
    if (!id.isEmpty() && !m_engine->moveProcess(pid, id))
        qWarning("agvd: cannot exclude process %d: %s", pid, qPrintable(m_engine->lastError()));
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
    if (!m_engine->apply(activeIds(), routingMode(m_store->mode()))) {
        const QString reason = m_engine->lastError();
        m_store->setRules(previous);
        m_engine->apply(activeIds(), routingMode(m_store->mode()));
        return {errors::SystemFailed, reason};
    }

    m_matcher.setRules(m_store->rules(), m_engine->baseCgroup());
    // Already running instances join right away; their existing connections stay where they are.
    scanRunning();

    const QString saveError = m_store->save();
    emit changed();
    if (!saveError.isEmpty())
        return {errors::SystemFailed, saveError};
    return {};
}

} // namespace agv
