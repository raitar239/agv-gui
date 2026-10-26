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
    QString uuid;
    const QString active = pathOf(property(RootPath, RootInterface, QStringLiteral("PrimaryConnection")));
    if (!active.isEmpty() && active != QLatin1String("/"))
        uuid = property(active, QStringLiteral("org.freedesktop.NetworkManager.Connection.Active"), QStringLiteral("Uuid")).toString();
    if (uuid == m_current)
        return;
    m_current = uuid;
    emit changed();
}

} // namespace agv
