#include "siteexclusion.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace agv::site {

namespace {

// One DNS label in its ASCII (punycode) form.
bool isLabel(const QString &label)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$"));
    return pattern.match(label).hasMatch();
}

// minLabels: 2 for a site ("domain.net"), 1 after "*." (a whole zone such as "*.ru" is fine).
bool isDomain(const QString &name, int minLabels)
{
    if (name.isEmpty() || name.size() > 253)
        return false;
    const QStringList labels = name.split(QLatin1Char('.'));
    if (labels.size() < minLabels)
        return false;
    for (const QString &label : labels) {
        if (!isLabel(label))
            return false;
    }
    // The top-level label is never all digits; that would be a malformed IPv4 address.
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    return !digits.match(labels.last()).hasMatch();
}

bool isPort(const QString &text)
{
    bool ok = false;
    const int port = text.toInt(&ok);
    return ok && port >= 1 && port <= 65535 && !text.startsWith(QLatin1Char('0'));
}

bool isIPv4(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})$"));
    const auto match = pattern.match(text);
    if (!match.hasMatch())
        return false;
    for (int i = 1; i <= 4; ++i) {
        const QString octet = match.captured(i);
        if (octet.toInt() > 255 || (octet.size() > 1 && octet.startsWith(QLatin1Char('0'))))
            return false;
    }
    return true;
}

bool isIPv6(const QString &text)
{
    if (!text.contains(QLatin1Char(':')))
        return false;
    QHostAddress address;
    return address.setAddress(text) && address.protocol() == QAbstractSocket::IPv6Protocol
        && !text.contains(QLatin1Char('%'));  // no zone ids
}

// Classifies an already normalized entry, or returns false.
bool classify(const QString &entry, Kind *kind)
{
    // CIDR
    if (entry.contains(QLatin1Char('/'))) {
        const QString address = entry.section(QLatin1Char('/'), 0, 0);
        const QString mask = entry.section(QLatin1Char('/'), 1);
        bool ok = false;
        const int bits = mask.toInt(&ok);
        if (!ok || mask.isEmpty() || (mask.size() > 1 && mask.startsWith(QLatin1Char('0'))))
            return false;
        if ((isIPv4(address) && bits >= 0 && bits <= 32) || (isIPv6(address) && bits >= 0 && bits <= 128)) {
            *kind = Kind::Range;
            return true;
        }
        return false;
    }
    // [IPv6] and [IPv6]:port
    if (entry.startsWith(QLatin1Char('['))) {
        const qsizetype close = entry.indexOf(QLatin1Char(']'));
        if (close < 0 || !isIPv6(entry.mid(1, close - 1)))
            return false;
        const QString rest = entry.mid(close + 1);
        if (!rest.isEmpty() && !(rest.startsWith(QLatin1Char(':')) && isPort(rest.mid(1))))
            return false;
        *kind = Kind::Address;
        return true;
    }
    // Bare IPv6 (several colons), IPv4 and IPv4:port
    if (entry.count(QLatin1Char(':')) > 1) {
        if (!isIPv6(entry))
            return false;
        *kind = Kind::Address;
        return true;
    }
    const QString host = entry.section(QLatin1Char(':'), 0, 0);
    if (isIPv4(host)) {
        if (entry.contains(QLatin1Char(':')) && !isPort(entry.section(QLatin1Char(':'), 1)))
            return false;
        *kind = Kind::Address;
        return true;
    }
    // Domains
    if (entry.startsWith(QLatin1String("*."))) {
        if (!isDomain(entry.mid(2), 1))
            return false;
        *kind = Kind::Wildcard;
        return true;
    }
    if (isDomain(entry, 2)) {
        *kind = Kind::Domain;
        return true;
    }
    return false;
}

} // namespace

bool isValid(const QString &entry)
{
    Kind kind;
    return classify(entry, &kind);
}

Parsed parse(const QString &input)
{
    Parsed result;
    result.input = input.trimmed();
    QString text = result.input;

    // A pasted link: keep the host only. Ports stay for IP addresses.
    if (text.contains(QLatin1String("://"))) {
        const QUrl url(text);
        if (url.isValid() && !url.host().isEmpty()) {
            text = url.host();
            if (text.contains(QLatin1Char(':')))
                text = QLatin1Char('[') + text + QLatin1Char(']');
            // A port only means something for an IP address; domains match on any port.
            if (url.port() > 0 && (isIPv4(url.host()) || isIPv6(url.host())))
                text += QLatin1Char(':') + QString::number(url.port());
        }
    }
    text = text.toLower();
    if (text.endsWith(QLatin1Char('.')) && !text.endsWith(QLatin1String("..")))
        text.chop(1);  // fully qualified "example.com."

    // ".ru" means the whole zone.
    if (text.startsWith(QLatin1Char('.')) && !text.startsWith(QLatin1String("..")))
        text.prepend(QLatin1Char('*'));

    // International names to punycode, keeping a leading "*.".
    const bool wildcard = text.startsWith(QLatin1String("*."));
    QString name = wildcard ? text.mid(2) : text;
    bool ascii = true;
    for (const QChar c : name) {
        if (c.unicode() > 127) {
            ascii = false;
            break;
        }
    }
    if (!ascii) {
        const QByteArray ace = QUrl::toAce(name);
        if (!ace.isEmpty())
            name = QString::fromLatin1(ace);
    }
    text = wildcard ? QStringLiteral("*.") + name : name;
    result.value = text;

    if (classify(text, &result.kind))
        return result;

    // Explain the usual mistakes.
    if (text.startsWith(QLatin1Char('*')) && !text.startsWith(QLatin1String("*."))) {
        result.problem = Problem::StarWithoutDot;
        result.suggestion = QStringLiteral("*.") + text.mid(1);
        result.error = QStringLiteral("\"%1\": write %2 for all subdomains").arg(input.trimmed(), result.suggestion);
    } else if (text.contains(QLatin1Char('*'))) {
        result.problem = Problem::StarInside;
        result.error = QStringLiteral("\"%1\": * is only allowed at the start, as *.domain").arg(input.trimmed());
    } else if (!text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char(':')) && isLabel(text)) {
        result.problem = Problem::BareZone;
        result.suggestion = QStringLiteral("*.") + text;
        result.error = QStringLiteral("\"%1\": for a whole zone write %2").arg(input.trimmed(), result.suggestion);
    } else {
        result.problem = Problem::Invalid;
        result.error = QStringLiteral("\"%1\" is not a domain, IP address or range").arg(input.trimmed());
    }
    return result;
}

QList<Parsed> parseList(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[\\s,;]+"));
    QList<Parsed> result;
    for (const QString &token : text.split(separators, Qt::SkipEmptyParts))
        result.append(parse(token));
    return result;
}

QStringList expand(const Parsed &entry, bool includeSubdomains)
{
    if (!entry.ok())
        return {};
    if (entry.kind != Kind::Domain || !includeSubdomains)
        return {entry.value};
    // "www.domain.net" already covers www; its subdomains belong to "*.domain.net".
    const QString base = entry.value.startsWith(QLatin1String("www.")) ? entry.value.mid(4) : entry.value;
    QStringList entries{entry.value};
    if (isDomain(base, 2))
        entries.append(QStringLiteral("*.") + base);
    return entries;
}

QString sortKey(const QString &entry)
{
    // Reverse the labels so related names sit together ("net.domain", "net.domain.*"); the
    // domain itself sorts before its wildcard.
    const bool wildcard = entry.startsWith(QLatin1String("*."));
    QString base = wildcard ? entry.mid(2) : entry;
    if (base.startsWith(QLatin1String("www.")))
        base = base.mid(4);
    if (isIPv4(base.section(QLatin1Char(':'), 0, 0).section(QLatin1Char('/'), 0, 0)) || base.contains(QLatin1Char(':')))
        return QStringLiteral("~") + entry;  // addresses after names
    QStringList labels = base.split(QLatin1Char('.'));
    std::reverse(labels.begin(), labels.end());
    return labels.join(QLatin1Char('.')) + (wildcard ? QStringLiteral(" 1") : QStringLiteral(" 0"));
}

} // namespace agv::site
