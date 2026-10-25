#include "daemonclient.h"

#include "keyedlistmodel.h"
#include "locationmodel.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDesktopServices>
#include <QFile>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace agv {

namespace {

constexpr int CallTimeoutMs = 180'000; // connect can take a while, polkit dialogs longer

const QString PropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

} // namespace

DaemonClient::DaemonClient(QDBusConnection bus, QObject *parent)
    : QObject(parent)
    , m_bus(std::move(bus))
    , m_watcher(new QDBusServiceWatcher(DBusService, m_bus,
                                        QDBusServiceWatcher::WatchForOwnerChange, this))
    , m_locations(new LocationModel(this))
    , m_siteModel(new KeyedListModel("item", {"item"}, this))
    , m_appModel(new KeyedListModel("id", {"id", "name", "icon", "desktop_file", "exe", "cmdline_match", "enabled",
                                           "bytes_out", "bytes_in"}, this))
{
    registerDBusTypes();

    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
        setAvailable(!newOwner.isEmpty());
        if (!newOwner.isEmpty())
            loadAll();
    });

    m_bus.connect(DBusService, DBusPath, PropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                  SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    m_bus.connect(DBusService, DBusPath, DBusInterface, QStringLiteral("LoginUrl"), this,
                  SLOT(onLoginUrl(QString)));
    m_bus.connect(DBusService, DBusPath, DBusInterface, QStringLiteral("LoginFinished"), this,
                  SLOT(onLoginFinished(bool,QString)));
    m_bus.connect(DBusService, DBusPath, DBusInterface, QStringLiteral("SiteExclusionsChanged"), this,
                  SLOT(onSiteExclusionsChanged()));
    m_bus.connect(DBusService, DBusPath, DBusInterface, QStringLiteral("AppExclusionsChanged"), this,
                  SLOT(onAppExclusionsChanged()));

    loadAll();
}

QObject *DaemonClient::locations() const
{
    return m_locations;
}

QObject *DaemonClient::siteExclusionModel() const
{
    return m_siteModel;
}

QObject *DaemonClient::appModel() const
{
    return m_appModel;
}

bool DaemonClient::transitioning() const
{
    const QString s = state();
    return s == QLatin1String("connecting") || s == QLatin1String("disconnecting");
}

void DaemonClient::connectTo(const QString &location)
{
    call(QStringLiteral("Connect"), {location}, {}, true);
}

void DaemonClient::connectFastest()
{
    call(QStringLiteral("ConnectFastest"), {}, {}, true);
}

void DaemonClient::disconnectVpn()
{
    call(QStringLiteral("Disconnect"), {}, {}, true);
}

void DaemonClient::toggle()
{
    if (busy() || transitioning())
        return;
    if (connected())
        disconnectVpn();
    else if (m_externalVpnCount > 0)
        takeOver();
    else
        connectTo();
}

void DaemonClient::takeOver()
{
    call(QStringLiteral("StopExternalVpns"), {}, [this](const QDBusMessage &) { connectTo(); }, true);
}

void DaemonClient::refreshLocations()
{
    m_locations->setLoading(true);
    call(QStringLiteral("ListLocations"), {}, [this](const QDBusMessage &reply) {
        m_locations->setLocations(qdbus_cast<AgvLocationList>(reply.arguments().value(0)));
        m_locations->setLoading(false);
    });
}

void DaemonClient::refreshSiteExclusions()
{
    call(QStringLiteral("GetSiteExclusions"), {}, [this](const QDBusMessage &reply) {
        QStringList items = reply.arguments().value(0).toStringList();
        items.sort(Qt::CaseInsensitive);
        if (items != m_siteExclusions || !m_siteExclusionsLoaded) {
            m_siteExclusions = items;
            m_siteExclusionsLoaded = true;
            QList<QVariantMap> rows;
            for (const QString &item : items)
                rows.append({{QStringLiteral("item"), item}});
            m_siteModel->setRows(rows);
            emit siteExclusionsChanged();
        }
    });
}

void DaemonClient::addSiteExclusions(const QString &text)
{
    // Accept anything pasted: spaces, commas, new lines; strip schemes and paths from URLs.
    static const QRegularExpression separators(QStringLiteral("[\\s,;]+"));
    QStringList items;
    for (QString item : text.split(separators, Qt::SkipEmptyParts)) {
        const QUrl url(item);
        if (url.isValid() && !url.host().isEmpty() && !url.scheme().isEmpty())
            item = url.host();
        items.append(item.toLower());
    }
    if (items.isEmpty())
        return;
    call(QStringLiteral("AddSiteExclusions"), {items}, [this](const QDBusMessage &) { refreshSiteExclusions(); });
}

void DaemonClient::removeSiteExclusion(const QString &item)
{
    call(QStringLiteral("RemoveSiteExclusions"), {QStringList{item}},
         [this](const QDBusMessage &) { refreshSiteExclusions(); });
}

void DaemonClient::setSiteExclusionMode(const QString &mode)
{
    call(QStringLiteral("SetSiteExclusionMode"), {mode}, [this](const QDBusMessage &) { refreshSiteExclusions(); });
}

void DaemonClient::refreshApps()
{
    call(QStringLiteral("ListAppExclusions"), {}, [this](const QDBusMessage &reply) {
        m_appRules = qdbus_cast<AgvAppExclusionList>(reply.arguments().value(0));
        m_appsLoaded = true;
        publishApps();
    });
}

void DaemonClient::refreshAppTraffic()
{
    if (!m_available || m_appRules.isEmpty())
        return;
    call(QStringLiteral("GetAppTraffic"), {}, [this](const QDBusMessage &reply) {
        const auto traffic = qdbus_cast<AgvAppTrafficList>(reply.arguments().value(0));
        m_traffic.clear();
        for (const AgvAppTraffic &entry : traffic)
            m_traffic.insert(entry.id, {entry.bytesOut, entry.bytesIn});
        publishApps();
    });
}

void DaemonClient::addApp(const QVariantMap &fields)
{
    // Only send what the daemon knows; empty strings are fine.
    static const QStringList keys{QStringLiteral("name"), QStringLiteral("icon"), QStringLiteral("desktop_file"),
                                  QStringLiteral("exe"), QStringLiteral("cmdline_match")};
    QVariantMap rule;
    for (const QString &key : keys) {
        if (fields.contains(key))
            rule.insert(key, fields.value(key).toString());
    }
    call(QStringLiteral("AddAppExclusion"), {rule}, [this, name = rule.value(QStringLiteral("name")).toString()](const QDBusMessage &) {
        emit info(appExclusionMode() == QLatin1String("vpn-only")
                      ? tr("%1 now uses the VPN. Restart it if it was already running.").arg(name)
                      : tr("%1 now bypasses the VPN. Restart it if it was already running.").arg(name));
        refreshApps();
    });
}

void DaemonClient::setAppEnabled(const QString &id, bool enabled)
{
    call(QStringLiteral("UpdateAppExclusion"), {id, QVariantMap{{QStringLiteral("enabled"), enabled}}},
         [this](const QDBusMessage &) { refreshApps(); });
}

void DaemonClient::removeApp(const QString &id)
{
    call(QStringLiteral("RemoveAppExclusion"), {id}, [this](const QDBusMessage &) { refreshApps(); });
}

bool DaemonClient::isAppExcluded(const QString &desktopFile, const QString &exe) const
{
    for (const AgvAppExclusion &app : m_appRules) {
        const QString file = app.rule.value(QStringLiteral("desktop_file")).toString();
        const QString program = app.rule.value(QStringLiteral("exe")).toString();
        if ((!desktopFile.isEmpty() && file == desktopFile) || (!exe.isEmpty() && program == exe))
            return true;
    }
    return false;
}

void DaemonClient::setAppExclusionMode(const QString &mode)
{
    call(QStringLiteral("SetAppExclusionMode"), {mode});
}

void DaemonClient::refreshConfig()
{
    call(QStringLiteral("GetConfig"), {}, [this](const QDBusMessage &reply) {
        const auto config = qdbus_cast<AgvConfigMap>(reply.arguments().value(0));
        m_config.clear();
        for (auto it = config.cbegin(); it != config.cend(); ++it) {
            m_config.insert(it.key(), QVariantMap{{QStringLiteral("value"), it->value},
                                                  {QStringLiteral("isDefault"), it->isDefault}});
        }
        emit configChanged();
    });
}

void DaemonClient::setConfig(const QString &key, const QString &value)
{
    call(QStringLiteral("SetConfig"), {key, value}, [this](const QDBusMessage &) {
        if (connected()) {
            m_needsReconnect = true;
            emit info(tr("Saved. Reconnect to apply it."));
        } else {
            emit info(tr("Saved. Applies on the next connection."));
        }
        refreshConfig();
    });
}

void DaemonClient::reconnect()
{
    if (!connected() || busy())
        return;
    const QString location = this->location();
    call(QStringLiteral("Disconnect"), {}, [this, location](const QDBusMessage &) {
        m_needsReconnect = false;
        emit configChanged();
        connectTo(location);
    }, true);
}

void DaemonClient::setStartAtBoot(bool enabled)
{
    call(QStringLiteral("SetStartAtBoot"), {enabled});
}

void DaemonClient::setAutoConnect(bool enabled)
{
    call(QStringLiteral("SetAutoConnect"), {enabled});
}

void DaemonClient::refreshTunnelTraffic()
{
    const QString device = m_props.value(QStringLiteral("TunDevice")).toString();
    // Interface names never contain '/', but this path is built from D-Bus data.
    if (!connected() || device.isEmpty() || device.contains(QLatin1Char('/'))) {
        if (m_tunnelIn != 0 || m_tunnelOut != 0) {
            m_tunnelIn = m_tunnelOut = 0;
            emit tunnelTrafficChanged();
        }
        return;
    }
    auto read = [&device](const char *counter) {
        QFile file(QStringLiteral("/sys/class/net/%1/statistics/%2").arg(device, QLatin1String(counter)));
        return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed().toDouble() : 0.0;
    };
    const double in = read("rx_bytes");
    const double out = read("tx_bytes");
    if (in != m_tunnelIn || out != m_tunnelOut) {
        m_tunnelIn = in;
        m_tunnelOut = out;
        emit tunnelTrafficChanged();
    }
}

void DaemonClient::onAppExclusionsChanged()
{
    refreshApps();
}

void DaemonClient::publishApps()
{
    m_apps.clear();
    for (const AgvAppExclusion &app : m_appRules) {
        QVariantMap entry = app.rule;
        entry.insert(QStringLiteral("id"), app.id);
        const auto traffic = m_traffic.value(app.id);
        // QML numbers are doubles; byte counts stay exact far beyond what an app will use.
        entry.insert(QStringLiteral("bytes_out"), double(traffic.first));
        entry.insert(QStringLiteral("bytes_in"), double(traffic.second));
        m_apps.append(entry);
    }
    std::sort(m_apps.begin(), m_apps.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().localeAwareCompare(
                   b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    QList<QVariantMap> rows;
    for (const QVariant &app : std::as_const(m_apps))
        rows.append(app.toMap());
    m_appModel->setRows(rows);
    emit appsChanged();
}

void DaemonClient::refreshLicense()
{
    call(QStringLiteral("GetLicense"), {}, [this](const QDBusMessage &reply) {
        m_license = qdbus_cast<QVariantMap>(reply.arguments().value(0));
        emit licenseChanged();
    });
}

void DaemonClient::login()
{
    call(QStringLiteral("Login"), {}, [this](const QDBusMessage &) {
        m_loginPending = true;
        emit loginPendingChanged();
    });
}

void DaemonClient::cancelLogin()
{
    call(QStringLiteral("CancelLogin"));
}

void DaemonClient::logout()
{
    call(QStringLiteral("Logout"), {}, [this](const QDBusMessage &) {
        m_license.clear();
        emit licenseChanged();
    }, true);
}

void DaemonClient::importLogin()
{
    call(QStringLiteral("ImportLogin"), {}, [this](const QDBusMessage &) {
        emit info(tr("Signed in with your adguardvpn-cli account"));
        loadAll();
    }, true);
}

void DaemonClient::onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface == DBusInterface)
        applyProperties(changed);
}

void DaemonClient::onLoginUrl(const QString &url)
{
    // Only ever hand a web link to the browser.
    const QUrl link(url);
    if (link.scheme() != QLatin1String("https")) {
        emit errorOccurred(tr("The login link from AdGuard VPN looks wrong: %1").arg(url));
        return;
    }
    if (!QDesktopServices::openUrl(link))
        emit info(tr("Open this link to sign in: %1").arg(url));
    else
        emit info(tr("Confirm the sign-in in your browser"));
}

void DaemonClient::onLoginFinished(bool success, const QString &message)
{
    m_loginPending = false;
    emit loginPendingChanged();
    if (success) {
        emit info(tr("Signed in"));
        loadAll();
    } else if (!message.isEmpty()) {
        emit errorOccurred(tr("Sign-in failed: %1").arg(message));
    }
}

void DaemonClient::onSiteExclusionsChanged()
{
    refreshSiteExclusions();
}

void DaemonClient::call(const QString &method, const QVariantList &args, Reply onSuccess, bool trackBusy)
{
    QDBusMessage message = QDBusMessage::createMethodCall(DBusService, DBusPath, DBusInterface, method);
    message.setArguments(args);
    // Let polkit ask for a password where the action needs one.
    message.setInteractiveAuthorizationAllowed(true);

    if (trackBusy)
        setBusy(true);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, method, onSuccess = std::move(onSuccess), trackBusy](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        if (trackBusy)
            setBusy(false);
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            if (method == QLatin1String("ListLocations"))
                m_locations->setLoading(false);
            emit errorOccurred(describe(reply));
            return;
        }
        if (onSuccess)
            onSuccess(reply);
    });
}

void DaemonClient::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    emit availableChanged();
}

void DaemonClient::loadAll()
{
    QDBusMessage message = QDBusMessage::createMethodCall(DBusService, DBusPath, PropertiesInterface,
                                                          QStringLiteral("GetAll"));
    message << DBusInterface;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        setAvailable(reply.type() != QDBusMessage::ErrorMessage);
        if (!m_available)
            return;
        applyProperties(qdbus_cast<QVariantMap>(reply.arguments().value(0)));
        refreshSiteExclusions();
        refreshApps();
        refreshConfig();
        if (loggedIn()) {
            refreshLocations();
            refreshLicense();
        }
    });
}

void DaemonClient::applyProperties(const QVariantMap &props)
{
    const bool wasLoggedIn = loggedIn();
    for (auto it = props.cbegin(); it != props.cend(); ++it) {
        if (it.key() == QLatin1String("ExternalVpns")) {
            const auto list = qdbus_cast<AgvExternalVpnList>(it.value());
            m_externalVpnCount = int(list.size());
        } else {
            m_props.insert(it.key(), it.value());
        }
    }
    if (!connected() && !transitioning() && m_needsReconnect) {
        m_needsReconnect = false; // the next connection uses the new settings anyway
        emit configChanged();
    }
    emit statusChanged();
    if (props.contains(QStringLiteral("SiteExclusionMode")))
        emit siteExclusionsChanged();

    if (!wasLoggedIn && loggedIn()) {
        refreshLocations();
        refreshLicense();
    }
}

void DaemonClient::setBusy(bool busy)
{
    const bool before = this->busy();
    m_busy += busy ? 1 : -1;
    if (before != this->busy())
        emit busyChanged();
}

QString DaemonClient::describe(const QDBusMessage &error)
{
    const QString name = error.errorName();
    if (name == QLatin1String("org.agv.Error.NotLoggedIn"))
        return tr("Sign in to AdGuard VPN first");
    if (name == QLatin1String("org.agv.Error.NotAuthorized")
        || name == QLatin1String("org.freedesktop.DBus.Error.AccessDenied"))
        return tr("Not allowed: %1").arg(error.errorMessage());
    if (name == QLatin1String("org.freedesktop.DBus.Error.InteractiveAuthorizationRequired"))
        return tr("This needs an administrator password");
    if (name == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")
        || name == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner"))
        return tr("The agvd service is not running");
    if (name == QLatin1String("org.freedesktop.DBus.Error.NoReply")
        || name == QLatin1String("org.freedesktop.DBus.Error.Timeout"))
        return tr("The agvd service did not answer in time");
    return error.errorMessage();
}

} // namespace agv
