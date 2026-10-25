#pragma once

#include "appstore.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <sys/types.h>

namespace agv {

// What the daemon reads about a process to decide whether it belongs to an excluded app.
struct ProcessInfo {
    pid_t pid = 0;
    QString exe;         // resolved /proc/<pid>/exe
    QStringList argv;    // /proc/<pid>/cmdline
    QString cgroup;      // unified-hierarchy path without the leading "/"
    qint64 uid = -1;     // real uid
};

// Reads a process from procDir (normally /proc); nullopt for kernel threads and processes that
// are gone or unreadable.
std::optional<ProcessInfo> readProcess(pid_t pid, const QString &procDir = QStringLiteral("/proc"));
QList<pid_t> listProcesses(const QString &procDir = QStringLiteral("/proc"));

// Decides which app rule, if any, a process belongs to.
//
// A rule's exe matches the process binary (symlinks resolved), argv[0], or argv[1] of a script
// interpreter — that catches wrapper scripts ("/bin/sh /usr/bin/firefox"), whose children then
// inherit the cgroup. cmdline_match is a substring of the whole command line, for Wine games
// ("GenshinImpact.exe") where the binary is wine64-preloader. Processes already inside
// excludedBase are skipped: children inherit their parent's cgroup anyway. A rule with an owner
// only takes that user's processes.
class AppMatcher
{
public:
    void setRules(const QList<AppRule> &rules, const QString &excludedBase);
    bool hasRules() const { return !m_rules.isEmpty(); }

    // The id of the first matching enabled rule, or an empty string.
    QString match(const ProcessInfo &process) const;

private:
    struct Compiled {
        QString id;
        qint64 ownerUid;
        QString exe;
        QString exeResolved;
        QString cmdline;
    };
    QList<Compiled> m_rules;
    QString m_base;
};

} // namespace agv
