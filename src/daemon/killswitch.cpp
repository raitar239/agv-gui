#include "killswitch.h"

namespace agv {

namespace {

QString cgroupMatch(const QString &path)
{
    const int level = int(path.split(QLatin1Char('/'), Qt::SkipEmptyParts).size());
    return QStringLiteral("socket cgroupv2 level %1 \"%2\"").arg(level).arg(path);
}

// Interface names come from the CLI's status output; keep anything odd out of the script.
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

KillSwitch::KillSwitch(SystemCommands *commands)
    : m_commands(commands)
{
}

QByteArray KillSwitch::buildRuleset(const Options &options)
{
    const QString tun = isSafeInterface(options.tunDevice) ? options.tunDevice : QStringLiteral("tun0");
    const QString mark = QStringLiteral("0x%1").arg(RoutingEngine::Mark, 8, 16, QLatin1Char('0'));
    // Link-local, multicast and the DHCP broadcast keep the network itself working.
    const QString link4 = QStringLiteral("169.254.0.0/16, 224.0.0.0/4, 255.255.255.255");
    const QString link6 = QStringLiteral("fe80::/10, ff00::/8");
    const QString lan4 = QStringLiteral("10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 100.64.0.0/10");
    const QString lan6 = QStringLiteral("fc00::/7");

    if (options.mode == RoutingEngine::Mode::VpnOnly) {
        return QStringLiteral(
            "table inet %1\n"
            "delete table inet %1\n"
            "table inet %1 {\n"
            // Marking reroutes these into the tunnel after the output hook has seen the first
            // device, so only postrouting knows where they really go.
            "\tchain postrouting {\n"
            "\t\ttype filter hook postrouting priority filter; policy accept;\n"
            "\t\tmeta mark %2 oifname != { \"lo\", \"%3\" } counter drop\n"
            "\t}\n"
            "}\n").arg(TableName, mark, tun).toUtf8();
    }

    // What may leave while the VPN is down, by the device the packet really leaves through.
    QString allowed = QStringLiteral(
        "\t\toifname { \"lo\", \"%1\" } accept\n"
        "\t\tmeta mark %2 accept\n"
        "\t\tip daddr { %3 } accept\n"
        "\t\tip6 daddr { %4 } accept\n"
        "\t\tudp sport 68 udp dport 67 accept\n"
        "\t\tudp sport 546 udp dport 547 accept\n").arg(tun, mark, link4, link6);
    if (options.allowLan)
        allowed += QStringLiteral("\t\tip daddr { %1 } accept\n\t\tip6 daddr { %2 } accept\n").arg(lan4, lan6);

    QString local;
    if (!options.ownCgroup.isEmpty())
        local += QStringLiteral("\t\t%1 accept\n").arg(cgroupMatch(options.ownCgroup));
    if (!options.resolvedCgroup.isEmpty())
        local += QStringLiteral("\t\t%1 meta l4proto { tcp, udp } th dport 53 accept\n").arg(cgroupMatch(options.resolvedCgroup));

    // In the output hook the device is still the one the first route picked. Packets that
    // NAT sends elsewhere (DnsGuard moves DNS into the tunnel) are judged in postrouting, where
    // the device is final; dropped there, since reject is not available in that hook.
    const QString script = QStringLiteral(
        "table inet %1\n"
        "delete table inet %1\n"
        "table inet %1 {\n"
        "\tchain allowed {\n"
        "%2"
        "\t}\n"
        "\tchain output {\n"
        "\t\ttype filter hook output priority filter; policy accept;\n"
        "\t\tct status dnat accept\n"
        "\t\tjump allowed\n"
        "%3"
        "\t\tcounter reject with icmpx admin-prohibited\n"
        "\t}\n"
        "\tchain forward {\n"
        "\t\ttype filter hook forward priority filter; policy accept;\n"
        "\t\tjump allowed\n"
        "\t\tcounter reject with icmpx admin-prohibited\n"
        "\t}\n"
        "\tchain postrouting {\n"
        "\t\ttype filter hook postrouting priority filter; policy accept;\n"
        "\t\tct status dnat jump allowed\n"
        "\t\tct status dnat counter drop\n"
        "\t}\n"
        "}\n").arg(TableName, allowed, local);
    return script.toUtf8();
}

bool KillSwitch::engage(const Options &options)
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

void KillSwitch::release()
{
    // Also clears a table left behind by a crashed agvd.
    m_commands->run(QStringLiteral("nft"), {QStringLiteral("delete"), QStringLiteral("table"),
                                            QStringLiteral("inet"), TableName});
    m_engaged = false;
}

} // namespace agv
