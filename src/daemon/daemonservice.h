#pragma once

#include "dbustypes.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QObject>

#include <functional>

namespace agv {

class PolkitAuthorizer;
class AppExclusionService;
class DaemonSettings;
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

    // Optional: without them the startup properties read false and setting them fails.
    void setStartup(DaemonSettings *settings, SystemdUnit *unit);

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

    VpnService *m_vpn;
    AppExclusionService *m_apps;
    DaemonSettings *m_settings = nullptr;
    SystemdUnit *m_unit = nullptr;
    PolkitAuthorizer *m_polkit;
    QDBusConnection m_bus;
    QVariantMap m_lastProperties;
};

} // namespace agv
