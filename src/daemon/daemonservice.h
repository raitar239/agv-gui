#pragma once

#include "dbustypes.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QObject>

#include <functional>

namespace agv {

class PolkitAuthorizer;
class RoutingEngine;
class AppExclusionService;
class DaemonSettings;
class DnsGuard;
class KillSwitch;
class NetworkRules;
class StatsRecorder;
class SystemdUnit;
class VpnService;
struct Error;

// The object exported at /org/agv/Daemon. The adaptor generated from dbus/org.agv.Daemon1.xml
// forwards every call here; methods that touch the CLI or polkit answer with delayed replies.
class DaemonService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_PROPERTY(QString State READ state)
    Q_PROPERTY(QString Location READ location)
    Q_PROPERTY(QString Mode READ mode)
    Q_PROPERTY(QString TunDevice READ tunDevice)
    Q_PROPERTY(QString LastError READ lastError)
    Q_PROPERTY(bool LoggedIn READ loggedIn)
    Q_PROPERTY(QString SiteExclusionMode READ siteExclusionMode)
    Q_PROPERTY(bool AppExclusionsSupported READ appExclusionsSupported)
    Q_PROPERTY(AgvExternalVpnList ExternalVpns READ externalVpns)
    Q_PROPERTY(QString AppExclusionMode READ appExclusionMode)
    Q_PROPERTY(bool StartAtBoot READ startAtBoot)
    Q_PROPERTY(bool AutoConnect READ autoConnect)
    Q_PROPERTY(bool KillSwitch READ killSwitch)
    Q_PROPERTY(bool KillSwitchAllowLan READ killSwitchAllowLan)
    Q_PROPERTY(bool KillSwitchActive READ killSwitchActive)
    Q_PROPERTY(bool KeepDnsInVpn READ keepDnsInVpn)
    Q_PROPERTY(bool Reconnecting READ reconnecting)
    Q_PROPERTY(QString CliVersion READ cliVersion)
    Q_PROPERTY(QVariantMap Network READ network)
    Q_PROPERTY(QVariantMap TrustedNetworks READ trustedNetworks)
    Q_PROPERTY(bool WifiAutoConnect READ wifiAutoConnect)
    Q_PROPERTY(bool TrustedDisconnect READ trustedDisconnect)
    Q_PROPERTY(QString CliInstall READ cliInstall)

public:
    DaemonService(VpnService *vpn, AppExclusionService *apps, PolkitAuthorizer *polkit,
                  QDBusConnection bus, QObject *parent = nullptr);

    QString state() const;
    QString location() const;
    QString mode() const;
    QString tunDevice() const;
    QString lastError() const;
    bool loggedIn() const;
    QString siteExclusionMode() const;
    bool appExclusionsSupported() const;
    AgvExternalVpnList externalVpns() const;
    QString appExclusionMode() const;
    bool startAtBoot() const;
    bool autoConnect() const;
    bool killSwitch() const;
    bool killSwitchAllowLan() const;
    bool killSwitchActive() const;
    bool keepDnsInVpn() const;
    bool reconnecting() const;
    QString cliVersion() const;
    QVariantMap network() const;
    QVariantMap trustedNetworks() const;
    bool wifiAutoConnect() const;
    bool trustedDisconnect() const;
    QString cliInstall() const;

    // Optional: without them the startup properties read false and setting them fails.
    void setStartup(DaemonSettings *settings, SystemdUnit *unit);
    // Optional: without it GetStatistics answers an empty object.
    void setStatistics(StatsRecorder *stats) { m_stats = stats; }
    // Optional: without it Network reports available = false. Needs setStartup().
    void setNetworkRules(NetworkRules *rules);
    // Optional: needs setStartup() too, where its setting lives. engine gives the cgroups.
    void setProtection(KillSwitch *killSwitch, DnsGuard *dnsGuard, RoutingEngine *engine);

public slots:
    void Connect(const QString &location);
    void ConnectFastest();
    void Disconnect();
    void StopExternalVpns();
    AgvLocationList ListLocations();

    void Login();
    void CancelLogin();
    void Logout();
    void ImportLogin();
    QVariantMap GetLicense();

    void SetSiteExclusionMode(const QString &mode);
    QStringList GetSiteExclusions();
    AgvSiteExclusionList ListSiteExclusions();
    void SetSiteExclusionEnabled(const QString &item, bool enabled, const QStringList &members);
    void AddSiteExclusions(const QStringList &items);
    void RemoveSiteExclusions(const QStringList &items);

    AgvAppExclusionList ListAppExclusions();
    QString AddAppExclusion(const QVariantMap &rule);
    void UpdateAppExclusion(const QString &id, const QVariantMap &rule);
    void RemoveAppExclusion(const QString &id);
    AgvAppTrafficList GetAppTraffic();
    void SetAppExclusionMode(const QString &mode);
    void SetStartAtBoot(bool enabled);
    void SetAutoConnect(bool enabled);
    void SetKillSwitch(bool enabled);
    void SetKillSwitchAllowLan(bool enabled);
    void SetKeepDnsInVpn(bool enabled);
    void UpdateCli();
    void SetWifiAutoConnect(bool enabled);
    void SetTrustedDisconnect(bool enabled);
    void SetNetworkTrusted(const QString &uuid, const QString &name, bool trusted);

    QString GetStatistics();
    void ResetStatistics();

    AgvConfigMap GetConfig();
    void SetConfig(const QString &key, const QString &value);

signals:
    void LoginUrl(const QString &url);
    void LoginFinished(bool success, const QString &message);
    void SiteExclusionsChanged();
    void AppExclusionsChanged();

private:
    // Marks the current call as delayed, checks polkit, then runs action with a reply helper.
    void authorized(const QString &actionId, std::function<void(QDBusMessage)> action);
    qint64 callerUid(const QDBusMessage &message) const;
    void replyDone(const QDBusMessage &message, const Error &error);
    void replyError(const QDBusMessage &message, const Error &error);
    void emitPropertiesChanged(const QStringList &names);
    // Engages or releases the kill switch and the DNS guard to match the settings and the VPN.
    void syncProtection();
    void setSetting(const QDBusMessage &message, const QString &property, const std::function<void()> &change);

    VpnService *m_vpn;
    AppExclusionService *m_apps;
    DaemonSettings *m_settings = nullptr;
    SystemdUnit *m_unit = nullptr;
    StatsRecorder *m_stats = nullptr;
    KillSwitch *m_killSwitch = nullptr;
    DnsGuard *m_dnsGuard = nullptr;
    NetworkRules *m_networkRules = nullptr;
    RoutingEngine *m_engine = nullptr;
    PolkitAuthorizer *m_polkit;
    QDBusConnection m_bus;
    QVariantMap m_lastProperties;
};

} // namespace agv
