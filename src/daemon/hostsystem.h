#pragma once

// Small pieces of host inspection the daemon needs, kept apart from VpnService so tests can point
// them at fake directories.

#include <QList>
#include <QString>

#include <sys/types.h>

namespace agv {

// A running "adguardvpn-cli connect" that the daemon did not start (a different HOME).
struct ExternalVpn {
    pid_t pid = 0;
    uid_t uid = 0;
    QString home;  // empty when /proc/<pid>/environ is not readable

    bool operator==(const ExternalVpn &other) const
    {
        return pid == other.pid && uid == other.uid && home == other.home;
    }
};

// Scans procDir (normally /proc) for adguardvpn-cli connect processes whose HOME differs from
// ownHome.
QList<ExternalVpn> findExternalVpns(const QString &ownHome, const QString &procDir = QStringLiteral("/proc"));

// True while procDir/<pid> still is an adguardvpn-cli connect process.
bool isVpnProcess(pid_t pid, const QString &procDir = QStringLiteral("/proc"));

// Path of the CLI's settings file (login token, exclusions, settings) for a HOME.
QString cliConfigPath(const QString &home);

// Copies another HOME's CLI settings file into ours. The source must be a regular file owned by
// ownerUid and is opened without following symlinks; the copy is written atomically with 0600.
// Returns an empty string on success, otherwise a human-readable reason.
QString importCliConfig(const QString &fromHome, uid_t ownerUid, const QString &toHome);

} // namespace agv
