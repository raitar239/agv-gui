#include "dnsguard.h"

namespace agv {

namespace {

bool isSafeInterface(const QString &name)
{
    if (name.isEmpty() || name.size() > 15)
        return false;
    for (const QChar c : name) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_') && c != QLatin1Char('.'))
            return false;
    }
    return true;
}

} // namespace

DnsGuard::DnsGuard(SystemCommands *commands)
    : m_commands(commands)
{
}

QByteArray DnsGuard::buildRuleset(const Options &options)
{
    const QString tun = isSafeInterface(options.tunDevice) ? options.tunDevice : QStringLiteral("tun0");
    const QString mark = QStringLiteral("0x%1").arg(RoutingEngine::Mark, 8, 16, QLatin1Char('0'));
    QString own;
    if (!options.ownCgroup.isEmpty()) {
        const int level = int(options.ownCgroup.split(QLatin1Char('/'), Qt::SkipEmptyParts).size());
        own = QStringLiteral("\t\tsocket cgroupv2 level %1 \"%2\" return\n").arg(level).arg(options.ownCgroup);
    }

    // The new destination is routed through the tunnel, but the source address was picked for
    // the original one (the router), so it is masqueraded to the tunnel's.
    return QStringLiteral(
        "table inet %1\n"
        "delete table inet %1\n"
        "table inet %1 {\n"
        "\tchain output {\n"
        "\t\ttype nat hook output priority dstnat; policy accept;\n"
        "\t\tmeta mark %2 return\n"
        "%3"
        "\t\tip daddr { 127.0.0.0/8, %4 } return\n"
        "\t\tip6 daddr { ::1, %5 } return\n"
        "\t\tmeta nfproto ipv4 meta l4proto { tcp, udp } th dport 53 counter dnat ip to %4\n"
        "\t\tmeta nfproto ipv6 meta l4proto { tcp, udp } th dport 53 counter dnat ip6 to %5\n"
        "\t}\n"
        "\tchain postrouting {\n"
        "\t\ttype nat hook postrouting priority srcnat; policy accept;\n"
        "\t\toifname \"%6\" meta l4proto { tcp, udp } th dport 53 masquerade\n"
        "\t}\n"
        "}\n").arg(TableName, mark, own, Target4, Target6, tun).toUtf8();
}

bool DnsGuard::engage(const Options &options)
{
    if (m_engaged && options == m_options)
        return true;
    const auto nft = m_commands->run(QStringLiteral("nft"), {QStringLiteral("-f"), QStringLiteral("-")},
                                     buildRuleset(options));
    if (nft.exitCode != 0) {
        m_lastError = QStringLiteral("nft failed: %1").arg(QString::fromUtf8(nft.output).trimmed());
        return false;
    }
    m_engaged = true;
    m_options = options;
    m_lastError.clear();
    return true;
}

void DnsGuard::release()
{
    m_commands->run(QStringLiteral("nft"), {QStringLiteral("delete"), QStringLiteral("table"),
                                            QStringLiteral("inet"), TableName});
    m_engaged = false;
}

} // namespace agv
