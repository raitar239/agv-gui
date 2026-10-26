#pragma once

#include "routingengine.h"

#include <QString>

namespace agv {

// Keeps DNS inside the VPN while it is connected. adguardvpn-cli changes the system DNS only
// through systemd-resolved or resolvconf; with NetworkManager writing /etc/resolv.conf itself
// lookups go to the router, outside the tunnel, and from there to the internet provider.
//
// A separate nftables table redirects plain DNS (port 53) of every program except the excluded
// apps and agvd with the CLI to AdGuard DNS, which is routed through the tunnel, where the CLI
// answers with its own DNS settings. Loopback resolvers (systemd-resolved, dnsmasq) are left
// alone; their upstream queries are redirected instead.
class DnsGuard
{
public:
    static inline const QString TableName = QStringLiteral("agvd-dns");
    static inline const QString Target4 = QStringLiteral("94.140.14.14");
    static inline const QString Target6 = QStringLiteral("2a10:50c0::ad1:ff");

    struct Options {
        QString tunDevice = QStringLiteral("tun0");
        QString ownCgroup;  // "system.slice/agvd.service"; empty: not matched

        bool operator==(const Options &) const = default;
    };

    explicit DnsGuard(SystemCommands *commands);

    bool engage(const Options &options);
    void release();
    bool isEngaged() const { return m_engaged; }
    QString lastError() const { return m_lastError; }

    static QByteArray buildRuleset(const Options &options);

private:
    SystemCommands *m_commands;
    bool m_engaged = false;
    Options m_options;
    QString m_lastError;
};

} // namespace agv
