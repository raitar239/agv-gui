#include "networkrules.h"

#include "daemonsettings.h"
#include "vpnservice.h"

namespace agv {

NetworkRules::NetworkRules(NetworkWatcher *watcher, VpnService *vpn, DaemonSettings *settings, QObject *parent)
    : QObject(parent)
    , m_watcher(watcher)
    , m_vpn(vpn)
    , m_settings(settings)
{
    connect(m_watcher, &NetworkWatcher::changed, this, &NetworkRules::onNetworkChanged);
}

QVariantMap NetworkRules::currentNetwork() const
{
    const NetworkWatcher::Network network = m_watcher->current();
    return {{QStringLiteral("available"), m_watcher->isAvailable()},
            {QStringLiteral("uuid"), network.uuid},
            {QStringLiteral("name"), network.name},
            {QStringLiteral("type"), network.type},
            {QStringLiteral("open"), network.open},
            {QStringLiteral("trusted"), !network.uuid.isEmpty() && m_settings->trustedNetworks().contains(network.uuid)}};
}

void NetworkRules::onNetworkChanged()
{
    emit changed();
    if (m_watcher->current().type.isEmpty())
        return;
    // A new network is the best moment for a pending reconnect.
    m_vpn->networkChanged();
    apply();
}

void NetworkRules::apply()
{
    const NetworkWatcher::Network network = m_watcher->current();
    if (network.type.isEmpty())
        return;
    const bool trusted = m_settings->trustedNetworks().contains(network.uuid);
    const auto state = m_vpn->state();
    const bool idle = state == VpnService::State::Disconnected || state == VpnService::State::Error;

    if (network.type == QLatin1String("wifi") && !trusted && m_settings->wifiAutoConnect() && idle
        && !m_vpn->reconnecting() && m_vpn->externalVpns().isEmpty()) {
        m_vpn->connectTo(QString(), [](const Error &) {});
    } else if (trusted && m_settings->trustedDisconnect()
               && (state == VpnService::State::Connected || m_vpn->reconnecting())) {
        m_vpn->disconnectVpn([](const Error &) {});
    }
}

} // namespace agv
