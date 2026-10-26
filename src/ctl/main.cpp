// agvctl: AGV from a terminal. Talks to agvd over D-Bus like the window does, so the same
// polkit rules apply (connecting needs no password for the active user; settings ask for one).

#include "dbustypes.h"
#include "statistics.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace {

const QString Service = QStringLiteral("org.agv.Daemon1");
const QString Path = QStringLiteral("/org/agv/Daemon");
const QString Interface = QStringLiteral("org.agv.Daemon1");

QTextStream out(stdout);
QTextStream err(stderr);

QDBusConnection bus()
{
    return qEnvironmentVariableIntValue("AGV_SESSION_BUS") == 1 ? QDBusConnection::sessionBus()
                                                                : QDBusConnection::systemBus();
}

[[noreturn]] void fail(const QString &message)
{
    err << "agvctl: " << message << Qt::endl;
    std::exit(1);
}

// Calls agvd and returns the reply, or exits with its error.
QDBusMessage call(const QString &method, const QVariantList &args = {}, int timeoutMs = 120'000)
{
    QDBusMessage message = QDBusMessage::createMethodCall(Service, Path, Interface, method);
    message.setArguments(args);
    message.setInteractiveAuthorizationAllowed(true);
    const QDBusMessage reply = bus().call(message, QDBus::Block, timeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown"))
            fail(QStringLiteral("agvd is not running. Start it with: sudo systemctl start agvd"));
        fail(reply.errorMessage());
    }
    return reply;
}

// org.freedesktop.DBus.Properties lives on the same object.
QDBusMessage propertiesCall(const QString &method, const QVariantList &args)
{
    QDBusMessage message = QDBusMessage::createMethodCall(Service, Path, QStringLiteral("org.freedesktop.DBus.Properties"), method);
    message.setArguments(args);
    const QDBusMessage reply = bus().call(message, QDBus::Block, 10'000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown"))
            fail(QStringLiteral("agvd is not running. Start it with: sudo systemctl start agvd"));
        fail(reply.errorMessage());
    }
    return reply;
}

QVariantMap allProperties()
{
    return qdbus_cast<QVariantMap>(propertiesCall(QStringLiteral("GetAll"), {Interface}).arguments().value(0));
}

QString bytes(double value)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    while (value >= 1024 && unit < 4) {
        value /= 1024;
        ++unit;
    }
    return (unit == 0 || value >= 100 ? QString::number(qRound(value)) : QString::number(value, 'f', 1))
           + QLatin1Char(' ') + QLatin1String(units[unit]);
}

QString duration(double seconds)
{
    const qint64 s = qint64(seconds);
    if (s < 60)
        return QStringLiteral("%1 s").arg(s);
    if (s < 3600)
        return QStringLiteral("%1 min").arg(s / 60);
    if (s < 86400)
        return QStringLiteral("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1 d %2 h").arg(s / 86400).arg((s % 86400) / 3600);
}

QString pretty(const QString &city)
{
    QString result = city.toLower();
    bool start = true;
    for (QChar &c : result) {
        if (start)
            c = c.toUpper();
        start = c == QLatin1Char(' ') || c == QLatin1Char('-');
    }
    return result;
}

QString onOff(bool on)
{
    return on ? QStringLiteral("on") : QStringLiteral("off");
}

bool parseOnOff(const QString &word)
{
    if (word == QLatin1String("on") || word == QLatin1String("true") || word == QLatin1String("1"))
        return true;
    if (word == QLatin1String("off") || word == QLatin1String("false") || word == QLatin1String("0"))
        return false;
    fail(QStringLiteral("expected on or off, not \"%1\"").arg(word));
}

int status(const QStringList &args)
{
    const QVariantMap p = allProperties();
    if (args.contains(QLatin1String("--json"))) {
        QJsonObject json = QJsonObject::fromVariantMap(p);
        json.remove(QStringLiteral("ExternalVpns"));
        json.remove(QStringLiteral("Network"));
        json.remove(QStringLiteral("TrustedNetworks"));
        out << QJsonDocument(json).toJson();
        return 0;
    }
    const QString state = p.value(QStringLiteral("State")).toString();
    if (state == QLatin1String("connected")) {
        out << "Connected to " << pretty(p.value(QStringLiteral("Location")).toString()) << " ("
            << p.value(QStringLiteral("Mode")).toString();
        if (!p.value(QStringLiteral("TunDevice")).toString().isEmpty())
            out << ", " << p.value(QStringLiteral("TunDevice")).toString();
        out << ")\n";
    } else if (p.value(QStringLiteral("Reconnecting")).toBool()) {
        out << "Reconnecting after a drop" << (p.value(QStringLiteral("KillSwitchActive")).toBool()
                                                   ? " (the kill switch blocks the internet until then)" : "")
            << '\n';
    } else if (state == QLatin1String("not_logged_in")) {
        out << "Not signed in. Sign in from the AGV window.\n";
    } else {
        out << "Not connected (" << state << ")\n";
    }
    if (!p.value(QStringLiteral("LastError")).toString().isEmpty() && state != QLatin1String("connected"))
        out << "Last error: " << p.value(QStringLiteral("LastError")).toString() << '\n';
    out << "Kill switch: " << onOff(p.value(QStringLiteral("KillSwitch")).toBool())
        << "   DNS inside the VPN: " << onOff(p.value(QStringLiteral("KeepDnsInVpn")).toBool())
        << "   Apps: " << p.value(QStringLiteral("AppExclusionMode")).toString() << '\n';
    if (!p.value(QStringLiteral("CliVersion")).toString().isEmpty())
        out << "adguardvpn-cli " << p.value(QStringLiteral("CliVersion")).toString() << '\n';
    else if (p.value(QStringLiteral("CliInstall")).toString() == QLatin1String("missing"))
        out << "adguardvpn-cli is not installed\n";
    return 0;
}

int locations(const QStringList &args)
{
    auto list = qdbus_cast<AgvLocationList>(call(QStringLiteral("ListLocations")).arguments().value(0));
    const QString filter = args.join(QLatin1Char(' '));
    std::sort(list.begin(), list.end(), [](const AgvLocation &a, const AgvLocation &b) {
        const int pa = a.ping < 0 ? INT_MAX : a.ping;
        const int pb = b.ping < 0 ? INT_MAX : b.ping;
        return pa != pb ? pa < pb : a.country < b.country;
    });
    for (const AgvLocation &location : std::as_const(list)) {
        const QString line = QStringLiteral("%1  %2, %3").arg(location.iso, location.country, location.city);
        if (!filter.isEmpty() && !line.contains(filter, Qt::CaseInsensitive))
            continue;
        out << line.leftJustified(42) << (location.ping < 0 ? QStringLiteral("—") : QStringLiteral("%1 ms").arg(location.ping)) << '\n';
    }
    return 0;
}

int sites(const QStringList &args)
{
    const QString action = args.value(0, QStringLiteral("list"));
    const QStringList items = args.mid(1);
    if (action == QLatin1String("list")) {
        const QVariantMap p = allProperties();
        out << "Mode: " << p.value(QStringLiteral("SiteExclusionMode")).toString() << '\n';
        const auto list = qdbus_cast<AgvSiteExclusionList>(call(QStringLiteral("ListSiteExclusions")).arguments().value(0));
        for (const AgvSiteExclusion &entry : list)
            out << (entry.enabled ? "  " : "- ") << entry.item << (entry.enabled ? "" : "  (off)") << '\n';
        if (list.isEmpty())
            out << "No websites\n";
        return 0;
    }
    if (items.isEmpty())
        fail(QStringLiteral("name at least one website"));
    if (action == QLatin1String("add")) {
        call(QStringLiteral("AddSiteExclusions"), {items});
    } else if (action == QLatin1String("remove")) {
        call(QStringLiteral("RemoveSiteExclusions"), {items});
    } else if (action == QLatin1String("on") || action == QLatin1String("off")) {
        for (const QString &item : items)
            call(QStringLiteral("SetSiteExclusionEnabled"), {item, action == QLatin1String("on"), QStringList()});
    } else if (action == QLatin1String("mode")) {
        call(QStringLiteral("SetSiteExclusionMode"), {items.first()});
    } else {
        fail(QStringLiteral("unknown sites command \"%1\"").arg(action));
    }
    return 0;
}

int apps(const QStringList &args)
{
    const QString action = args.value(0, QStringLiteral("list"));
    const auto list = qdbus_cast<AgvAppExclusionList>(call(QStringLiteral("ListAppExclusions")).arguments().value(0));
    if (action == QLatin1String("list")) {
        out << "Mode: " << allProperties().value(QStringLiteral("AppExclusionMode")).toString() << '\n';
        for (const AgvAppExclusion &app : list) {
            const bool enabled = app.rule.value(QStringLiteral("enabled")).toBool();
            out << app.id << "  " << (enabled ? "on " : "off") << "  " << app.rule.value(QStringLiteral("name")).toString() << '\n';
        }
        if (list.isEmpty())
            out << "No apps\n";
        return 0;
    }
    if (action == QLatin1String("mode")) {
        call(QStringLiteral("SetAppExclusionMode"), {args.value(1)});
        return 0;
    }
    if (action != QLatin1String("on") && action != QLatin1String("off"))
        fail(QStringLiteral("unknown apps command \"%1\"").arg(action));
    const QString wanted = args.mid(1).join(QLatin1Char(' '));
    for (const AgvAppExclusion &app : list) {
        if (app.id == wanted || app.rule.value(QStringLiteral("name")).toString().compare(wanted, Qt::CaseInsensitive) == 0) {
            call(QStringLiteral("UpdateAppExclusion"),
                 {app.id, QVariantMap{{QStringLiteral("enabled"), action == QLatin1String("on")}}});
            return 0;
        }
    }
    fail(QStringLiteral("no app \"%1\"; see agvctl apps").arg(wanted));
}

int stats(const QStringList &args)
{
    QString period = args.value(0, QStringLiteral("today"));
    if (period == QLatin1String("7d"))
        period = QStringLiteral("week");
    else if (period == QLatin1String("30d"))
        period = QStringLiteral("month");
    if (!QStringList{QStringLiteral("today"), QStringLiteral("week"), QStringLiteral("month"), QStringLiteral("all")}.contains(period))
        fail(QStringLiteral("period is today, week, month or all"));
    const QJsonObject report = QJsonDocument::fromJson(call(QStringLiteral("GetStatistics")).arguments().value(0).toString().toUtf8()).object();
    if (args.contains(QLatin1String("--csv"))) {
        out << QString::fromUtf8(agv::stats::toCsv(report));
        return 0;
    }
    const QVariantMap s = agv::stats::summarize(report, period, QDateTime::currentDateTime());
    out << "Through the VPN: " << bytes(s.value(QStringLiteral("in")).toDouble() + s.value(QStringLiteral("out")).toDouble())
        << " (down " << bytes(s.value(QStringLiteral("in")).toDouble()) << ", up " << bytes(s.value(QStringLiteral("out")).toDouble()) << ")\n"
        << "Connected: " << duration(s.value(QStringLiteral("seconds")).toDouble()) << ", "
        << s.value(QStringLiteral("connections")).toInt() << " connection(s)\n";
    const QVariantList locations = s.value(QStringLiteral("locations")).toList();
    if (!locations.isEmpty()) {
        out << "\nLocations\n";
        for (const QVariant &entry : locations.mid(0, 5)) {
            const QVariantMap l = entry.toMap();
            out << "  " << pretty(l.value(QStringLiteral("location")).toString()).leftJustified(20)
                << duration(l.value(QStringLiteral("seconds")).toDouble()).leftJustified(14)
                << bytes(l.value(QStringLiteral("in")).toDouble() + l.value(QStringLiteral("out")).toDouble()) << '\n';
        }
    }
    const QVariantList apps = s.value(QStringLiteral("apps")).toList();
    if (!apps.isEmpty()) {
        out << "\nApps\n";
        for (const QVariant &entry : apps.mid(0, 8)) {
            const QVariantMap a = entry.toMap();
            out << "  " << a.value(QStringLiteral("name")).toString().leftJustified(24)
                << bytes(a.value(QStringLiteral("in")).toDouble() + a.value(QStringLiteral("out")).toDouble()) << '\n';
        }
    }
    return 0;
}

void usage()
{
    out << "Usage: agvctl COMMAND [ARGS]\n\n"
           "  status [--json]              connection, kill switch, CLI version\n"
           "  connect [LOCATION]           connect (city, country or ISO code; default: the last one)\n"
           "  connect --fastest            connect to the fastest location\n"
           "  disconnect                   disconnect (and stop reconnecting)\n"
           "  locations [FILTER]           locations by ping\n"
           "  sites [list]                 website exclusions\n"
           "  sites add|remove SITE...     add or remove websites (domain.net also adds *.domain.net\n"
           "                               only in the window; here exactly what you type)\n"
           "  sites on|off SITE...         switch websites on or off without removing them\n"
           "  sites mode general|selective\n"
           "  apps [list]                  excluded apps\n"
           "  apps on|off NAME|ID          switch an app exclusion\n"
           "  apps mode bypass|vpn-only\n"
           "  killswitch on|off            block traffic outside the VPN while it should be up\n"
           "  dns on|off                   keep DNS inside the VPN\n"
           "  stats [today|week|month|all] [--csv]\n"
           "  version\n";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("agvctl"));
    agv::registerDBusTypes();

    QStringList args = app.arguments().mid(1);
    const QString command = args.isEmpty() ? QStringLiteral("status") : args.takeFirst();

    if (command == QLatin1String("help") || command == QLatin1String("--help") || command == QLatin1String("-h")) {
        usage();
        return 0;
    }
    if (command == QLatin1String("version") || command == QLatin1String("--version")) {
        out << "agvctl " << AGV_VERSION << '\n';
        return 0;
    }
    if (command == QLatin1String("status"))
        return status(args);
    if (command == QLatin1String("connect")) {
        if (args.contains(QLatin1String("--fastest")))
            call(QStringLiteral("ConnectFastest"));
        else
            call(QStringLiteral("Connect"), {args.join(QLatin1Char(' '))});
        return status({});
    }
    if (command == QLatin1String("disconnect")) {
        call(QStringLiteral("Disconnect"));
        return status({});
    }
    if (command == QLatin1String("locations"))
        return locations(args);
    if (command == QLatin1String("sites"))
        return sites(args);
    if (command == QLatin1String("apps"))
        return apps(args);
    if (command == QLatin1String("killswitch")) {
        if (args.isEmpty()) {
            out << "Kill switch: " << onOff(allProperties().value(QStringLiteral("KillSwitch")).toBool()) << '\n';
            return 0;
        }
        call(QStringLiteral("SetKillSwitch"), {parseOnOff(args.first())});
        return 0;
    }
    if (command == QLatin1String("dns")) {
        if (args.isEmpty()) {
            out << "DNS inside the VPN: " << onOff(allProperties().value(QStringLiteral("KeepDnsInVpn")).toBool()) << '\n';
            return 0;
        }
        call(QStringLiteral("SetKeepDnsInVpn"), {parseOnOff(args.first())});
        return 0;
    }
    if (command == QLatin1String("stats"))
        return stats(args);

    err << "agvctl: unknown command \"" << command << "\"\n\n";
    usage();
    return 2;
}
