#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>

#include <functional>

namespace agv {

// Enables or disables a unit file through systemd's D-Bus API (agvd runs as root, so systemd
// accepts it without its own polkit prompt; agvd checks its callers itself).
class SystemdUnit : public QObject
{
    Q_OBJECT

public:
    SystemdUnit(QDBusConnection bus, QString unit, QObject *parent = nullptr);

    // Reads the unit file state; enabledChanged() follows when it differs from the cached value.
    void refresh();
    bool isEnabled() const { return m_enabled; }
    bool isAvailable() const { return m_available; }

    using Done = std::function<void(const QString &error)>;
    void setEnabled(bool enabled, Done done);

signals:
    void enabledChanged();

private:
    QDBusConnection m_bus;
    QString m_unit;
    bool m_enabled = false;
    bool m_available = false;
};

} // namespace agv
