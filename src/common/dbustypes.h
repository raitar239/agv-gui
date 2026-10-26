#pragma once

// Types used by the org.agv.Daemon1 D-Bus interface, shared by agvd and agv-gui.
// The names match the org.qtproject.QtDBus.QtTypeName annotations in dbus/org.agv.Daemon1.xml.

#include <QDBusArgument>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

// (sssi): iso, country, city, ping in ms (-1 when unknown)
struct AgvLocation {
    QString iso;
    QString country;
    QString city;
    int ping = -1;
};
using AgvLocationList = QList<AgvLocation>;

// (sb): value, is_default
struct AgvConfigValue {
    QString value;
    bool isDefault = false;
};
using AgvConfigMap = QMap<QString, AgvConfigValue>;

// (sa{sv}): id, rule
struct AgvAppExclusion {
    QString id;
    QVariantMap rule;
};
using AgvAppExclusionList = QList<AgvAppExclusion>;

// (stt): id, bytes_out, bytes_in
struct AgvAppTraffic {
    QString id;
    quint64 bytesOut = 0;
    quint64 bytesIn = 0;
};
using AgvAppTrafficList = QList<AgvAppTraffic>;

// (sb): entry, enabled
struct AgvSiteExclusion {
    QString item;
    bool enabled = true;
};
using AgvSiteExclusionList = QList<AgvSiteExclusion>;

// (uus): pid, uid, home ("" when unknown)
struct AgvExternalVpn {
    uint pid = 0;
    uint uid = 0;
    QString home;

    bool operator==(const AgvExternalVpn &) const = default;
};
using AgvExternalVpnList = QList<AgvExternalVpn>;

QDBusArgument &operator<<(QDBusArgument &arg, const AgvLocation &value);
const QDBusArgument &operator>>(const QDBusArgument &arg, AgvLocation &value);
QDBusArgument &operator<<(QDBusArgument &arg, const AgvConfigValue &value);
const QDBusArgument &operator>>(const QDBusArgument &arg, AgvConfigValue &value);
QDBusArgument &operator<<(QDBusArgument &arg, const AgvAppExclusion &value);
const QDBusArgument &operator>>(const QDBusArgument &arg, AgvAppExclusion &value);
QDBusArgument &operator<<(QDBusArgument &arg, const AgvAppTraffic &value);
const QDBusArgument &operator>>(const QDBusArgument &arg, AgvAppTraffic &value);
QDBusArgument &operator<<(QDBusArgument &arg, const AgvSiteExclusion &value);
const QDBusArgument &operator>>(const QDBusArgument &arg, AgvSiteExclusion &value);
QDBusArgument &operator<<(QDBusArgument &arg, const AgvExternalVpn &value);
const QDBusArgument &operator>>(const QDBusArgument &arg, AgvExternalVpn &value);

Q_DECLARE_METATYPE(AgvLocation)
Q_DECLARE_METATYPE(AgvConfigValue)
Q_DECLARE_METATYPE(AgvAppExclusion)
Q_DECLARE_METATYPE(AgvAppTraffic)
Q_DECLARE_METATYPE(AgvExternalVpn)
Q_DECLARE_METATYPE(AgvSiteExclusion)

namespace agv {

inline const QString DBusService = QStringLiteral("org.agv.Daemon1");
inline const QString DBusPath = QStringLiteral("/org/agv/Daemon");
inline const QString DBusInterface = QStringLiteral("org.agv.Daemon1");

// Must be called once before any of the types above crosses D-Bus.
void registerDBusTypes();

} // namespace agv
