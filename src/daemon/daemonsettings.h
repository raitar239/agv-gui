#pragma once

#include <QString>
#include <QVariantMap>

namespace agv {

// The daemon's own preferences (settings.json in its data directory), written atomically.
class DaemonSettings
{
public:
    explicit DaemonSettings(QString path);

    void load();
    bool save() const;

    bool autoConnect() const { return m_values.value(QStringLiteral("auto_connect")).toBool(); }
    void setAutoConnect(bool on) { m_values.insert(QStringLiteral("auto_connect"), on); }
    bool killSwitch() const { return m_values.value(QStringLiteral("kill_switch")).toBool(); }
    void setKillSwitch(bool on) { m_values.insert(QStringLiteral("kill_switch"), on); }
    // Printers, NAS and the router stay reachable while the kill switch blocks the internet.
    // Redirect plain DNS into the tunnel while connected (DnsGuard). On unless turned off.
    bool keepDnsInVpn() const { return m_values.value(QStringLiteral("keep_dns_in_vpn"), true).toBool(); }
    void setKeepDnsInVpn(bool on) { m_values.insert(QStringLiteral("keep_dns_in_vpn"), on); }
    // Network rules (NetworkRules): connect on Wi-Fi that is not trusted, disconnect on trusted.
    bool wifiAutoConnect() const { return m_values.value(QStringLiteral("wifi_auto_connect")).toBool(); }
    void setWifiAutoConnect(bool on) { m_values.insert(QStringLiteral("wifi_auto_connect"), on); }
    bool trustedDisconnect() const { return m_values.value(QStringLiteral("trusted_disconnect")).toBool(); }
    void setTrustedDisconnect(bool on) { m_values.insert(QStringLiteral("trusted_disconnect"), on); }
    // NetworkManager connection uuid → its name when it was trusted.
    QVariantMap trustedNetworks() const { return m_values.value(QStringLiteral("trusted_networks")).toMap(); }
    void setTrustedNetworks(const QVariantMap &networks) { m_values.insert(QStringLiteral("trusted_networks"), networks); }
    bool killSwitchAllowLan() const { return m_values.value(QStringLiteral("kill_switch_allow_lan"), true).toBool(); }
    void setKillSwitchAllowLan(bool on) { m_values.insert(QStringLiteral("kill_switch_allow_lan"), on); }

private:
    QString m_path;
    QVariantMap m_values;
};

} // namespace agv
