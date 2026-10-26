#include "networkwatcher.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QTimer>

namespace agv {

namespace {

const QString Service = QStringLiteral("org.freedesktop.NetworkManager");
const QString RootPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString RootInterface = QStringLiteral("org.freedesktop.NetworkManager");
constexpr uint ApPrivacy = 0x1;  // NM_802_11_AP_FLAGS_PRIVACY

QString pathOf(const QVariant &value)
{
    return value.value<QDBusObjectPath>().path();
}

} // namespace

NetworkWatcher::NetworkWatcher(QDBusConnection bus, QObject *parent)
    : QObject(parent)
    , m_bus(std::move(bus))
    , m_settle(new QTimer(this))
{
    m_settle->setSingleShot(true);
    m_settle->setInterval(2000);
    connect(m_settle, &QTimer::timeout, this, &NetworkWatcher::refresh);
}

void NetworkWatcher::start()
{
    m_bus.connect(Service, RootPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                  QStringLiteral("PropertiesChanged"), this,
                  SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    m_available = property(RootPath, RootInterface, QStringLiteral("Version")).isValid();
    if (m_available)
        refresh();
}

void NetworkWatcher::onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != RootInterface)
        return;
    if (changed.contains(QStringLiteral("PrimaryConnection")) || changed.contains(QStringLiteral("State"))) {
        m_available = true;
        m_settle->start();
    }
}

QVariant NetworkWatcher::property(const QString &path, const QString &interface, const QString &name) const
{
    // Short and blocking: NetworkManager is local, and a stuck one must not hold agvd up for long.
    QDBusMessage message = QDBusMessage::createMethodCall(Service, path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                          QStringLiteral("Get"));
    message.setArguments({interface, name});
    const QDBusReply<QDBusVariant> reply = m_bus.call(message, QDBus::Block, 2000);
    return reply.isValid() ? reply.value().variant() : QVariant();
}

void NetworkWatcher::refresh()
{
    Network network;
    const QString active = pathOf(property(RootPath, RootInterface, QStringLiteral("PrimaryConnection")));
    if (!active.isEmpty() && active != QLatin1String("/")) {
        const QString activeInterface = QStringLiteral("org.freedesktop.NetworkManager.Connection.Active");
        network.uuid = property(active, activeInterface, QStringLiteral("Uuid")).toString();
        network.name = property(active, activeInterface, QStringLiteral("Id")).toString();
        const QString type = property(active, activeInterface, QStringLiteral("Type")).toString();
        if (type == QLatin1String("802-11-wireless")) {
            network.type = QStringLiteral("wifi");
            const auto devices = qdbus_cast<QList<QDBusObjectPath>>(property(active, activeInterface, QStringLiteral("Devices")));
            if (!devices.isEmpty()) {
                const QString ap = pathOf(property(devices.first().path(),
                                                   QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless"),
                                                   QStringLiteral("ActiveAccessPoint")));
                if (!ap.isEmpty() && ap != QLatin1String("/")) {
                    const QString apInterface = QStringLiteral("org.freedesktop.NetworkManager.AccessPoint");
                    const uint flags = property(ap, apInterface, QStringLiteral("Flags")).toUInt();
                    const uint wpa = property(ap, apInterface, QStringLiteral("WpaFlags")).toUInt();
                    const uint rsn = property(ap, apInterface, QStringLiteral("RsnFlags")).toUInt();
                    network.open = !(flags & ApPrivacy) && wpa == 0 && rsn == 0;
                }
            }
        } else if (type == QLatin1String("802-3-ethernet")) {
            network.type = QStringLiteral("ethernet");
        } else {
            network.type = QStringLiteral("other");
        }
    }
    if (network == m_current)
        return;
    m_current = network;
    emit changed();
}

} // namespace agv
