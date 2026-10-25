#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>

#include <functional>

namespace agv {

inline const QString ActionControl = QStringLiteral("org.agv.daemon.control");
inline const QString ActionConfigure = QStringLiteral("org.agv.daemon.configure");

// Asks polkit whether a D-Bus caller may perform an action, talking to
// org.freedesktop.PolicyKit1 directly instead of linking polkit-qt.
class PolkitAuthorizer : public QObject
{
    Q_OBJECT

public:
    // With enabled == false every check succeeds (development on the session bus).
    PolkitAuthorizer(QDBusConnection bus, bool enabled, QObject *parent = nullptr);

    using Callback = std::function<void(bool authorized, const QString &error)>;

    // sender is the caller's unique bus name; may show an authentication dialog to the caller.
    void check(const QString &sender, const QString &actionId, Callback callback);

private:
    QDBusConnection m_bus;
    bool m_enabled;
};

} // namespace agv
