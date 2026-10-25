#pragma once

#include "dbustypes.h"

#include <QDBusConnection>
#include <QHash>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QVariantMap>

#include <functional>

class QDBusMessage;
class QDBusServiceWatcher;

namespace agv {

class LocationModel;

// The GUI's view of agvd: mirrors its properties, forwards actions, turns errors into
// human-readable messages. Lives for the whole process; QML scenes come and go.
class DaemonClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(QString location READ location NOTIFY statusChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY statusChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)
    Q_PROPERTY(bool transitioning READ transitioning NOTIFY statusChanged)
    Q_PROPERTY(int externalVpnCount READ externalVpnCount NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString siteExclusionMode READ siteExclusionMode NOTIFY siteExclusionsChanged)
    Q_PROPERTY(QStringList siteExclusions READ siteExclusions NOTIFY siteExclusionsChanged)
    Q_PROPERTY(bool siteExclusionsLoaded READ siteExclusionsLoaded NOTIFY siteExclusionsChanged)
    Q_PROPERTY(bool appExclusionsSupported READ appExclusionsSupported NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap license READ license NOTIFY licenseChanged)
    Q_PROPERTY(bool loginPending READ loginPending NOTIFY loginPendingChanged)
    Q_PROPERTY(QObject *locations READ locations CONSTANT)
    // Excluded apps: [{ id, name, icon, desktop_file, exe, cmdline_match, enabled, bytes_out, bytes_in }]
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    Q_PROPERTY(bool appsLoaded READ appsLoaded NOTIFY appsChanged)

public:
    explicit DaemonClient(QDBusConnection bus, QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString state() const { return m_props.value(QStringLiteral("State")).toString(); }
    QString location() const { return m_props.value(QStringLiteral("Location")).toString(); }
    QString mode() const { return m_props.value(QStringLiteral("Mode")).toString(); }
    QString lastError() const { return m_props.value(QStringLiteral("LastError")).toString(); }
    bool loggedIn() const { return m_props.value(QStringLiteral("LoggedIn")).toBool(); }
    bool connected() const { return state() == QLatin1String("connected"); }
    bool transitioning() const;
    int externalVpnCount() const { return m_externalVpnCount; }
    bool busy() const { return m_busy > 0; }
    QString siteExclusionMode() const { return m_props.value(QStringLiteral("SiteExclusionMode")).toString(); }
    QStringList siteExclusions() const { return m_siteExclusions; }
    bool siteExclusionsLoaded() const { return m_siteExclusionsLoaded; }
    bool appExclusionsSupported() const { return m_props.value(QStringLiteral("AppExclusionsSupported")).toBool(); }
    QVariantMap license() const { return m_license; }
    bool loginPending() const { return m_loginPending; }
    QObject *locations() const;
    QVariantList apps() const { return m_apps; }
    bool appsLoaded() const { return m_appsLoaded; }
    LocationModel *locationModel() const { return m_locations; }

    // Connects to a location (city, country or ISO code; empty: the last used one).
    Q_INVOKABLE void connectTo(const QString &location = QString());
    Q_INVOKABLE void connectFastest();
    Q_INVOKABLE void disconnectVpn();
    Q_INVOKABLE void toggle();
    // Stops a VPN started outside agv-gui, then connects through agvd.
    Q_INVOKABLE void takeOver();

    Q_INVOKABLE void refreshLocations();
    Q_INVOKABLE void refreshSiteExclusions();
    Q_INVOKABLE void addSiteExclusions(const QString &text);
    Q_INVOKABLE void removeSiteExclusion(const QString &item);
    Q_INVOKABLE void setSiteExclusionMode(const QString &mode);

    Q_INVOKABLE void refreshApps();
    Q_INVOKABLE void refreshAppTraffic();
    // fields: name, icon, desktop_file, exe, cmdline_match (as in the D-Bus API)
    Q_INVOKABLE void addApp(const QVariantMap &fields);
    Q_INVOKABLE void setAppEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void removeApp(const QString &id);
    // True when an app with this .desktop file or program is already in the list.
    Q_INVOKABLE bool isAppExcluded(const QString &desktopFile, const QString &exe) const;

    Q_INVOKABLE void refreshLicense();
    Q_INVOKABLE void login();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void importLogin();

signals:
    void availableChanged();
    void statusChanged();
    void busyChanged();
    void siteExclusionsChanged();
    void licenseChanged();
    void appsChanged();
    void loginPendingChanged();
    void errorOccurred(const QString &message);
    void info(const QString &message);

private slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onLoginUrl(const QString &url);
    void onLoginFinished(bool success, const QString &message);
    void onSiteExclusionsChanged();
    void onAppExclusionsChanged();

private:
    using Reply = std::function<void(const QDBusMessage &)>;

    void call(const QString &method, const QVariantList &args = {}, Reply onSuccess = {}, bool trackBusy = false);
    void setAvailable(bool available);
    void loadAll();
    void applyProperties(const QVariantMap &props);
    void setBusy(bool busy);
    void publishApps();
    static QString describe(const QDBusMessage &error);

    QDBusConnection m_bus;
    QDBusServiceWatcher *m_watcher;
    LocationModel *m_locations;
    bool m_available = false;
    int m_busy = 0;
    QVariantMap m_props;
    int m_externalVpnCount = 0;
    QStringList m_siteExclusions;
    bool m_siteExclusionsLoaded = false;
    QVariantMap m_license;
    AgvAppExclusionList m_appRules;
    QHash<QString, QPair<quint64, quint64>> m_traffic;
    QVariantList m_apps;
    bool m_appsLoaded = false;
    bool m_loginPending = false;
};

} // namespace agv
