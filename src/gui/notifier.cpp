#include "notifier.h"

#include "daemonclient.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QSettings>

namespace agv {

namespace {

const QString EnabledKey = QStringLiteral("notifications/enabled");

} // namespace

Notifier::Notifier(DaemonClient *daemon, QObject *parent)
    : QObject(parent)
    , m_daemon(daemon)
    , m_enabled(QSettings().value(EnabledKey, true).toBool())
{
    connect(m_daemon, &DaemonClient::statusChanged, this, &Notifier::onStatusChanged);
}

void Notifier::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    QSettings().setValue(EnabledKey, enabled);
    emit enabledChanged();
}

void Notifier::onStatusChanged()
{
    const bool reconnecting = m_daemon->reconnecting();
    const bool blocked = m_daemon->killSwitchActive();
    if (reconnecting && !m_wasReconnecting) {
        notify(tr("VPN connection lost"),
               blocked ? tr("Reconnecting… The kill switch blocks the internet until the VPN is back.")
                       : tr("Reconnecting… Until then traffic goes without the VPN."),
               true);
    } else if (!reconnecting && m_wasReconnecting && m_daemon->connected()) {
        notify(tr("VPN reconnected"), tr("Your traffic goes through the VPN again."), false);
    } else if (blocked && !m_wasKillSwitchActive && !reconnecting) {
        notify(tr("Internet blocked by the kill switch"),
               tr("The VPN is not connected. Connect again or turn the VPN off in AGV."), true);
    }
    m_wasReconnecting = reconnecting;
    m_wasKillSwitchActive = blocked;
}

void Notifier::notify(const QString &summary, const QString &body, bool urgent)
{
    if (!m_enabled)
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    const QVariantMap hints{{QStringLiteral("urgency"), QVariant::fromValue(uchar(urgent ? 2 : 1))},
                            {QStringLiteral("desktop-entry"), QStringLiteral("agv-gui")}};
    message.setArguments({QStringLiteral("AGV"), m_lastId, QStringLiteral("network-vpn"), summary, body,
                          QStringList(), hints, urgent ? 0 : 5000});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<uint> reply = *watcher;
        // No notification server is fine: the window and tray still show the state.
        if (reply.isValid())
            m_lastId = reply.value();
    });
}

} // namespace agv
