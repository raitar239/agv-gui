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

    struct Result {
        bool authorized = false;
        // Authentication would have been possible but interaction was not allowed.
        bool challenge = false;
        QString message;
    };
    using Callback = std::function<void(const Result &)>;

    // sender is the caller's unique bus name. With allowInteraction polkit may show an
    // authentication dialog to the caller; pass the caller's own D-Bus flag
    // (QDBusMessage::isInteractiveAuthorizationAllowed), never force it.
    void check(const QString &sender, const QString &actionId, bool allowInteraction,
               Callback callback);

private:
    QDBusConnection m_bus;
    bool m_enabled;
};

} // namespace agv
