#pragma once

#include "routingengine.h"

#include <QString>

namespace agv {

// Keeps traffic from leaking past the VPN while it should be up, including the moments it drops
// and reconnects. A separate nftables table, so it comes and goes without touching the app
// exclusion rules; agvd removes it when it stops (and "agvd --cleanup" too).
//
// Bypass mode: only the tunnel, loopback, agvd with adguardvpn-cli (to reconnect), the excluded
// apps (they skip the VPN by design), DNS from systemd-resolved and, optionally, the local
// network get out; everything else is rejected, forwarded traffic of containers and VMs too.
// VPN-only mode: the listed apps are the ones meant for the VPN, so only their packets are
// rejected when they would leave outside the tunnel.
class KillSwitch
{
public:
    static inline const QString TableName = QStringLiteral("agvd-killswitch");

    struct Options {
        QString tunDevice = QStringLiteral("tun0");
        bool allowLan = true;
        RoutingEngine::Mode mode = RoutingEngine::Mode::Bypass;
        QString ownCgroup;       // "system.slice/agvd.service"; empty: not matched
        QString resolvedCgroup;  // "system.slice/systemd-resolved.service" when it exists

        bool operator==(const Options &) const = default;
    };

    explicit KillSwitch(SystemCommands *commands);

    // Loads or replaces the table; returns false (lastError()) when nft refuses it.
    bool engage(const Options &options);
    void release();
    bool isEngaged() const { return m_engaged; }
    QString lastError() const { return m_lastError; }

    // The nftables script engage() loads; public for tests.
    static QByteArray buildRuleset(const Options &options);

private:
    SystemCommands *m_commands;
    bool m_engaged = false;
    Options m_options;
    QString m_lastError;
};

} // namespace agv
