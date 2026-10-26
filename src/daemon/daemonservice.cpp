#include "daemonservice.h"

#include "appexclusionservice.h"
#include "daemonsettings.h"
#include "killswitch.h"
#include "systemdunit.h"
#include "polkitauthorizer.h"
#include "statsrecorder.h"
#include "vpnservice.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <pwd.h>

namespace agv {

namespace {

const QStringList StatusProperties{
    QStringLiteral("State"),    QStringLiteral("Location"),  QStringLiteral("Mode"),
    QStringLiteral("TunDevice"), QStringLiteral("LastError"), QStringLiteral("LoggedIn"),
    QStringLiteral("ExternalVpns"), QStringLiteral("Reconnecting"), QStringLiteral("KillSwitchActive"),
};

} // namespace

DaemonService::DaemonService(VpnService *vpn, AppExclusionService *apps, PolkitAuthorizer *polkit,
                             QDBusConnection bus, QObject *parent)
    : QObject(parent)
    , m_vpn(vpn)
    , m_apps(apps)
    , m_polkit(polkit)
    , m_bus(std::move(bus))
{
    connect(m_vpn, &VpnService::statusChanged, this, [this] {
        syncKillSwitch();
        emitPropertiesChanged(StatusProperties);
    });
    connect(m_vpn, &VpnService::siteExclusionsChanged, this, [this] {
        emitPropertiesChanged({QStringLiteral("SiteExclusionMode")});
        emit SiteExclusionsChanged();
    });
    connect(m_apps, &AppExclusionService::changed, this, [this] {
        syncKillSwitch();
        emitPropertiesChanged({QStringLiteral("AppExclusionMode")});
        emit AppExclusionsChanged();
    });
    connect(m_vpn, &VpnService::loginUrlReady, this, &DaemonService::LoginUrl);
    connect(m_vpn, &VpnService::loginFinished, this, &DaemonService::LoginFinished);
}

QString DaemonService::state() const { return m_vpn->stateName(); }
QString DaemonService::location() const { return m_vpn->location(); }
QString DaemonService::mode() const { return m_vpn->mode(); }
QString DaemonService::tunDevice() const { return m_vpn->tunDevice(); }
QString DaemonService::lastError() const { return m_vpn->lastError(); }
bool DaemonService::loggedIn() const { return m_vpn->loggedIn(); }
QString DaemonService::siteExclusionMode() const { return m_vpn->siteExclusionMode(); }
bool DaemonService::appExclusionsSupported() const { return m_apps->isSupported(); }
QString DaemonService::appExclusionMode() const { return m_apps->mode(); }
bool DaemonService::startAtBoot() const { return m_unit && m_unit->isEnabled(); }
bool DaemonService::autoConnect() const { return m_settings && m_settings->autoConnect(); }

void DaemonService::setStartup(DaemonSettings *settings, SystemdUnit *unit)
{
    m_settings = settings;
    m_unit = unit;
    if (m_unit) {
        connect(m_unit, &SystemdUnit::enabledChanged, this,
                [this] { emitPropertiesChanged({QStringLiteral("StartAtBoot")}); });
        m_unit->refresh();
    }
}

void DaemonService::SetStartAtBoot(bool enabled)
{
    authorized(ActionConfigure, [this, enabled](const QDBusMessage &message) {
        if (!m_unit) {
            replyError(message, {errors::NotSupported, QStringLiteral("Not running as a system service")});
            return;
        }
        m_unit->setEnabled(enabled, [this, message](const QString &error) {
            replyDone(message, error.isEmpty() ? Error{} : Error{errors::SystemFailed, error});
        });
    });
}

void DaemonService::SetAutoConnect(bool enabled)
{
    authorized(ActionConfigure, [this, enabled](const QDBusMessage &message) {
        setSetting(message, QStringLiteral("AutoConnect"), [this, enabled] {
            m_settings->setAutoConnect(enabled);
            m_vpn->setAutoConnect(enabled);
        });
    });
}

void DaemonService::SetKillSwitch(bool enabled)
{
    authorized(ActionConfigure, [this, enabled](const QDBusMessage &message) {
        if (!m_killSwitch) {
            replyError(message, {errors::NotSupported, QStringLiteral("The kill switch needs nftables and cgroup v2")});
            return;
        }
        setSetting(message, QStringLiteral("KillSwitch"), [this, enabled] { m_settings->setKillSwitch(enabled); });
    });
}

void DaemonService::SetKillSwitchAllowLan(bool enabled)
{
    authorized(ActionConfigure, [this, enabled](const QDBusMessage &message) {
        setSetting(message, QStringLiteral("KillSwitchAllowLan"),
                   [this, enabled] { m_settings->setKillSwitchAllowLan(enabled); });
    });
}

void DaemonService::setSetting(const QDBusMessage &message, const QString &property,
                               const std::function<void()> &change)
{
    if (!m_settings) {
        replyError(message, {errors::NotSupported, QStringLiteral("Settings are not available")});
        return;
    }
    change();
    if (!m_settings->save()) {
        replyError(message, {errors::SystemFailed, QStringLiteral("Cannot save settings")});
        return;
    }
    syncKillSwitch();
    emitPropertiesChanged({property, QStringLiteral("KillSwitchActive")});
    replyDone(message, {});
}

bool DaemonService::killSwitch() const { return m_killSwitch && m_settings && m_settings->killSwitch(); }
bool DaemonService::killSwitchAllowLan() const { return !m_settings || m_settings->killSwitchAllowLan(); }
bool DaemonService::reconnecting() const { return m_vpn->reconnecting(); }

bool DaemonService::killSwitchActive() const
{
    return m_killSwitch && m_killSwitch->isEngaged() && m_vpn->state() != VpnService::State::Connected;
}

void DaemonService::setKillSwitch(KillSwitch *killSwitch, RoutingEngine *engine)
{
    m_killSwitch = killSwitch;
    m_engine = engine;
    syncKillSwitch();
}

void DaemonService::syncKillSwitch()
{
    if (!m_killSwitch)
        return;
    // SOCKS5 mode has no tunnel to keep traffic in: apps choose to use the proxy.
    const bool socks = m_vpn->lastMode().compare(QLatin1String("socks"), Qt::CaseInsensitive) == 0;
    if (!killSwitch() || !m_vpn->wanted() || socks) {
        if (m_killSwitch->isEngaged())
            m_killSwitch->release();
        return;
    }

    KillSwitch::Options options;
    if (!m_vpn->lastTunDevice().isEmpty())
        options.tunDevice = m_vpn->lastTunDevice();
    options.allowLan = killSwitchAllowLan();
    options.mode = m_apps->mode() == QLatin1String("vpn-only") ? RoutingEngine::Mode::VpnOnly
                                                                : RoutingEngine::Mode::Bypass;
    options.ownCgroup = m_engine->ownCgroup();
    // nftables needs the cgroup to exist when the rules load.
    const QString resolved = QStringLiteral("system.slice/systemd-resolved.service");
    if (QFileInfo::exists(m_engine->cgroupRoot() + QLatin1Char('/') + resolved))
        options.resolvedCgroup = resolved;
    if (!m_killSwitch->engage(options))
        qWarning("agvd: kill switch: %s", qPrintable(m_killSwitch->lastError()));
}

AgvExternalVpnList DaemonService::externalVpns() const
{
    AgvExternalVpnList result;
    for (const ExternalVpn &vpn : m_vpn->externalVpns())
        result.append({uint(vpn.pid), uint(vpn.uid), vpn.home});
    return result;
}

void DaemonService::Connect(const QString &location)
{
    authorized(ActionControl, [this, location](const QDBusMessage &message) {
        m_vpn->connectTo(location, [this, message](const Error &error) { replyDone(message, error); });
    });
}

void DaemonService::ConnectFastest()
{
    authorized(ActionControl, [this](const QDBusMessage &message) {
        m_vpn->connectFastest([this, message](const Error &error) { replyDone(message, error); });
    });
}

void DaemonService::Disconnect()
{
    authorized(ActionControl, [this](const QDBusMessage &message) {
        m_vpn->disconnectVpn([this, message](const Error &error) { replyDone(message, error); });
    });
}

void DaemonService::StopExternalVpns()
{
    authorized(ActionConfigure, [this](const QDBusMessage &message) {
        m_vpn->stopExternalVpns([this, message](const Error &error) { replyDone(message, error); });
    });
}

AgvLocationList DaemonService::ListLocations()
{
    setDelayedReply(true);
    const QDBusMessage message = this->message();
    m_vpn->listLocations([this, message](const QList<cli::Location> &locations, const Error &error) {
        if (!error.isOk()) {
            replyError(message, error);
            return;
        }
        AgvLocationList result;
        result.reserve(locations.size());
        for (const cli::Location &location : locations)
            result.append({location.iso, location.country, location.city, location.ping.value_or(-1)});
        m_bus.send(message.createReply(QVariant::fromValue(result)));
    });
    return {};
}

void DaemonService::Login()
{
    authorized(ActionConfigure, [this](const QDBusMessage &message) {
        m_vpn->startLogin([this, message](const Error &error) { replyDone(message, error); });
    });
}

void DaemonService::CancelLogin()
{
    authorized(ActionConfigure, [this](const QDBusMessage &message) {
        m_vpn->cancelLogin();
        replyDone(message, {});
    });
}

void DaemonService::Logout()
{
    authorized(ActionConfigure, [this](const QDBusMessage &message) {
        m_vpn->logout([this, message](const Error &error) { replyDone(message, error); });
    });
}

void DaemonService::ImportLogin()
{
    authorized(ActionConfigure, [this](const QDBusMessage &message) {
        // Import only from the caller's own home, never from a path the client names.
        const QDBusReply<uint> uid = m_bus.interface()->serviceUid(message.service());
        const passwd *user = uid.isValid() ? getpwuid(uid.value()) : nullptr;
        if (!user || !user->pw_dir) {
            replyError(message, {errors::NotFound, QStringLiteral("Cannot determine the caller's home")});
            return;
        }
        m_vpn->importLogin(QFile::decodeName(user->pw_dir), uid.value(),
                           [this, message](const Error &error) { replyDone(message, error); });
    });
}

QVariantMap DaemonService::GetLicense()
{
    authorized(ActionControl, [this](const QDBusMessage &message) {
        m_vpn->license([this, message](const cli::License &license, const Error &error) {
            if (!error.isOk()) {
                replyError(message, error);
                return;
            }
            QVariantMap result{
                {QStringLiteral("account"), license.account},
                {QStringLiteral("plan"), license.plan},
            };
            if (license.maxDevices)
                result.insert(QStringLiteral("max_devices"), *license.maxDevices);
            if (license.validUntil)
                result.insert(QStringLiteral("valid_until"), license.validUntil->toString(Qt::ISODate));
            m_bus.send(message.createReply(result));
        });
    });
    return {};
}

void DaemonService::SetSiteExclusionMode(const QString &mode)
{
    authorized(ActionControl, [this, mode](const QDBusMessage &message) {
        m_vpn->setSiteExclusionMode(mode, [this, message](const Error &error) { replyDone(message, error); });
    });
}

QStringList DaemonService::GetSiteExclusions()
{
    setDelayedReply(true);
    const QDBusMessage message = this->message();
    m_vpn->siteExclusions([this, message](const QStringList &items, const Error &error) {
        if (!error.isOk())
            replyError(message, error);
        else
            m_bus.send(message.createReply(items));
    });
    return {};
}

AgvSiteExclusionList DaemonService::ListSiteExclusions()
{
    setDelayedReply(true);
    const QDBusMessage message = this->message();
    m_vpn->listSiteExclusions([this, message](const QList<QPair<QString, bool>> &items, const Error &error) {
        if (!error.isOk()) {
            replyError(message, error);
            return;
        }
        AgvSiteExclusionList result;
        for (const auto &[item, enabled] : items)
            result.append({item, enabled});
        m_bus.send(message.createReply(QVariant::fromValue(result)));
    });
    return {};
}

void DaemonService::SetSiteExclusionEnabled(const QString &item, bool enabled, const QStringList &members)
{
    authorized(ActionControl, [this, item, enabled, members](const QDBusMessage &message) {
        m_vpn->setSiteExclusionEnabled(item, enabled, members,
                                       [this, message](const Error &error) { replyDone(message, error); });
    });
}

void DaemonService::AddSiteExclusions(const QStringList &items)
{
    authorized(ActionControl, [this, items](const QDBusMessage &message) {
        m_vpn->addSiteExclusions(items, [this, message](const Error &error) { replyDone(message, error); });
    });
}

void DaemonService::RemoveSiteExclusions(const QStringList &items)
{
    authorized(ActionControl, [this, items](const QDBusMessage &message) {
        m_vpn->removeSiteExclusions(items, [this, message](const Error &error) { replyDone(message, error); });
    });
}

QString DaemonService::GetStatistics()
{
    if (!m_stats)
        return QStringLiteral("{}");
    return QString::fromUtf8(QJsonDocument(m_stats->report(callerUid(message()))).toJson(QJsonDocument::Compact));
}

void DaemonService::ResetStatistics()
{
    authorized(ActionControl, [this](const QDBusMessage &message) {
        if (m_stats)
            m_stats->reset();
        replyDone(message, {});
    });
}

AgvAppExclusionList DaemonService::ListAppExclusions()
{
    AgvAppExclusionList result;
    for (const AppRule &rule : m_apps->rules(callerUid(message())))
        result.append({rule.id, AppStore::toMap(rule)});
    return result;
}

QString DaemonService::AddAppExclusion(const QVariantMap &rule)
{
    authorized(ActionControl, [this, rule](const QDBusMessage &message) {
        QString id;
        const Error error = m_apps->add(rule, callerUid(message), &id);
        if (!error.isOk())
            replyError(message, error);
        else
            m_bus.send(message.createReply(id));
    });
    return {};
}

void DaemonService::UpdateAppExclusion(const QString &id, const QVariantMap &rule)
{
    authorized(ActionControl, [this, id, rule](const QDBusMessage &message) {
        replyDone(message, m_apps->update(id, rule, callerUid(message)));
    });
}

void DaemonService::RemoveAppExclusion(const QString &id)
{
    authorized(ActionControl, [this, id](const QDBusMessage &message) {
        replyDone(message, m_apps->remove(id, callerUid(message)));
    });
}

void DaemonService::SetAppExclusionMode(const QString &mode)
{
    authorized(ActionConfigure, [this, mode](const QDBusMessage &message) {
        replyDone(message, m_apps->setMode(mode));
    });
}

AgvAppTrafficList DaemonService::GetAppTraffic()
{
    AgvAppTrafficList result;
    const auto traffic = m_apps->traffic();
    for (auto it = traffic.cbegin(); it != traffic.cend(); ++it)
        result.append({it.key(), it->bytesOut, it->bytesIn});
    return result;
}

AgvConfigMap DaemonService::GetConfig()
{
    setDelayedReply(true);
    const QDBusMessage message = this->message();
    m_vpn->config([this, message](const QMap<QString, cli::ConfigValue> &config, const Error &error) {
        if (!error.isOk()) {
            replyError(message, error);
            return;
        }
        AgvConfigMap result;
        for (auto it = config.cbegin(); it != config.cend(); ++it)
            result.insert(it.key(), {it->value, it->isDefault});
        m_bus.send(message.createReply(QVariant::fromValue(result)));
    });
    return {};
}

void DaemonService::SetConfig(const QString &key, const QString &value)
{
    authorized(ActionConfigure, [this, key, value](const QDBusMessage &message) {
        m_vpn->setConfig(key, value, [this, message](const Error &error) { replyDone(message, error); });
    });
}

void DaemonService::authorized(const QString &actionId, std::function<void(QDBusMessage)> action)
{
    setDelayedReply(true);
    const QDBusMessage message = this->message();
    m_polkit->check(message.service(), actionId, message.isInteractiveAuthorizationAllowed(),
                    [this, message, action = std::move(action)](const PolkitAuthorizer::Result &result) {
        if (result.authorized) {
            action(message);
            return;
        }
        // The standard error tells clients to retry with interactive authorization allowed.
        const QString name = result.challenge
            ? QStringLiteral("org.freedesktop.DBus.Error.InteractiveAuthorizationRequired")
            : errors::NotAuthorized;
        replyError(message, {name, result.message});
    });
}

qint64 DaemonService::callerUid(const QDBusMessage &message) const
{
    const QDBusReply<uint> uid = m_bus.interface()->serviceUid(message.service());
    // Unknown callers see and own nothing (uid -2 matches no rule owner).
    return uid.isValid() ? qint64(uid.value()) : -2;
}

void DaemonService::replyDone(const QDBusMessage &message, const Error &error)
{
    if (error.isOk())
        m_bus.send(message.createReply());
    else
        replyError(message, error);
}

void DaemonService::replyError(const QDBusMessage &message, const Error &error)
{
    m_bus.send(message.createErrorReply(error.name, error.message));
}

void DaemonService::emitPropertiesChanged(const QStringList &names)
{
    QVariantMap changed;
    for (const QString &name : names) {
        const QVariant value = property(name.toLatin1().constData());
        if (m_lastProperties.value(name) != value) {
            m_lastProperties.insert(name, value);
            changed.insert(name, value);
        }
    }
    if (changed.isEmpty())
        return;

    QDBusMessage signal = QDBusMessage::createSignal(
        DBusPath, QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    signal << DBusInterface << changed << QStringList();
    m_bus.send(signal);
}

} // namespace agv
