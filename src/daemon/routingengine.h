#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <sys/types.h>

namespace agv {

// Runs system tools; replaced by a fake in tests.
class SystemCommands
{
public:
    struct Result {
        int exitCode = -1;
        QByteArray output;  // stdout and stderr
    };

    virtual ~SystemCommands() = default;
    virtual Result run(const QString &program, const QStringList &args, const QByteArray &input = {}) = 0;
};

class ProcessCommands : public SystemCommands
{
public:
    Result run(const QString &program, const QStringList &args, const QByteArray &input = {}) override;
};

// Where RoutingEngine touches the system; tests point these at temporary directories.
struct RoutingPaths {
    QString cgroupRoot = QStringLiteral("/sys/fs/cgroup");
    QString procSys = QStringLiteral("/proc/sys");
    QString selfCgroup = QStringLiteral("/proc/self/cgroup");
    // Remembers what apply() changed (cgroup paths, the original src_valid_mark) so that
    // "agvd --cleanup", run from another process after the daemon stopped, can undo it.
    QString stateFile;
};

// Excludes processes from the VPN at the kernel level (or, in VPN-only mode, keeps only them in it):
//
//   cgroup <base>/<app id>  →  nftables "socket cgroupv2" marks the packets  →
//   "ip rule fwmark … lookup main" sends them past AdGuard's table 880 to the physical route  →
//   masquerade fixes a source address that was already picked for the tunnel.
//
// Replies are matched to the connection's ct mark, and src_valid_mark=1 lets reverse-path
// filtering use it, so rp_filter does not have to be loosened system-wide (same as wg-quick).
// AdGuard's own routing (AUTO mode, table 880) is left untouched.
class RoutingEngine
{
public:
    static constexpr quint32 Mark = 0x61677664;  // "agvd"
    static constexpr int RulePriority = 30700;   // before AdGuard's 30800/30801
    static inline const QString TableName = QStringLiteral("agvd");
    static inline const QString BuiltinRunId = QStringLiteral("run"); // plain "agv-run cmd"

    // Bypass: the listed apps skip the VPN. VpnOnly: only the listed apps use it, everything else
    // goes straight out. Same marks and cgroups; only the ip rule's selector flips.
    enum class Mode { Bypass, VpnOnly };

    explicit RoutingEngine(SystemCommands *commands, RoutingPaths paths = RoutingPaths());

    // The daemon's own cgroup ("system.slice/agvd.service") from /proc/self/cgroup; app cgroups
    // live in "<own>/novpn/<id>". Returns false (and isSupported() stays false) without cgroup v2.
    bool init();
    bool isSupported() const { return m_supported; }
    QString lastError() const { return m_lastError; }
    QString baseCgroup() const { return m_base; }
    // Absolute path of an exclusion group's cgroup directory.
    QString cgroupPath(const QString &appId) const { return cgroupDir(m_base + QLatin1Char('/') + appId); }

    // Makes the system match the given app ids (enabled exclusions): creates their cgroups,
    // loads the nftables table atomically, adds the ip rules and sysctls. Idempotent.
    bool apply(const QStringList &appIds, Mode mode = Mode::Bypass);

    // Removes everything apply() set up and moves excluded processes back. Works without init()
    // as long as the state file is there.
    void teardown();

    // Puts a process into an app's cgroup (the app must be part of the last apply()).
    bool moveProcess(pid_t pid, const QString &appId);

    struct Traffic {
        quint64 bytesOut = 0;
        quint64 bytesIn = 0;
    };
    // Per-app byte counters from the nftables table.
    QMap<QString, Traffic> traffic();

    // The nftables script apply() loads; public for tests.
    static QByteArray buildRuleset(const QString &base, const QStringList &appIds);

private:
    QString cgroupDir(const QString &relative) const;
    bool ensureCgroup(const QString &appId);
    void releaseCgroup(const QString &appId);
    bool applyIpRules(Mode mode);
    void removeIpRules();
    void setSrcValidMark();
    QMap<QString, QString> readState() const;
    void writeState(const QMap<QString, QString> &state) const;
    bool fail(const QString &message);

    SystemCommands *m_commands;
    RoutingPaths m_paths;
    bool m_supported = false;
    QString m_own;      // daemon's cgroup, relative to cgroupRoot
    QString m_base;     // m_own + "/novpn"
    QStringList m_applied;
    QString m_lastError;
};

} // namespace agv
