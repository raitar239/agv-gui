#include "cliparser.h"

#include <QRegularExpression>

#include <utility>

namespace agv::cli {

namespace {

QStringList cleanLines(const QString &output)
{
    return stripAnsi(output).split(QLatin1Char('\n'));
}

std::optional<ExclusionMode> exclusionModeFromName(const QString &name)
{
    if (name.compare(QLatin1String("GENERAL"), Qt::CaseInsensitive) == 0)
        return ExclusionMode::General;
    if (name.compare(QLatin1String("SELECTIVE"), Qt::CaseInsensitive) == 0)
        return ExclusionMode::Selective;
    return std::nullopt;
}

// Splits the middle part of a location row ("France               Marseille") into
// country and city. Columns are separated by runs of spaces; when a long country name
// fills its column the separator may be a single space, so fall back to header positions.
std::pair<QString, QString> splitCountryCity(const QString &line, qsizetype countryCol,
                                             qsizetype cityCol, qsizetype pingCol)
{
    static const QRegularExpression gap(QStringLiteral("\\s{2,}"));

    const QString middle = line.mid(countryCol, pingCol - countryCol).trimmed();
    const QStringList parts = middle.split(gap, Qt::SkipEmptyParts);
    if (parts.size() == 2)
        return {parts[0], parts[1]};

    return {line.mid(countryCol, cityCol - countryCol).trimmed(),
            line.mid(cityCol, pingCol - cityCol).trimmed()};
}

} // namespace

QString stripAnsi(const QString &text)
{
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;?]*[ -/]*[@-~]"));
    QString result = text;
    result.remove(ansi);
    return result;
}

bool isUsageError(const QString &output)
{
    const QString text = stripAnsi(output);
    return text.contains(QLatin1String("A subcommand is required"))
        || text.contains(QLatin1String("Run with --help for more information"))
        || text.contains(QLatin1String("The following argument was not expected"))
        || text.contains(QLatin1String("The following arguments were not expected"));
}

bool isNotLoggedIn(int exitCode, const QString &output)
{
    if (exitCode == ExitNotLoggedIn)
        return true;
    const QString text = stripAnsi(output);
    return text.contains(QLatin1String("You are not logged in"))
        || text.contains(QLatin1String("Please log in"));
}

QString parseLoginUrl(const QString &output)
{
    static const QRegularExpression url(
        QStringLiteral("authorize in your browser.*?(https://\\S+)"));

    const auto match = url.match(stripAnsi(output));
    return match.hasMatch() ? match.captured(1) : QString();
}

std::optional<Status> parseStatus(const QString &output)
{
    static const QRegularExpression connected(
        QStringLiteral("^Connected to (.+) in (\\w+) mode(?:, running on (\\S+))?"));

    for (const QString &line : cleanLines(output)) {
        const QString trimmed = line.trimmed();
        if (trimmed == QLatin1String("VPN is disconnected"))
            return Status{};
        if (trimmed == QLatin1String("You are not logged in"))
            return Status{Status::State::NotLoggedIn, {}, {}, {}};

        const auto match = connected.match(trimmed);
        if (match.hasMatch()) {
            Status status;
            status.state = Status::State::Connected;
            status.location = match.captured(1);
            status.mode = match.captured(2).toUpper();
            status.tunDevice = match.captured(3);
            return status;
        }
    }
    return std::nullopt;
}

std::optional<QList<Location>> parseLocations(const QString &output)
{
    static const QRegularExpression row(QStringLiteral("^([A-Z]{2})\\s"));

    const QStringList lines = cleanLines(output);

    qsizetype header = -1;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (lines[i].startsWith(QLatin1String("ISO")) && lines[i].contains(QLatin1String("COUNTRY"))
            && lines[i].contains(QLatin1String("CITY"))) {
            header = i;
            break;
        }
    }
    if (header < 0)
        return std::nullopt;

    const QString &head = lines[header];
    const qsizetype countryCol = head.indexOf(QLatin1String("COUNTRY"));
    const qsizetype cityCol = head.indexOf(QLatin1String("CITY"));
    const qsizetype pingCol = head.indexOf(QLatin1String("PING"));
    if (countryCol < 0 || cityCol <= countryCol || pingCol <= cityCol)
        return std::nullopt;

    QList<Location> locations;
    for (qsizetype i = header + 1; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.trimmed().isEmpty())
            break; // the table ends with a blank line followed by a hint

        if (!row.match(line).hasMatch())
            return std::nullopt;

        Location location;
        location.iso = line.left(2);

        // Ping is the last token; a long city name may push it past the header column.
        QString rest = line.trimmed();
        const qsizetype lastSpace = rest.lastIndexOf(QLatin1Char(' '));
        bool ok = false;
        const int ping = rest.mid(lastSpace + 1).toInt(&ok);
        if (ok && lastSpace > 0) {
            location.ping = ping;
            rest.truncate(lastSpace);
        }

        const qsizetype end = qMax(pingCol, rest.size());
        auto [country, city] = splitCountryCity(rest, countryCol, cityCol, end);
        location.country = country;
        location.city = city;
        if (location.country.isEmpty() || location.city.isEmpty())
            return std::nullopt;

        locations.append(location);
    }
    return locations;
}

std::optional<QMap<QString, ConfigValue>> parseConfig(const QString &output)
{
    static const QRegularExpression defaultValue(QStringLiteral("^Default \\((.*)\\)$"));

    const QStringList lines = cleanLines(output);
    const qsizetype header = lines.indexOf(QStringLiteral("Current configuration:"));
    if (header < 0)
        return std::nullopt;

    QMap<QString, ConfigValue> config;
    for (qsizetype i = header + 1; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.trimmed().isEmpty())
            continue;

        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            return std::nullopt;

        const QString key = line.left(colon).trimmed();
        const QString raw = line.mid(colon + 1).trimmed();

        ConfigValue value;
        if (const auto match = defaultValue.match(raw); match.hasMatch()) {
            value.value = match.captured(1);
            value.isDefault = true;
        } else if (raw == QLatin1String("not set")) {
            value.isDefault = true;
        } else {
            value.value = raw;
        }
        config.insert(key, value);
    }

    if (config.isEmpty())
        return std::nullopt;
    return config;
}

std::optional<ExclusionMode> parseExclusionMode(const QString &output)
{
    static const QRegularExpression mode(QStringLiteral("Current exclusion mode is (\\w+)"));

    const auto match = mode.match(stripAnsi(output));
    if (!match.hasMatch())
        return std::nullopt;
    return exclusionModeFromName(match.captured(1));
}

std::optional<SiteExclusions> parseSiteExclusions(const QString &output)
{
    static const QRegularExpression header(QStringLiteral("^Exclusions for (\\w+) mode:$"));

    const QStringList lines = cleanLines(output);
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const auto match = header.match(lines[i].trimmed());
        if (!match.hasMatch())
            continue;

        const auto mode = exclusionModeFromName(match.captured(1));
        if (!mode)
            return std::nullopt;

        SiteExclusions exclusions;
        exclusions.mode = *mode;
        for (qsizetype j = i + 1; j < lines.size(); ++j) {
            const QString domain = lines[j].trimmed();
            if (!domain.isEmpty())
                exclusions.domains.append(domain);
        }
        return exclusions;
    }
    return std::nullopt;
}

std::optional<License> parseLicense(const QString &output)
{
    static const QRegularExpression account(QStringLiteral("^Logged in as (\\S+)"));
    static const QRegularExpression plan(QStringLiteral("^You are using the (\\w+) version"));
    static const QRegularExpression devices(QStringLiteral("^Up to (\\d+) devices"));
    static const QRegularExpression validUntil(
        QStringLiteral("valid until (\\d{4}-\\d{2}-\\d{2})"));

    License license;
    for (const QString &raw : cleanLines(output)) {
        const QString line = raw.trimmed();
        if (const auto m = account.match(line); m.hasMatch())
            license.account = m.captured(1);
        else if (const auto m = plan.match(line); m.hasMatch())
            license.plan = m.captured(1).toUpper();
        else if (const auto m = devices.match(line); m.hasMatch())
            license.maxDevices = m.captured(1).toInt();
        else if (const auto m = validUntil.match(line); m.hasMatch())
            license.validUntil = QDate::fromString(m.captured(1), Qt::ISODate);
    }

    if (license.account.isEmpty() && license.plan.isEmpty())
        return std::nullopt;
    return license;
}

} // namespace agv::cli
