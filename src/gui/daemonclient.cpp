#include "daemonclient.h"

#include "keyedlistmodel.h"
#include "locationmodel.h"
#include "siteexclusion.h"
#include "statistics.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDesktopServices>
#include <QFile>
#include <QStandardPaths>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <tuple>

namespace agv {

namespace {

constexpr int CallTimeoutMs = 180'000; // connect can take a while, polkit dialogs longer

const QString PropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

} // namespace

DaemonClient::DaemonClient(QDBusConnection bus, QObject *parent)
    : QObject(parent)
    , m_bus(std::move(bus))
    , m_watcher(new QDBusServiceWatcher(DBusService, m_bus,
                                        QDBusServiceWatcher::WatchForOwnerChange, this))
    , m_locations(new LocationModel(this))
    , m_siteModel(new KeyedListModel("item", {"item", "on", "kind", "site", "siteOn", "subCount", "subOn"}, this))
    , m_appModel(new KeyedListModel("id", {"id", "name", "icon", "desktop_file", "exe", "cmdline_match", "enabled",
                                           "bytes_out", "bytes_in"}, this))
{
    registerDBusTypes();

    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
        setAvailable(!newOwner.isEmpty());
        if (!newOwner.isEmpty())
            loadAll();
    });

    m_bus.connect(DBusService, DBusPath, PropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                  SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    m_bus.connect(DBusService, DBusPath, DBusInterface, QStringLiteral("LoginUrl"), this,
                  SLOT(onLoginUrl(QString)));
    m_bus.connect(DBusService, DBusPath, DBusInterface, QStringLiteral("LoginFinished"), this,
                  SLOT(onLoginFinished(bool,QString)));
    m_bus.connect(DBusService, DBusPath, DBusInterface, QStringLiteral("SiteExclusionsChanged"), this,
                  SLOT(onSiteExclusionsChanged()));
    m_bus.connect(DBusService, DBusPath, DBusInterface, QStringLiteral("AppExclusionsChanged"), this,
                  SLOT(onAppExclusionsChanged()));

    loadAll();
}

QObject *DaemonClient::locations() const
{
    return m_locations;
}

QObject *DaemonClient::siteExclusionModel() const
{
    return m_siteModel;
}

QObject *DaemonClient::appModel() const
{
    return m_appModel;
}

bool DaemonClient::transitioning() const
{
    const QString s = state();
    return s == QLatin1String("connecting") || s == QLatin1String("disconnecting");
}

void DaemonClient::connectTo(const QString &location)
{
    call(QStringLiteral("Connect"), {location}, {}, true);
}

void DaemonClient::connectFastest()
{
    call(QStringLiteral("ConnectFastest"), {}, {}, true);
}

void DaemonClient::disconnectVpn()
{
    call(QStringLiteral("Disconnect"), {}, {}, true);
}

void DaemonClient::toggle()
{
    // While agvd reconnects after a drop, the button stops it (and frees a kill-switched network).
    if (reconnecting()) {
        disconnectVpn();
        return;
    }
    if (busy() || transitioning())
        return;
    if (connected())
        disconnectVpn();
    else if (m_externalVpnCount > 0)
        takeOver();
    else
        connectTo();
}

void DaemonClient::takeOver()
{
    call(QStringLiteral("StopExternalVpns"), {}, [this](const QDBusMessage &) { connectTo(); }, true);
}

void DaemonClient::refreshLocations()
{
    m_locations->setLoading(true);
    call(QStringLiteral("ListLocations"), {}, [this](const QDBusMessage &reply) {
        m_locations->setLocations(qdbus_cast<AgvLocationList>(reply.arguments().value(0)));
        m_locations->setLoading(false);
    });
}

void DaemonClient::refreshSiteExclusions()
{
    call(QStringLiteral("ListSiteExclusions"), {}, [this](const QDBusMessage &reply) {
        auto entries = qdbus_cast<AgvSiteExclusionList>(reply.arguments().value(0));
        // "domain.net" first, then "*.domain.net", then the next domain; addresses last.
        std::sort(entries.begin(), entries.end(), [](const AgvSiteExclusion &a, const AgvSiteExclusion &b) {
            return site::sortKey(a.item) < site::sortKey(b.item);
        });

        QSet<QString> present;
        for (const AgvSiteExclusion &entry : entries)
            present.insert(entry.item);

        // The entry that heads the group of everything under domain "a": "a", else "www.a", else "*.a".
        const auto headOf = [&present](const QString &a) {
            for (const QString &candidate : {a, QStringLiteral("www.") + a, QStringLiteral("*.") + a}) {
                if (present.contains(candidate))
                    return candidate;
            }
            return QString();
        };

        QList<QVariantMap> top;
        QHash<QString, QList<QVariantMap>> children;  // group head → its rows
        for (const AgvSiteExclusion &entry : entries) {
            // domain, subdomains (*.domain.net), zone (*.ru), address, range; legacy for entries
            // the CLI kept from before that do not fit its own syntax (e.g. a bare "ru").
            const site::Parsed parsed = site::parse(entry.item);
            QString kind = QStringLiteral("legacy");
            if (parsed.ok() && parsed.value == entry.item) {
                switch (parsed.kind) {
                case site::Kind::Domain: kind = QStringLiteral("domain"); break;
                case site::Kind::Address: kind = QStringLiteral("address"); break;
                case site::Kind::Range: kind = QStringLiteral("range"); break;
                case site::Kind::Wildcard:
                    kind = entry.item.indexOf(QLatin1Char('.'), 2) > 0 ? QStringLiteral("subdomains")
                                                                         : QStringLiteral("zone");
                    break;
                }
            }
            // Sites fold under the shortest listed domain above them: "cs.avito.ru" and "*.avito.ru"
            // under "avito.ru". Zones (*.ru) and addresses stand alone.
            QString head;
            if (kind == QLatin1String("domain") || kind == QLatin1String("subdomains")) {
                const QStringList labels = (kind == QLatin1String("subdomains") ? entry.item.mid(2) : entry.item)
                                               .split(QLatin1Char('.'));
                for (qsizetype i = labels.size() - 2; i >= 0 && head.isEmpty(); --i)
                    head = headOf(labels.mid(i).join(QLatin1Char('.')));
                if (head == entry.item)
                    head.clear();
            }
            const QVariantMap row{{QStringLiteral("item"), entry.item},
                                  {QStringLiteral("on"), entry.enabled},
                                  {QStringLiteral("kind"), kind},
                                  {QStringLiteral("site"), head},
                                  {QStringLiteral("siteOn"), true},
                                  {QStringLiteral("subCount"), 0},
                                  {QStringLiteral("subOn"), 0}};
            if (head.isEmpty())
                top.append(row);
            else
                children[head].append(row);
        }

        // Sites by name (avito.ru, *.github.com, ozon.ru), then zones, addresses, leftovers.
        const auto order = [](const QVariantMap &row) {
            static const QStringList kinds{QStringLiteral("domain"), QStringLiteral("subdomains"),
                                           QStringLiteral("zone"), QStringLiteral("address"),
                                           QStringLiteral("range"), QStringLiteral("legacy")};
            const QString item = row.value(QStringLiteral("item")).toString();
            // "domain.net" and a lone "*.domain.net" sort together.
            const int group = qMax(1, int(kinds.indexOf(row.value(QStringLiteral("kind")).toString())));
            QString name = item;
            if (name.startsWith(QLatin1String("*.")))
                name.remove(0, 2);
            else if (name.startsWith(QLatin1String("www.")))
                name.remove(0, 4);
            return std::make_tuple(group, name, item);
        };
        std::stable_sort(top.begin(), top.end(),
                         [&order](const QVariantMap &a, const QVariantMap &b) { return order(a) < order(b); });

        // Each site followed by its folded rows. Under a switched-off site they keep their own "on"
        // but are not in effect.
        QList<QVariantMap> rows;
        QStringList active;
        m_siteGroups.clear();
        for (QVariantMap row : std::as_const(top)) {
            const QString item = row.value(QStringLiteral("item")).toString();
            const bool on = row.value(QStringLiteral("on")).toBool();
            QList<QVariantMap> sub = children.value(item);
            int subOn = 0;
            for (QVariantMap &member : sub) {
                member[QStringLiteral("siteOn")] = on;
                m_siteGroups[item].append(member.value(QStringLiteral("item")).toString());
                if (member.value(QStringLiteral("on")).toBool()) {
                    ++subOn;
                    if (on)
                        active.append(member.value(QStringLiteral("item")).toString());
                }
            }
            if (on)
                active.append(item);
            row[QStringLiteral("subCount")] = int(sub.size());
            row[QStringLiteral("subOn")] = subOn;
            rows.append(row);
            rows.append(sub);
        }

        m_siteExclusions = active;
        m_siteExclusionsLoaded = true;
        m_siteModel->setRows(rows);
        emit siteExclusionsChanged();
    });
}

QVariantMap DaemonClient::checkSites(const QString &text, bool includeSubdomains) const
{
    QStringList entries;
    QStringList problems;
    for (const site::Parsed &parsed : site::parseList(text)) {
        if (parsed.ok()) {
            for (const QString &entry : site::expand(parsed, includeSubdomains)) {
                if (!entries.contains(entry))
                    entries.append(entry);
            }
            continue;
        }
        switch (parsed.problem) {
        case site::Problem::StarWithoutDot:
            problems.append(tr("“%1”: write %2 for all subdomains").arg(parsed.input, parsed.suggestion));
            break;
        case site::Problem::StarInside:
            problems.append(tr("“%1”: * is only allowed at the start, as *.domain").arg(parsed.input));
            break;
        case site::Problem::BareZone:
            problems.append(tr("“%1”: for the whole zone write %2").arg(parsed.input, parsed.suggestion));
            break;
        default:
            problems.append(tr("“%1” is not a domain, IP address or range").arg(parsed.input));
            break;
        }
    }
    return {{QStringLiteral("entries"), entries}, {QStringLiteral("problems"), problems}};
}

void DaemonClient::addSiteExclusions(const QString &text, bool includeSubdomains)
{
    const QVariantMap check = checkSites(text, includeSubdomains);
    const QStringList problems = check.value(QStringLiteral("problems")).toStringList();
    if (!problems.isEmpty())
        emit errorOccurred(problems.first());
    const QStringList entries = check.value(QStringLiteral("entries")).toStringList();
    if (entries.isEmpty())
        return;
    call(QStringLiteral("AddSiteExclusions"), {entries}, [this](const QDBusMessage &) { refreshSiteExclusions(); });
}

void DaemonClient::setSiteExclusionEnabled(const QString &item, bool enabled)
{
    // A site switches the rows folded under it along with itself.
    call(QStringLiteral("SetSiteExclusionEnabled"), {item, enabled, m_siteGroups.value(item)},
         [this](const QDBusMessage &) { refreshSiteExclusions(); });
}

void DaemonClient::removeSiteExclusion(const QString &item)
{
    // Removing a site removes the rows folded under it too.
    call(QStringLiteral("RemoveSiteExclusions"), {QStringList{item} + m_siteGroups.value(item)},
         [this](const QDBusMessage &) { refreshSiteExclusions(); });
}

void DaemonClient::setSiteExclusionMode(const QString &mode)
{
    call(QStringLiteral("SetSiteExclusionMode"), {mode}, [this](const QDBusMessage &) { refreshSiteExclusions(); });
}

void DaemonClient::refreshApps()
{
    call(QStringLiteral("ListAppExclusions"), {}, [this](const QDBusMessage &reply) {
        m_appRules = qdbus_cast<AgvAppExclusionList>(reply.arguments().value(0));
        m_appsLoaded = true;
        publishApps();
    });
}

void DaemonClient::refreshAppTraffic()
{
    if (!m_available || m_appRules.isEmpty())
        return;
    call(QStringLiteral("GetAppTraffic"), {}, [this](const QDBusMessage &reply) {
        const auto traffic = qdbus_cast<AgvAppTrafficList>(reply.arguments().value(0));
        m_traffic.clear();
        for (const AgvAppTraffic &entry : traffic)
            m_traffic.insert(entry.id, {entry.bytesOut, entry.bytesIn});
        publishApps();
    });
}

void DaemonClient::addApp(const QVariantMap &fields)
{
    // Only send what the daemon knows; empty strings are fine.
    static const QStringList keys{QStringLiteral("name"), QStringLiteral("icon"), QStringLiteral("desktop_file"),
                                  QStringLiteral("exe"), QStringLiteral("cmdline_match")};
    QVariantMap rule;
    for (const QString &key : keys) {
        if (fields.contains(key))
            rule.insert(key, fields.value(key).toString());
    }
    call(QStringLiteral("AddAppExclusion"), {rule}, [this, name = rule.value(QStringLiteral("name")).toString()](const QDBusMessage &) {
        emit info(appExclusionMode() == QLatin1String("vpn-only")
                      ? tr("%1 now uses the VPN. Restart it if it was already running.").arg(name)
                      : tr("%1 now bypasses the VPN. Restart it if it was already running.").arg(name));
        refreshApps();
    });
}

void DaemonClient::setAppEnabled(const QString &id, bool enabled)
{
    call(QStringLiteral("UpdateAppExclusion"), {id, QVariantMap{{QStringLiteral("enabled"), enabled}}},
         [this](const QDBusMessage &) { refreshApps(); });
}

void DaemonClient::removeApp(const QString &id)
{
    call(QStringLiteral("RemoveAppExclusion"), {id}, [this](const QDBusMessage &) { refreshApps(); });
}

bool DaemonClient::isAppExcluded(const QString &desktopFile, const QString &exe) const
{
    for (const AgvAppExclusion &app : m_appRules) {
        const QString file = app.rule.value(QStringLiteral("desktop_file")).toString();
        const QString program = app.rule.value(QStringLiteral("exe")).toString();
        if ((!desktopFile.isEmpty() && file == desktopFile) || (!exe.isEmpty() && program == exe))
            return true;
    }
    return false;
}

void DaemonClient::setAppExclusionMode(const QString &mode)
{
    call(QStringLiteral("SetAppExclusionMode"), {mode});
}

void DaemonClient::refreshConfig()
{
    call(QStringLiteral("GetConfig"), {}, [this](const QDBusMessage &reply) {
        const auto config = qdbus_cast<AgvConfigMap>(reply.arguments().value(0));
        m_config.clear();
        for (auto it = config.cbegin(); it != config.cend(); ++it) {
            m_config.insert(it.key(), QVariantMap{{QStringLiteral("value"), it->value},
                                                  {QStringLiteral("isDefault"), it->isDefault}});
        }
        emit configChanged();
    });
}

void DaemonClient::setConfig(const QString &key, const QString &value)
{
    call(QStringLiteral("SetConfig"), {key, value}, [this](const QDBusMessage &) {
        if (connected()) {
            m_needsReconnect = true;
            emit info(tr("Saved. Reconnect to apply it."));
        } else {
            emit info(tr("Saved. Applies on the next connection."));
        }
        refreshConfig();
    });
}

void DaemonClient::reconnect()
{
    if (!connected() || busy())
        return;
    const QString location = this->location();
    call(QStringLiteral("Disconnect"), {}, [this, location](const QDBusMessage &) {
        m_needsReconnect = false;
        emit configChanged();
        connectTo(location);
    }, true);
}

void DaemonClient::setStartAtBoot(bool enabled)
{
    call(QStringLiteral("SetStartAtBoot"), {enabled});
}

void DaemonClient::setAutoConnect(bool enabled)
{
    call(QStringLiteral("SetAutoConnect"), {enabled});
}

void DaemonClient::setKillSwitch(bool enabled)
{
    call(QStringLiteral("SetKillSwitch"), {enabled});
}

void DaemonClient::setKillSwitchAllowLan(bool enabled)
{
    call(QStringLiteral("SetKillSwitchAllowLan"), {enabled});
}

QVariantList DaemonClient::trustedNetworks() const
{
    const QVariantMap networks = m_props.value(QStringLiteral("TrustedNetworks")).toMap();
    QVariantList result;
    for (auto it = networks.cbegin(); it != networks.cend(); ++it)
        result.append(QVariantMap{{QStringLiteral("uuid"), it.key()}, {QStringLiteral("name"), it.value().toString()}});
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().localeAwareCompare(
                   b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    return result;
}

void DaemonClient::setWifiAutoConnect(bool enabled)
{
    call(QStringLiteral("SetWifiAutoConnect"), {enabled});
}

void DaemonClient::setTrustedDisconnect(bool enabled)
{
    call(QStringLiteral("SetTrustedDisconnect"), {enabled});
}

void DaemonClient::setNetworkTrusted(const QString &uuid, const QString &name, bool trusted)
{
    call(QStringLiteral("SetNetworkTrusted"), {uuid, name, trusted});
}

void DaemonClient::updateCli()
{
    call(QStringLiteral("UpdateCli"), {}, [this](const QDBusMessage &) {
        emit info(connected() ? tr("AdGuard VPN CLI is updated. Reconnect to start using it.")
                              : tr("AdGuard VPN CLI is updated."));
    }, true);
}

void DaemonClient::setKeepDnsInVpn(bool enabled)
{
    call(QStringLiteral("SetKeepDnsInVpn"), {enabled});
}

void DaemonClient::refreshTunnelTraffic()
{
    const QString device = m_props.value(QStringLiteral("TunDevice")).toString();
    // SOCKS5 mode has no tunnel: the VPN's traffic is what agvd counts for itself and the CLI.
    if (connected() && device.isEmpty() && appExclusionsSupported()) {
        call(QStringLiteral("GetAppTraffic"), {}, [this](const QDBusMessage &reply) {
            for (const AgvAppTraffic &entry : qdbus_cast<AgvAppTrafficList>(reply.arguments().value(0))) {
                if (entry.id == QLatin1String("agvd")) {
                    m_tunnelIn = double(entry.bytesIn);
                    m_tunnelOut = double(entry.bytesOut);
                }
            }
            emit tunnelTrafficChanged();
        });
        return;
    }
    // Interface names never contain '/', but this path is built from D-Bus data.
    if (!connected() || device.isEmpty() || device.contains(QLatin1Char('/'))) {
        if (m_tunnelIn != 0 || m_tunnelOut != 0) {
            m_tunnelIn = m_tunnelOut = 0;
            emit tunnelTrafficChanged();
        }
        return;
    }
    auto read = [&device](const char *counter) {
        QFile file(QStringLiteral("/sys/class/net/%1/statistics/%2").arg(device, QLatin1String(counter)));
        return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed().toDouble() : 0.0;
    };
    // Emitted on every read, so a speed chart also sees the seconds without traffic.
    m_tunnelIn = read("rx_bytes");
    m_tunnelOut = read("tx_bytes");
    emit tunnelTrafficChanged();
}

void DaemonClient::onAppExclusionsChanged()
{
    refreshApps();
}

void DaemonClient::publishApps()
{
    m_apps.clear();
    for (const AgvAppExclusion &app : m_appRules) {
        QVariantMap entry = app.rule;
        entry.insert(QStringLiteral("id"), app.id);
        const auto traffic = m_traffic.value(app.id);
        // QML numbers are doubles; byte counts stay exact far beyond what an app will use.
        entry.insert(QStringLiteral("bytes_out"), double(traffic.first));
        entry.insert(QStringLiteral("bytes_in"), double(traffic.second));
        m_apps.append(entry);
    }
    std::sort(m_apps.begin(), m_apps.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().localeAwareCompare(
                   b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    QList<QVariantMap> rows;
    for (const QVariant &app : std::as_const(m_apps))
        rows.append(app.toMap());
    m_appModel->setRows(rows);
    emit appsChanged();
}

void DaemonClient::refreshLicense()
{
    call(QStringLiteral("GetLicense"), {}, [this](const QDBusMessage &reply) {
        m_license = qdbus_cast<QVariantMap>(reply.arguments().value(0));
        emit licenseChanged();
    });
}

void DaemonClient::refreshStatistics()
{
    if (!m_available)
        return;
    call(QStringLiteral("GetStatistics"), {}, [this](const QDBusMessage &reply) {
        m_statistics = QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8()).object();
        m_statisticsAvailable = true;
        ++m_statisticsRevision;
        emit statisticsChanged();
    });
}

void DaemonClient::resetStatistics()
{
    call(QStringLiteral("ResetStatistics"), {}, [this](const QDBusMessage &) { refreshStatistics(); });
}

QVariantMap DaemonClient::statisticsFor(const QString &period, const QString &appId) const
{
    return stats::summarize(m_statistics, period, QDateTime::currentDateTime(), appId);
}

void DaemonClient::exportStatistics()
{
    QString folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (folder.isEmpty() || !QDir(folder).exists())
        folder = QDir::homePath();
    const QString path = QStringLiteral("%1/agv-statistics-%2.csv")
                             .arg(folder, QDate::currentDate().toString(Qt::ISODate));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(stats::toCsv(m_statistics)) < 0 || !file.commit()) {
        emit errorOccurred(tr("Cannot save %1").arg(path));
        return;
    }
    emit info(tr("Saved to %1").arg(QDir::toNativeSeparators(path)));
}

void DaemonClient::login()
{
    call(QStringLiteral("Login"), {}, [this](const QDBusMessage &) {
        m_loginPending = true;
        emit loginPendingChanged();
    });
}

void DaemonClient::cancelLogin()
{
    call(QStringLiteral("CancelLogin"));
}

void DaemonClient::logout()
{
    call(QStringLiteral("Logout"), {}, [this](const QDBusMessage &) {
        m_license.clear();
        emit licenseChanged();
    }, true);
}

void DaemonClient::importLogin()
{
    call(QStringLiteral("ImportLogin"), {}, [this](const QDBusMessage &) {
        emit info(tr("Signed in with your adguardvpn-cli account"));
        loadAll();
    }, true);
}

void DaemonClient::onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface == DBusInterface)
        applyProperties(changed);
}

void DaemonClient::onLoginUrl(const QString &url)
{
    // Only ever hand a web link to the browser.
    const QUrl link(url);
    if (link.scheme() != QLatin1String("https")) {
        emit errorOccurred(tr("The login link from AdGuard VPN looks wrong: %1").arg(url));
        return;
    }
    if (!QDesktopServices::openUrl(link))
        emit info(tr("Open this link to sign in: %1").arg(url));
    else
        emit info(tr("Confirm the sign-in in your browser"));
}

void DaemonClient::onLoginFinished(bool success, const QString &message)
{
    m_loginPending = false;
    emit loginPendingChanged();
    if (success) {
        emit info(tr("Signed in"));
        loadAll();
    } else if (!message.isEmpty()) {
        emit errorOccurred(tr("Sign-in failed: %1").arg(message));
    }
}

void DaemonClient::onSiteExclusionsChanged()
{
    refreshSiteExclusions();
}

void DaemonClient::call(const QString &method, const QVariantList &args, Reply onSuccess, bool trackBusy)
{
    QDBusMessage message = QDBusMessage::createMethodCall(DBusService, DBusPath, DBusInterface, method);
    message.setArguments(args);
    // Let polkit ask for a password where the action needs one.
    message.setInteractiveAuthorizationAllowed(true);

    if (trackBusy)
        setBusy(true);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, method, onSuccess = std::move(onSuccess), trackBusy](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        if (trackBusy)
            setBusy(false);
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            // An older agvd without statistics: the page says so instead of a toast every few seconds.
            if (method == QLatin1String("GetStatistics")) {
                m_statisticsAvailable = false;
                emit statisticsChanged();
                return;
            }
            if (method == QLatin1String("ListLocations"))
                m_locations->setLoading(false);
            emit errorOccurred(describe(reply));
            return;
        }
        if (onSuccess)
            onSuccess(reply);
    });
}

void DaemonClient::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    emit availableChanged();
}

void DaemonClient::loadAll()
{
    QDBusMessage message = QDBusMessage::createMethodCall(DBusService, DBusPath, PropertiesInterface,
                                                          QStringLiteral("GetAll"));
    message << DBusInterface;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        setAvailable(reply.type() != QDBusMessage::ErrorMessage);
        if (!m_available)
            return;
        applyProperties(qdbus_cast<QVariantMap>(reply.arguments().value(0)));
        refreshSiteExclusions();
        refreshApps();
        refreshConfig();
        if (loggedIn()) {
            refreshLocations();
            refreshLicense();
        }
    });
}

void DaemonClient::applyProperties(const QVariantMap &props)
{
    const bool wasLoggedIn = loggedIn();
    for (auto it = props.cbegin(); it != props.cend(); ++it) {
        if (it.key() == QLatin1String("ExternalVpns")) {
            const auto list = qdbus_cast<AgvExternalVpnList>(it.value());
            m_externalVpnCount = int(list.size());
        } else if (it.value().metaType() == QMetaType::fromType<QDBusArgument>()) {
            // Nested dictionaries (Network, TrustedNetworks) arrive still marshalled.
            m_props.insert(it.key(), qdbus_cast<QVariantMap>(it.value()));
        } else {
            m_props.insert(it.key(), it.value());
        }
    }
    if (!connected() && !transitioning() && m_needsReconnect) {
        m_needsReconnect = false; // the next connection uses the new settings anyway
        emit configChanged();
    }
    emit statusChanged();
    if (props.contains(QStringLiteral("SiteExclusionMode")))
        emit siteExclusionsChanged();

    if (!wasLoggedIn && loggedIn()) {
        refreshLocations();
        refreshLicense();
    }
}

void DaemonClient::setBusy(bool busy)
{
    const bool before = this->busy();
    m_busy += busy ? 1 : -1;
    if (before != this->busy())
        emit busyChanged();
}

QString DaemonClient::describe(const QDBusMessage &error)
{
    const QString name = error.errorName();
    if (name == QLatin1String("org.agv.Error.NotLoggedIn"))
        return tr("Sign in to AdGuard VPN first");
    if (name == QLatin1String("org.agv.Error.NotAuthorized")
        || name == QLatin1String("org.freedesktop.DBus.Error.AccessDenied"))
        return tr("Not allowed: %1").arg(error.errorMessage());
    if (name == QLatin1String("org.freedesktop.DBus.Error.InteractiveAuthorizationRequired"))
        return tr("This needs an administrator password");
    if (name == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")
        || name == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner"))
        return tr("The agvd service is not running");
    if (name == QLatin1String("org.freedesktop.DBus.Error.NoReply")
        || name == QLatin1String("org.freedesktop.DBus.Error.Timeout"))
        return tr("The agvd service did not answer in time");
    return error.errorMessage();
}

} // namespace agv
