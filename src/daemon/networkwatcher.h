#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QTimer;

namespace agv {

// Which network the computer uses for the internet, as NetworkManager sees it (its primary
// connection), to notice when it changes. Without NetworkManager nothing is reported.
class NetworkWatcher : public QObject
{
    Q_OBJECT

public:
    explicit NetworkWatcher(QDBusConnection bus, QObject *parent = nullptr);

    void start();
    bool isAvailable() const { return m_available; }
    // Connected to some network (NetworkManager has a primary connection).
    bool isOnline() const { return !m_current.isEmpty(); }

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
    QString m_current;  // the primary connection's profile uuid
};

} // namespace agv
