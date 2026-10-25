#include "systemdunit.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>

namespace agv {

namespace {

const QString Service = QStringLiteral("org.freedesktop.systemd1");
const QString Path = QStringLiteral("/org/freedesktop/systemd1");
const QString Manager = QStringLiteral("org.freedesktop.systemd1.Manager");

QDBusMessage managerCall(const QString &method)
{
    return QDBusMessage::createMethodCall(Service, Path, Manager, method);
}

} // namespace

SystemdUnit::SystemdUnit(QDBusConnection bus, QString unit, QObject *parent)
    : QObject(parent)
    , m_bus(std::move(bus))
    , m_unit(std::move(unit))
{
}

void SystemdUnit::refresh()
{
    QDBusMessage call = managerCall(QStringLiteral("GetUnitFileState"));
    call << m_unit;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        m_available = reply.type() != QDBusMessage::ErrorMessage;
        // "enabled", "enabled-runtime", "linked", "alias" all start the unit at boot here.
        const bool enabled = m_available
            && reply.arguments().value(0).toString().startsWith(QLatin1String("enabled"));
        if (enabled != m_enabled) {
            m_enabled = enabled;
            emit enabledChanged();
        }
    });
}

void SystemdUnit::setEnabled(bool enabled, Done done)
{
    QDBusMessage call = managerCall(enabled ? QStringLiteral("EnableUnitFiles") : QStringLiteral("DisableUnitFiles"));
    call << QStringList{m_unit} << false; // runtime: persist across reboots
    if (enabled)
        call << false;                    // force: do not replace foreign symlinks

    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, done = std::move(done)](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            done(reply.errorMessage());
            return;
        }
        // Like systemctl: reload so systemd sees the new symlinks, then report the resulting state.
        auto *reload = new QDBusPendingCallWatcher(m_bus.asyncCall(managerCall(QStringLiteral("Reload"))), this);
        connect(reload, &QDBusPendingCallWatcher::finished, this, [this, done](QDBusPendingCallWatcher *reload) {
            reload->deleteLater();
            refresh();
            done({});
        });
    });
}

} // namespace agv
