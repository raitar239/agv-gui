#include "appmatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace agv {

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A script started via its shebang runs as "<interpreter> <script> args…".
bool isInterpreter(const QString &exe)
{
    const QString name = exe.mid(exe.lastIndexOf(QLatin1Char('/')) + 1);
    static const QStringList shells{QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("dash"),
                                    QStringLiteral("zsh"), QStringLiteral("ksh"), QStringLiteral("fish"),
                                    QStringLiteral("perl"), QStringLiteral("ruby"), QStringLiteral("node")};
    return shells.contains(name) || name.startsWith(QLatin1String("python"));
}

QString resolve(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? path : canonical;
}

} // namespace

std::optional<ProcessInfo> readProcess(pid_t pid, const QString &procDir)
{
    const QString dir = procDir + QLatin1Char('/') + QString::number(pid);
    const QByteArray cmdline = readFile(dir + QStringLiteral("/cmdline"));
    if (cmdline.isEmpty())
        return std::nullopt; // kernel thread, zombie or already gone

    ProcessInfo info;
    info.pid = pid;
    for (const QByteArray &arg : cmdline.split('\0')) {
        if (!arg.isEmpty())
            info.argv.append(QString::fromLocal8Bit(arg));
    }

    QString exe = QFileInfo(dir + QStringLiteral("/exe")).symLinkTarget();
    // After an upgrade the running binary shows as "/usr/bin/foo (deleted)".
    if (exe.endsWith(QLatin1String(" (deleted)")))
        exe.chop(10);
    info.exe = exe;

    for (const QByteArray &line : readFile(dir + QStringLiteral("/cgroup")).split('\n')) {
        if (line.startsWith("0::/"))
            info.cgroup = QString::fromUtf8(line.mid(4));
    }
    for (const QByteArray &line : readFile(dir + QStringLiteral("/status")).split('\n')) {
        if (line.startsWith("Uid:"))
            info.uid = line.mid(4).simplified().split(' ').value(0).toLongLong();
        else if (line.startsWith("PPid:"))
            info.ppid = pid_t(line.mid(5).trimmed().toInt());
    }
    return info;
}

std::optional<Lineage> readLineage(pid_t pid, const QString &procDir)
{
    const QString dir = procDir + QLatin1Char('/') + QString::number(pid);
    const QByteArray status = readFile(dir + QStringLiteral("/status"));
    if (status.isEmpty())
        return std::nullopt;

    Lineage lineage;
    for (const QByteArray &line : status.split('\n')) {
        if (line.startsWith("PPid:")) {
            lineage.ppid = pid_t(line.mid(5).trimmed().toInt());
            break;
        }
    }
    for (const QByteArray &line : readFile(dir + QStringLiteral("/cgroup")).split('\n')) {
        if (line.startsWith("0::/"))
            lineage.cgroup = QString::fromUtf8(line.mid(4));
    }
    return lineage;
}

QList<pid_t> listProcesses(const QString &procDir)
{
    QList<pid_t> pids;
    for (const QString &entry : QDir(procDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const pid_t pid = entry.toInt(&ok);
        if (ok && pid > 0)
            pids.append(pid);
    }
    return pids;
}

void AppMatcher::setRules(const QList<AppRule> &rules, const QString &excludedBase)
{
    m_rules.clear();
    m_base = excludedBase;
    for (const AppRule &rule : rules) {
        if (!rule.enabled || (rule.exe.isEmpty() && rule.cmdlineMatch.isEmpty()))
            continue;
        m_rules.append({rule.id, rule.ownerUid, rule.exe, rule.exe.isEmpty() ? QString() : resolve(rule.exe),
                        rule.cmdlineMatch});
    }
}

QString AppMatcher::groupOf(const QString &cgroup) const
{
    if (m_base.isEmpty() || !cgroup.startsWith(m_base + QLatin1Char('/')))
        return {};
    return cgroup.mid(m_base.size() + 1).section(QLatin1Char('/'), 0, 0);
}

QString AppMatcher::match(const ProcessInfo &process) const
{
    if (m_rules.isEmpty() || process.argv.isEmpty())
        return {};
    if (!m_base.isEmpty()
        && (process.cgroup == m_base || process.cgroup.startsWith(m_base + QLatin1Char('/')))) {
        return {};
    }

    const QString joined = process.argv.join(QLatin1Char(' '));
    for (const Compiled &rule : m_rules) {
        // A rule only takes its owner's processes: one user's exclusion never affects another.
        if (rule.ownerUid >= 0 && process.uid != rule.ownerUid)
            continue;
        if (!rule.exe.isEmpty()) {
            if (process.exe == rule.exe || process.exe == rule.exeResolved)
                return rule.id;
            if (process.argv.value(0) == rule.exe || process.argv.value(0) == rule.exeResolved)
                return rule.id;
            if (process.argv.size() > 1 && isInterpreter(process.exe)
                && (process.argv.at(1) == rule.exe || process.argv.at(1) == rule.exeResolved)) {
                return rule.id;
            }
        }
        if (!rule.cmdline.isEmpty() && joined.contains(rule.cmdline))
            return rule.id;
    }
    return {};
}

} // namespace agv
