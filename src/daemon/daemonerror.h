#pragma once

#include <QString>

namespace agv {

// Error names returned over D-Bus. An empty name means success.
namespace errors {
inline const QString NotLoggedIn = QStringLiteral("org.agv.Error.NotLoggedIn");
inline const QString CliFailed = QStringLiteral("org.agv.Error.CliFailed");
inline const QString CliNotFound = QStringLiteral("org.agv.Error.CliNotFound");
inline const QString Timeout = QStringLiteral("org.agv.Error.Timeout");
inline const QString UnexpectedOutput = QStringLiteral("org.agv.Error.UnexpectedOutput");
inline const QString InvalidArgument = QStringLiteral("org.agv.Error.InvalidArgument");
inline const QString Busy = QStringLiteral("org.agv.Error.Busy");
inline const QString NotAuthorized = QStringLiteral("org.agv.Error.NotAuthorized");
inline const QString NotImplemented = QStringLiteral("org.agv.Error.NotImplemented");
inline const QString NotFound = QStringLiteral("org.agv.Error.NotFound");
inline const QString NotSupported = QStringLiteral("org.agv.Error.NotSupported");
inline const QString SystemFailed = QStringLiteral("org.agv.Error.SystemFailed");
} // namespace errors

struct Error {
    QString name;
    QString message;

    bool isOk() const { return name.isEmpty(); }
};

} // namespace agv
