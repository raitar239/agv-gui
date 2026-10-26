#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QTimer;

namespace agv {

// The network the computer uses for the internet, as NetworkManager sees it (its primary
// connection). Without NetworkManager isAvailable() is false and nothing is reported.
class NetworkWatcher : public QObject
{
    Q_OBJECT

public:
    struct Network {
        QString uuid;   // NetworkManager connection profile
        QString name;   // profile name; for Wi-Fi usually the SSID
        QString type;   // "wifi", "ethernet", "other"; empty when offline
        bool open = false;  // Wi-Fi without encryption

        bool operator==(const Network &) const = default;
    };

    explicit NetworkWatcher(QDBusConnection bus, QObject *parent = nullptr);

    void start();
    bool isAvailable() const { return m_available; }
    Network current() const { return m_current; }

signals:
    // A different network (or none); settled for a moment, so connection steps do not count.
    void changed();

private slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void refresh();
    QVariant property(const QString &path, const QString &interface, const QString &name) const;

    QDBusConnection m_bus;
    QTimer *m_settle;
    bool m_available = false;
    Network m_current;
};

} // namespace agv
