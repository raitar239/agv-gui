#include "daemonclient.h"

#include "locationmodel.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QUrl>

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

    loadAll();
}

QObject *DaemonClient::locations() const
{
    return m_locations;
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
