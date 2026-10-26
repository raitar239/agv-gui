#include "dbustypes.h"

#include <QDBusMetaType>

QDBusArgument &operator<<(QDBusArgument &arg, const AgvLocation &value)
{
    arg.beginStructure();
    arg << value.iso << value.country << value.city << value.ping;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, AgvLocation &value)
{
    arg.beginStructure();
    arg >> value.iso >> value.country >> value.city >> value.ping;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const AgvConfigValue &value)
{
    arg.beginStructure();
    arg << value.value << value.isDefault;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, AgvConfigValue &value)
{
    arg.beginStructure();
    arg >> value.value >> value.isDefault;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const AgvAppExclusion &value)
{
    arg.beginStructure();
    arg << value.id << value.rule;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, AgvAppExclusion &value)
{
    arg.beginStructure();
    arg >> value.id >> value.rule;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const AgvAppTraffic &value)
{
    arg.beginStructure();
    arg << value.id << value.bytesOut << value.bytesIn;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, AgvAppTraffic &value)
{
    arg.beginStructure();
    arg >> value.id >> value.bytesOut >> value.bytesIn;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const AgvSiteExclusion &value)
{
    arg.beginStructure();
    arg << value.item << value.enabled;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, AgvSiteExclusion &value)
{
    arg.beginStructure();
    arg >> value.item >> value.enabled;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const AgvExternalVpn &value)
{
    arg.beginStructure();
    arg << value.pid << value.uid << value.home;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, AgvExternalVpn &value)
{
    arg.beginStructure();
    arg >> value.pid >> value.uid >> value.home;
    arg.endStructure();
    return arg;
}

namespace agv {

void registerDBusTypes()
{
    qDBusRegisterMetaType<AgvLocation>();
    qDBusRegisterMetaType<AgvLocationList>();
    qDBusRegisterMetaType<AgvConfigValue>();
    qDBusRegisterMetaType<AgvConfigMap>();
    qDBusRegisterMetaType<AgvAppExclusion>();
    qDBusRegisterMetaType<AgvAppExclusionList>();
    qDBusRegisterMetaType<AgvAppTraffic>();
    qDBusRegisterMetaType<AgvAppTrafficList>();
    qDBusRegisterMetaType<AgvExternalVpn>();
    qDBusRegisterMetaType<AgvExternalVpnList>();
    qDBusRegisterMetaType<AgvSiteExclusion>();
    qDBusRegisterMetaType<AgvSiteExclusionList>();
}

} // namespace agv
