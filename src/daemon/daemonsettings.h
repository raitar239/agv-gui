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
    bool killSwitchAllowLan() const { return m_values.value(QStringLiteral("kill_switch_allow_lan"), true).toBool(); }
    void setKillSwitchAllowLan(bool on) { m_values.insert(QStringLiteral("kill_switch_allow_lan"), on); }

private:
    QString m_path;
    QVariantMap m_values;
};

} // namespace agv
