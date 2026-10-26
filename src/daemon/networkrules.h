#pragma once

#include "networkwatcher.h"

#include <QObject>
#include <QVariantMap>

namespace agv {

class DaemonSettings;
class VpnService;

// What to do when the computer joins a network: connect the VPN on Wi-Fi that is not trusted,
// optionally disconnect on trusted networks, and retry a pending reconnect right away. Acts only
// when the network changes, so a manual connect or disconnect holds until the next one.
class NetworkRules : public QObject
{
    Q_OBJECT

public:
    NetworkRules(NetworkWatcher *watcher, VpnService *vpn, DaemonSettings *settings, QObject *parent = nullptr);

    // { available, uuid, name, type ("wifi", "ethernet", "other", "" offline), open, trusted }
    QVariantMap currentNetwork() const;
    // Applies the rules to the network the computer is on now (after the settings changed).
    void apply();

signals:
    void changed();

private:
    void onNetworkChanged();

    NetworkWatcher *m_watcher;
    VpnService *m_vpn;
    DaemonSettings *m_settings;
};

} // namespace agv
