#pragma once

#include <QObject>
#include <QString>

namespace agv {

class DaemonClient;

// Desktop notifications (org.freedesktop.Notifications) for what happens without the user:
// the VPN dropped, agvd reconnected it, the kill switch is holding traffic back. Connecting and
// disconnecting by hand stay quiet. Can be switched off in Settings.
class Notifier : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit Notifier(DaemonClient *daemon, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

signals:
    void enabledChanged();

private:
    void onStatusChanged();
    void notify(const QString &summary, const QString &body, bool urgent);

    DaemonClient *m_daemon;
    bool m_enabled = true;
    bool m_wasReconnecting = false;
    bool m_wasKillSwitchActive = false;
    uint m_lastId = 0;  // replaced instead of stacking up
};

} // namespace agv
