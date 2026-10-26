#include "routingengine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace agv {

namespace {

constexpr int CommandTimeoutMs = 10'000;
constexpr int MaxRuleDeletes = 16;

QString markHex()
{
    return QStringLiteral("0x%1").arg(RoutingEngine::Mark, 8, 16, QLatin1Char('0'));
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &data, QIODevice::OpenMode extra = {})
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | extra))
        return false;
    return file.write(data) == data.size();
}

} // namespace

SystemCommands::Result ProcessCommands::run(const QString &program, const QStringList &args,
                                            const QByteArray &input)
{
    // Absolute paths only: the daemon runs as root and must not depend on PATH.
    QString path = program;
    if (!path.startsWith(QLatin1Char('/'))) {
        path = QStandardPaths::findExecutable(program, {QStringLiteral("/usr/sbin"), QStringLiteral("/usr/bin"),
                                                        QStringLiteral("/sbin"), QStringLiteral("/bin")});
    }
    if (path.isEmpty())
        return {-1, program.toUtf8() + " not found"};

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(path, args);
    if (!process.waitForStarted(CommandTimeoutMs))
        return {-1, process.errorString().toUtf8()};
    if (!input.isEmpty())
        process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(CommandTimeoutMs)) {
        process.kill();
        process.waitForFinished();
        return {-1, program.toUtf8() + " timed out"};
    }
    return {process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1, process.readAll()};
}

RoutingEngine::RoutingEngine(SystemCommands *commands, RoutingPaths paths)
    : m_commands(commands)
    , m_paths(std::move(paths))
{
}

bool RoutingEngine::init()
{
    m_supported = false;

    if (!QFileInfo::exists(m_paths.cgroupRoot + QStringLiteral("/cgroup.controllers")))
        return fail(QStringLiteral("cgroup v2 is not mounted at %1").arg(m_paths.cgroupRoot));

    // Unified hierarchy entry: "0::/system.slice/agvd.service"
    m_own.clear();
    bool found = false;
    for (const QByteArray &line : readFile(m_paths.selfCgroup).split('\n')) {
        if (line.startsWith("0::")) {
            m_own = QString::fromUtf8(line.mid(3)).trimmed();
            found = true;
        }
    }
    if (!found)
        return fail(QStringLiteral("Cannot find the daemon's cgroup in %1").arg(m_paths.selfCgroup));
    while (m_own.startsWith(QLatin1Char('/')))
        m_own.remove(0, 1);
    m_base = m_own.isEmpty() ? QStringLiteral("agvd/novpn") : m_own + QStringLiteral("/novpn");

    if (m_commands->run(QStringLiteral("nft"), {QStringLiteral("--version")}).exitCode != 0)
        return fail(QStringLiteral("nft (nftables) is not installed"));
    if (m_commands->run(QStringLiteral("ip"), {QStringLiteral("-V")}).exitCode != 0)
        return fail(QStringLiteral("ip (iproute2) is not installed"));

    m_supported = true;
    m_lastError.clear();
    return true;
}

QByteArray RoutingEngine::buildRuleset(const QString &base, const QStringList &appIds)
{
    const int level = int(base.split(QLatin1Char('/'), Qt::SkipEmptyParts).size()) + 1;
    const QString mark = markHex();

    QString out;
    QString in;
    for (const QString &id : appIds) {
        const QString match = QStringLiteral("socket cgroupv2 level %1 \"%2/%3\"").arg(level).arg(base, id);
        out += QStringLiteral("\t\t%1 counter meta mark set %2 ct mark set meta mark comment \"out:%3\"\n")
                   .arg(match, mark, id);
        in += QStringLiteral("\t\t%1 counter comment \"in:%2\"\n").arg(match, id);
    }
    // The daemon's own cgroup (the one above "novpn"): adguardvpn-cli talking to the VPN server.
    if (base.endsWith(QLatin1String("/novpn"))) {
        const QString own = base.chopped(6);
        const QString match = QStringLiteral("socket cgroupv2 level %1 \"%2\" meta mark != %3")
                                  .arg(level - 2).arg(own, mark);
        out += QStringLiteral("\t\t%1 counter comment \"out:%2\"\n").arg(match, DaemonTrafficId);
        in += QStringLiteral("\t\t%1 counter comment \"in:%2\"\n").arg(match, DaemonTrafficId);
    }

    // "table; delete table; table {…}" replaces the whole table in one transaction, whether or
    // not it existed before.
    const QString script = QStringLiteral(
        "table inet %1\n"
        "delete table inet %1\n"
        "table inet %1 {\n"
        "\tchain output {\n"
        "\t\ttype route hook output priority mangle; policy accept;\n"
        "\t\toifname \"lo\" return\n"
        "%2"
        "\t}\n"
        "\tchain input {\n"
        "\t\ttype filter hook input priority mangle; policy accept;\n"
        "%3"
        "\t}\n"
        "\tchain prerouting {\n"
        "\t\ttype filter hook prerouting priority mangle; policy accept;\n"
        "\t\tct mark %4 meta mark set ct mark\n"
        "\t}\n"
        "\tchain postrouting {\n"
        "\t\ttype nat hook postrouting priority srcnat; policy accept;\n"
        "\t\tmeta mark %4 oifname != \"lo\" masquerade\n"
        "\t}\n"
        "}\n").arg(TableName, out, in, mark);
    return script.toUtf8();
}

bool RoutingEngine::apply(const QStringList &appIds, Mode mode)
{
    if (!m_supported)
        return fail(QStringLiteral("App exclusions are not supported: %1").arg(m_lastError));

    for (const QString &id : appIds) {
        if (!ensureCgroup(id))
            return false;
    }

    // nftables resolves cgroup paths when the rules load, so the directories must exist first.
    const auto nft = m_commands->run(QStringLiteral("nft"), {QStringLiteral("-f"), QStringLiteral("-")},
                                     buildRuleset(m_base, appIds));
    if (nft.exitCode != 0)
        return fail(QStringLiteral("nft failed: %1").arg(QString::fromUtf8(nft.output).trimmed()));

    if (!applyIpRules(mode))
        return false;
    setSrcValidMark();

    // Drop cgroups of apps that are gone, including ones removed while the daemon was down.
    const QStringList present = QDir(cgroupDir(m_base)).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &id : present) {
        if (!appIds.contains(id))
            releaseCgroup(id);
    }

    m_applied = appIds;
    m_lastError.clear();
    return true;
}

void RoutingEngine::teardown()
{
    m_commands->run(QStringLiteral("nft"), {QStringLiteral("delete"), QStringLiteral("table"),
                                            QStringLiteral("inet"), TableName});
    removeIpRules();

    // The state file knows where the cgroups were even when this process is not agvd itself.
    const QMap<QString, QString> state = readState();
    if (state.contains(QStringLiteral("own")))
        m_own = state.value(QStringLiteral("own"));
    if (state.contains(QStringLiteral("base")))
        m_base = state.value(QStringLiteral("base"));

    const QByteArray original = state.value(QStringLiteral("src_valid_mark")).toUtf8();
    if (original == "0" || original == "1")
        writeFile(m_paths.procSys + QStringLiteral("/net/ipv4/conf/all/src_valid_mark"), original + '\n');

    if (!m_base.isEmpty()) {
        const QStringList present = QDir(cgroupDir(m_base)).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &id : present)
            releaseCgroup(id);
        QDir().rmdir(cgroupDir(m_base));
    }
    if (!m_paths.stateFile.isEmpty())
        QFile::remove(m_paths.stateFile);
    m_applied.clear();
}

bool RoutingEngine::moveProcess(pid_t pid, const QString &appId)
{
    if (!m_applied.contains(appId))
        return fail(QStringLiteral("Unknown app exclusion %1").arg(appId));
    if (pid <= 0)
        return fail(QStringLiteral("Invalid process id"));

    const QString procs = cgroupDir(m_base + QLatin1Char('/') + appId) + QStringLiteral("/cgroup.procs");
    if (!writeFile(procs, QByteArray::number(pid) + '\n', QIODevice::Append))
        return fail(QStringLiteral("Cannot move process %1 into %2").arg(pid).arg(appId));
    return true;
}

QMap<QString, RoutingEngine::Traffic> RoutingEngine::traffic()
{
    QMap<QString, Traffic> result;
    const auto nft = m_commands->run(QStringLiteral("nft"), {QStringLiteral("-j"), QStringLiteral("list"),
                                                             QStringLiteral("table"), QStringLiteral("inet"),
                                                             TableName});
    if (nft.exitCode != 0)
        return result;

    const QJsonArray items = QJsonDocument::fromJson(nft.output).object().value(QStringLiteral("nftables")).toArray();
    for (const QJsonValue &item : items) {
        const QJsonObject rule = item.toObject().value(QStringLiteral("rule")).toObject();
        const QString comment = rule.value(QStringLiteral("comment")).toString();
        const bool out = comment.startsWith(QLatin1String("out:"));
        if (!out && !comment.startsWith(QLatin1String("in:")))
            continue;

        for (const QJsonValue &expr : rule.value(QStringLiteral("expr")).toArray()) {
            const QJsonObject counter = expr.toObject().value(QStringLiteral("counter")).toObject();
            if (counter.isEmpty())
                continue;
            const auto bytes = quint64(counter.value(QStringLiteral("bytes")).toDouble());
            Traffic &traffic = result[comment.mid(comment.indexOf(QLatin1Char(':')) + 1)];
            (out ? traffic.bytesOut : traffic.bytesIn) += bytes;
        }
    }
    return result;
}

QString RoutingEngine::cgroupDir(const QString &relative) const
{
    return m_paths.cgroupRoot + QLatin1Char('/') + relative;
}

bool RoutingEngine::ensureCgroup(const QString &appId)
{
    const QString dir = cgroupDir(m_base + QLatin1Char('/') + appId);
    if (!QDir().mkpath(dir))
        return fail(QStringLiteral("Cannot create cgroup %1").arg(dir));
    return true;
}

void RoutingEngine::releaseCgroup(const QString &appId)
{
    // Processes go back to the daemon's own cgroup, i.e. back through the VPN for new sockets.
    const QString dir = cgroupDir(m_base + QLatin1Char('/') + appId);
    const QString target = cgroupDir(m_own) + QStringLiteral("/cgroup.procs");
    for (const QByteArray &pid : readFile(dir + QStringLiteral("/cgroup.procs")).split('\n')) {
        if (!pid.trimmed().isEmpty())
            writeFile(target, pid.trimmed() + '\n', QIODevice::Append);
    }
    QDir().rmdir(dir);
}

bool RoutingEngine::applyIpRules(Mode mode)
{
    removeIpRules();
    // Marked traffic, or in VPN-only mode everything unmarked, skips AdGuard's table 880.
    QStringList args{QStringLiteral("rule"), QStringLiteral("add")};
    if (mode == Mode::VpnOnly)
        args << QStringLiteral("not");
    args << QStringLiteral("fwmark") << markHex() << QStringLiteral("lookup") << QStringLiteral("main")
         << QStringLiteral("priority") << QString::number(RulePriority);
    // Where main has no route (typically IPv6 on a network without it) the lookup would fall
    // through to AdGuard's table and into the tunnel; "unreachable" makes the app fall back to
    // the other address family instead.
    QStringList fence = args.mid(0, args.indexOf(QStringLiteral("lookup")));
    fence << QStringLiteral("unreachable") << QStringLiteral("priority") << QString::number(RulePriority + 1);

    const auto v4 = m_commands->run(QStringLiteral("ip"), QStringList{QStringLiteral("-4")} + args);
    if (v4.exitCode != 0)
        return fail(QStringLiteral("ip rule failed: %1").arg(QString::fromUtf8(v4.output).trimmed()));
    m_commands->run(QStringLiteral("ip"), QStringList{QStringLiteral("-4")} + fence);
    // IPv6 may be disabled; then there is nothing to route anyway.
    m_commands->run(QStringLiteral("ip"), QStringList{QStringLiteral("-6")} + args);
    m_commands->run(QStringLiteral("ip"), QStringList{QStringLiteral("-6")} + fence);
    return true;
}

void RoutingEngine::removeIpRules()
{
    for (const QString &family : {QStringLiteral("-4"), QStringLiteral("-6")}) {
        for (const int priority : {RulePriority, RulePriority + 1}) {
            for (int i = 0; i < MaxRuleDeletes; ++i) {
                const auto result = m_commands->run(QStringLiteral("ip"),
                                                    {family, QStringLiteral("rule"), QStringLiteral("del"),
                                                     QStringLiteral("priority"), QString::number(priority)});
                if (result.exitCode != 0)
                    break;
            }
        }
    }
}

void RoutingEngine::setSrcValidMark()
{
    const QString path = m_paths.procSys + QStringLiteral("/net/ipv4/conf/all/src_valid_mark");
    QMap<QString, QString> state = readState();
    // Save the original once; later applies must not overwrite it with our own "1".
    if (!state.contains(QStringLiteral("src_valid_mark")))
        state.insert(QStringLiteral("src_valid_mark"), QString::fromUtf8(readFile(path)).trimmed());
    state.insert(QStringLiteral("own"), m_own);
    state.insert(QStringLiteral("base"), m_base);
    writeState(state);
    writeFile(path, "1\n");
}

QMap<QString, QString> RoutingEngine::readState() const
{
    QMap<QString, QString> state;
    if (m_paths.stateFile.isEmpty())
        return state;
    for (const QByteArray &line : readFile(m_paths.stateFile).split('\n')) {
        const qsizetype eq = line.indexOf('=');
        if (eq > 0)
            state.insert(QString::fromUtf8(line.left(eq)), QString::fromUtf8(line.mid(eq + 1)));
    }
    return state;
}

void RoutingEngine::writeState(const QMap<QString, QString> &state) const
{
    if (m_paths.stateFile.isEmpty())
        return;
    QByteArray data;
    for (auto it = state.cbegin(); it != state.cend(); ++it)
        data += it.key().toUtf8() + '=' + it.value().toUtf8() + '\n';
    writeFile(m_paths.stateFile, data);
}

bool RoutingEngine::fail(const QString &message)
{
    m_lastError = message;
    return false;
}

} // namespace agv
