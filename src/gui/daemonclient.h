#pragma once

#include "dbustypes.h"

#include <QDBusConnection>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QVariantMap>

#include <functional>

class QDBusMessage;
class QDBusServiceWatcher;

namespace agv {

class KeyedListModel;
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
    // "bypass" or "vpn-only"
    Q_PROPERTY(QString appExclusionMode READ appExclusionMode NOTIFY statusChanged)
    Q_PROPERTY(bool startAtBoot READ startAtBoot NOTIFY statusChanged)
    // The CLI configuration: { "Mode": { value, isDefault }, "Protocol": …, … } keyed by CLI label.
    Q_PROPERTY(QVariantMap config READ config NOTIFY configChanged)
    // A setting changed while connected; it takes effect after reconnecting.
    Q_PROPERTY(bool needsReconnect READ needsReconnect NOTIFY configChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect NOTIFY statusChanged)
    // Bytes through the tunnel since it came up; refreshed by refreshTunnelTraffic().
    // False in SOCKS5 mode: there is no tunnel whose traffic could be counted.
    Q_PROPERTY(bool hasTunnel READ hasTunnel NOTIFY statusChanged)
    Q_PROPERTY(double tunnelBytesIn READ tunnelBytesIn NOTIFY tunnelTrafficChanged)
    Q_PROPERTY(double tunnelBytesOut READ tunnelBytesOut NOTIFY tunnelTrafficChanged)
    Q_PROPERTY(QVariantMap license READ license NOTIFY licenseChanged)
    Q_PROPERTY(bool loginPending READ loginPending NOTIFY loginPendingChanged)
    Q_PROPERTY(QObject *locations READ locations CONSTANT)
    // Excluded apps: [{ id, name, icon, desktop_file, exe, cmdline_match, enabled, bytes_out, bytes_in }]
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    Q_PROPERTY(bool appsLoaded READ appsLoaded NOTIFY appsChanged)
    // The same lists as models that update row by row, so views keep their scroll position.
    Q_PROPERTY(QObject *siteExclusionModel READ siteExclusionModel CONSTANT)
    Q_PROPERTY(QObject *appModel READ appModel CONSTANT)
    // Bumped whenever new statistics arrive; false availability means an agvd without them.
    Q_PROPERTY(int statisticsRevision READ statisticsRevision NOTIFY statisticsChanged)
    Q_PROPERTY(bool statisticsAvailable READ statisticsAvailable NOTIFY statisticsChanged)

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
    QString appExclusionMode() const
    {
        const QString mode = m_props.value(QStringLiteral("AppExclusionMode")).toString();
        return mode.isEmpty() ? QStringLiteral("bypass") : mode;
    }
    bool startAtBoot() const { return m_props.value(QStringLiteral("StartAtBoot")).toBool(); }
    bool autoConnect() const { return m_props.value(QStringLiteral("AutoConnect")).toBool(); }
    QVariantMap config() const { return m_config; }
    bool needsReconnect() const { return m_needsReconnect && connected(); }
    bool hasTunnel() const { return !m_props.value(QStringLiteral("TunDevice")).toString().isEmpty(); }
    double tunnelBytesIn() const { return m_tunnelIn; }
    double tunnelBytesOut() const { return m_tunnelOut; }
    QVariantMap license() const { return m_license; }
    bool loginPending() const { return m_loginPending; }
    QObject *locations() const;
    QVariantList apps() const { return m_apps; }
    bool appsLoaded() const { return m_appsLoaded; }
    QObject *siteExclusionModel() const;
    QObject *appModel() const;
    LocationModel *locationModel() const { return m_locations; }
    int statisticsRevision() const { return m_statisticsRevision; }
    bool statisticsAvailable() const { return m_statisticsAvailable; }

    // Connects to a location (city, country or ISO code; empty: the last used one).
    Q_INVOKABLE void connectTo(const QString &location = QString());
    Q_INVOKABLE void connectFastest();
    Q_INVOKABLE void disconnectVpn();
    Q_INVOKABLE void toggle();
    // Stops a VPN started outside agv-gui, then connects through agvd.
    Q_INVOKABLE void takeOver();

    Q_INVOKABLE void refreshLocations();
    Q_INVOKABLE void refreshSiteExclusions();
    // Adds what was typed or pasted; with includeSubdomains "domain.net" brings "*.domain.net".
    Q_INVOKABLE void addSiteExclusions(const QString &text, bool includeSubdomains = true);
    // Live check of the input: { entries: [what would be added], problems: [explanations] }.
    Q_INVOKABLE QVariantMap checkSites(const QString &text, bool includeSubdomains = true) const;
    Q_INVOKABLE void setSiteExclusionEnabled(const QString &item, bool enabled);
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

    Q_INVOKABLE void setAppExclusionMode(const QString &mode);
    Q_INVOKABLE void refreshConfig();
    // key is a "config set-*" name without the prefix: mode, protocol, socks-port, dns, …
    Q_INVOKABLE void setConfig(const QString &key, const QString &value);
    // Disconnects and connects to the same location, so new settings apply.
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void setStartAtBoot(bool enabled);
    Q_INVOKABLE void setAutoConnect(bool enabled);
    // Reads the tunnel interface counters from sysfs (cheap; called by the UI while visible).
    Q_INVOKABLE void refreshTunnelTraffic();

    Q_INVOKABLE void refreshLicense();
    Q_INVOKABLE void login();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void importLogin();

    Q_INVOKABLE void refreshStatistics();
    Q_INVOKABLE void resetStatistics();
    // Totals, chart, locations, apps and sessions for "today", "week", "month" or "all".
    Q_INVOKABLE QVariantMap statisticsFor(const QString &period) const;

signals:
    void availableChanged();
    void statusChanged();
    void busyChanged();
    void siteExclusionsChanged();
    void licenseChanged();
    void appsChanged();
    void tunnelTrafficChanged();
    void configChanged();
    void loginPendingChanged();
    void statisticsChanged();
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
    KeyedListModel *m_siteModel;
    KeyedListModel *m_appModel;
    bool m_available = false;
    int m_busy = 0;
    QVariantMap m_props;
    int m_externalVpnCount = 0;
    QStringList m_siteExclusions;  // in effect
    QHash<QString, QStringList> m_siteGroups;  // site → entries folded under it
    bool m_siteExclusionsLoaded = false;
    QVariantMap m_license;
    AgvAppExclusionList m_appRules;
    QHash<QString, QPair<quint64, quint64>> m_traffic;
    QVariantList m_apps;
    bool m_appsLoaded = false;
    QVariantMap m_config;
    bool m_needsReconnect = false;
    double m_tunnelIn = 0;
    double m_tunnelOut = 0;
    bool m_loginPending = false;
    QJsonObject m_statistics;
    int m_statisticsRevision = 0;
    bool m_statisticsAvailable = true;
};

} // namespace agv
