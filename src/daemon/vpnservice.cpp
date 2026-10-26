#include "vpnservice.h"

#include "clirunner.h"
#include "siteexclusion.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <csignal>

namespace agv {

namespace {

constexpr int PollIntervalMs = 10'000;
constexpr int ConnectTimeoutMs = 90'000;
constexpr int DisconnectTimeoutMs = 30'000;
constexpr int LocationsTimeoutMs = 30'000;
constexpr int LoginTimeoutMs = 30 * 60'000; // the device-code link lives ~30 minutes
constexpr int ExternalStopPollMs = 250;
constexpr int AutoConnectRetryMs = 30'000;
constexpr int AutoConnectMaxAttempts = 20;   // ~10 minutes of waiting for the network
constexpr int ExternalStopTimeoutMs = 10'000;
// Pauses between reconnect attempts after a drop; the last one repeats until it works.
constexpr int ReconnectDelaysMs[] = {3'000, 5'000, 10'000, 20'000, 30'000, 60'000};

QString lastMeaningfulLine(const QString &output)
{
    const QStringList lines = cli::stripAnsi(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QString line = it->trimmed();
        // Skip the CLI's "You can ... by running `...`" hints.
        if (!line.isEmpty() && !line.startsWith(QLatin1String("You can ")))
            return line;
    }
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

bool isOnOff(const QString &value)
{
    return value == QLatin1String("on") || value == QLatin1String("off");
}

bool isUnsigned(const QString &value, uint max)
{
    bool ok = false;
    const uint number = value.toUInt(&ok);
    return ok && number <= max;
}

// Free-form values must never look like an option to the CLI.
bool isPlainToken(const QString &value)
{
    static const QRegularExpression token(QStringLiteral("^[^\\s-][^\\s]*$"));
    return token.match(value).hasMatch();
}

} // namespace

VpnService::VpnService(CliRunner *cli, QObject *parent)
    : QObject(parent)
    , m_cli(cli)
    , m_poll(new QTimer(this))
    , m_autoConnectRetry(new QTimer(this))
    , m_reconnect(new QTimer(this))
{
    m_reconnect->setSingleShot(true);
    connect(m_reconnect, &QTimer::timeout, this, &VpnService::reconnectNow);
    m_autoConnectRetry->setSingleShot(true);
    connect(m_autoConnectRetry, &QTimer::timeout, this, &VpnService::tryAutoConnect);
    m_poll->setSingleShot(true);
    connect(m_poll, &QTimer::timeout, this, [this] {
        if (m_transitioning)
            schedulePoll();
        else
            refreshStatus();
    });
}

QString VpnService::stateName() const
{
    switch (m_state) {
    case State::Unknown: return QStringLiteral("unknown");
    case State::NotLoggedIn: return QStringLiteral("not-logged-in");
    case State::Disconnected: return QStringLiteral("disconnected");
    case State::Connecting: return QStringLiteral("connecting");
    case State::Connected: return QStringLiteral("connected");
    case State::Disconnecting: return QStringLiteral("disconnecting");
    case State::Error: return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

void VpnService::start()
{
    refreshCliInfo();
    refreshStatus([this](const Error &) {
        refreshSiteExclusionMode();
        tryAutoConnect();
    });
}

void VpnService::setAutoConnect(bool on)
{
    m_autoConnect = on;
    if (!on)
        m_autoConnectRetry->stop();
}

void VpnService::tryAutoConnect()
{
    // Only from a plain disconnected state: never fight a user's choice or another VPN instance.
    const bool idle = m_state == State::Disconnected || m_state == State::Error; // Error: last try failed
    if (!m_autoConnect || m_transitioning || m_reconnecting || !idle || !m_externalVpns.isEmpty())
        return;
    if (++m_autoConnectAttempts > AutoConnectMaxAttempts)
        return;

    connectTo(QString(), [this](const Error &error) {
        if (error.isOk()) {
            m_autoConnectAttempts = 0;
            return;
        }
        // Usually the network is not up yet right after boot.
        if (error.name != errors::NotLoggedIn)
            m_autoConnectRetry->start(AutoConnectRetryMs);
    });
}

void VpnService::refreshStatus(Done done)
{
    m_cli->run({QStringLiteral("status")}, [this, done](const CliRunner::Result &result) {
        const auto status = result.failedToStart || result.timedOut
            ? std::nullopt
            : cli::parseStatus(result.output);

        Error error;
        if (!status) {
            error = errorFor({QStringLiteral("status")}, result.exitCode, result.output,
                             result.timedOut, result.failedToStart);
            // A zero exit code with output we cannot read is still a failure to know the state.
            if (error.isOk() || error.name == errors::CliFailed)
                error = {errors::UnexpectedOutput,
                         QStringLiteral("Unrecognized status: %1").arg(lastMeaningfulLine(result.output))};
            if (!m_transitioning) {
                setLastError(error.message);
                setStatus(State::Error);
            }
        } else if (!m_transitioning) {
            switch (status->state) {
            case cli::Status::State::NotLoggedIn:
                setStatus(State::NotLoggedIn);
                break;
            case cli::Status::State::Disconnected:
                setStatus(State::Disconnected);
                break;
            case cli::Status::State::Connected:
                setStatus(State::Connected, status->location, status->mode, status->tunDevice);
                break;
            }
        }

        updateExternalVpns();
        // Notice an install made while agvd runs.
        if (m_cliInstall == QLatin1String("missing") && !result.failedToStart)
            refreshCliInfo();
        schedulePoll();
        if (done)
            done(error);
    });
}

void VpnService::updateExternalVpns()
{
    // While our own VPN is up (or changing), another instance cannot be running alongside it.
    QList<ExternalVpn> found;
    if (m_state != State::Connected && m_state != State::Connecting && m_state != State::Disconnecting)
        found = findExternalVpns(m_cli->home(), m_procDir);

    if (found != m_externalVpns) {
        m_externalVpns = found;
        emit statusChanged();
    }
}

void VpnService::stopExternalVpns(Done done)
{
    if (m_transitioning) {
        done({errors::Busy, QStringLiteral("Another connection change is in progress")});
        return;
    }

    QList<pid_t> pending;
    for (const ExternalVpn &vpn : findExternalVpns(m_cli->home(), m_procDir)) {
        // Re-check right before signalling: the PID must still be the VPN we found.
        if (!isVpnProcess(vpn.pid, m_procDir))
            continue;
        if (::kill(vpn.pid, SIGTERM) != 0) {
            done({errors::NotAuthorized,
                  QStringLiteral("Cannot stop process %1: %2").arg(vpn.pid).arg(qt_error_string(errno))});
            return;
        }
        pending.append(vpn.pid);
    }

    m_transitioning = true;
    auto *timer = new QTimer(this);
    auto waitedMs = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [this, timer, pending, waitedMs, done] {
        *waitedMs += ExternalStopPollMs;
        const bool alive = std::any_of(pending.cbegin(), pending.cend(),
                                       [this](pid_t pid) { return isVpnProcess(pid, m_procDir); });
        if (alive && *waitedMs < ExternalStopTimeoutMs)
            return;

        timer->deleteLater();
        m_transitioning = false;
        const Error error = alive
            ? Error{errors::Timeout, QStringLiteral("The external VPN did not stop in time")}
            : Error{};
        refreshStatus([done, error](const Error &) { done(error); });
    });
    timer->start(ExternalStopPollMs);
}

void VpnService::importLogin(const QString &fromHome, uid_t ownerUid, Done done)
{
    if (m_transitioning || m_state == State::Connected) {
        done({errors::Busy, QStringLiteral("Disconnect before importing a login")});
        return;
    }
    if (!QFileInfo::exists(cliConfigPath(fromHome))) {
        done({errors::NotFound, QStringLiteral("No AdGuard VPN CLI login found in %1").arg(fromHome)});
        return;
    }

    const QString reason = importCliConfig(fromHome, ownerUid, m_cli->home());
    if (!reason.isEmpty()) {
        done({errors::InvalidArgument, reason});
        return;
    }

    emit siteExclusionsChanged();
    refreshStatus([this, done](const Error &error) {
        refreshSiteExclusionMode();
        done(error);
    });
}

void VpnService::connectTo(const QString &location, Done done)
{
    QStringList args{QStringLiteral("connect"), QStringLiteral("-y"), QStringLiteral("--no-progress")};
    const QString target = location.simplified();
    if (!target.isEmpty()) {
        // City names contain spaces ("New York"), so only reject what could look like an option.
        if (target.startsWith(QLatin1Char('-'))) {
            done({errors::InvalidArgument, QStringLiteral("Invalid location: %1").arg(location)});
            return;
        }
        args << QStringLiteral("-l") << target;
    }
    connectWith(args, std::move(done));
}

void VpnService::connectFastest(Done done)
{
    connectWith({QStringLiteral("connect"), QStringLiteral("-y"), QStringLiteral("--no-progress"),
                 QStringLiteral("-f")},
                std::move(done));
}

void VpnService::connectWith(const QStringList &args, Done done)
{
    if (m_transitioning) {
        done({errors::Busy, QStringLiteral("Another connection change is in progress")});
        return;
    }

    m_transitioning = true;
    setLastError({});
    setStatus(State::Connecting, m_location, m_mode, m_tunDevice);

    runChecked(args, [this, done](const QString &, const Error &error) {
        m_transitioning = false;
        if (!error.isOk())
            setLastError(error.message);
        refreshStatus([this, done, error](const Error &) {
            if (!error.isOk() && m_state == State::Disconnected)
                setStatus(State::Error);
            done(error);
        });
    }, ConnectTimeoutMs);
}

void VpnService::disconnectVpn(Done done)
{
    // An explicit disconnect ends any pending automatic attempts.
    m_autoConnectRetry->stop();
    const bool wasReconnecting = m_reconnecting;
    if (m_wanted || m_reconnecting) {
        m_wanted = false;
        stopReconnecting();
        emit statusChanged();
    }
    if (m_transitioning && wasReconnecting) {
        // A reconnect attempt is running: undo it as soon as it ends.
        m_disconnectAfterAttempt = true;
        done({});
        return;
    }
    if (m_transitioning) {
        done({errors::Busy, QStringLiteral("Another connection change is in progress")});
        return;
    }

    m_transitioning = true;
    setStatus(State::Disconnecting, m_location, m_mode, m_tunDevice);

    runChecked({QStringLiteral("disconnect")}, [this, done](const QString &, const Error &error) {
        m_transitioning = false;
        if (!error.isOk())
            setLastError(error.message);
        refreshStatus([done, error](const Error &) { done(error); });
    }, DisconnectTimeoutMs);
}

void VpnService::listLocations(Reply<QList<cli::Location>> reply)
{
    runChecked({QStringLiteral("list-locations")}, [reply](const QString &output, const Error &error) {
        if (!error.isOk()) {
            reply({}, error);
            return;
        }
        const auto locations = cli::parseLocations(output);
        if (!locations)
            reply({}, {errors::UnexpectedOutput, lastMeaningfulLine(output)});
        else
            reply(*locations, {});
    }, LocationsTimeoutMs);
}

void VpnService::license(Reply<cli::License> reply)
{
    runChecked({QStringLiteral("license")}, [reply](const QString &output, const Error &error) {
        if (!error.isOk()) {
            reply({}, error);
            return;
        }
        const auto license = cli::parseLicense(output);
        if (!license)
            reply({}, {errors::UnexpectedOutput, lastMeaningfulLine(output)});
        else
            reply(*license, {});
    });
}

void VpnService::siteExclusions(Reply<QStringList> reply)
{
    runChecked({QStringLiteral("site-exclusions"), QStringLiteral("show")},
               [reply](const QString &output, const Error &error) {
        if (!error.isOk()) {
            reply({}, error);
            return;
        }
        const auto exclusions = cli::parseSiteExclusions(output);
        if (!exclusions)
            reply({}, {errors::UnexpectedOutput, lastMeaningfulLine(output)});
        else
            reply(exclusions->domains, {});
    });
}

void VpnService::addSiteExclusions(const QStringList &items, Done done)
{
    // New entries must be exactly what the CLI understands.
    for (const QString &item : items) {
        if (!site::isValid(item)) {
            done({errors::InvalidArgument, QStringLiteral("Invalid exclusion: %1").arg(item)});
            return;
        }
    }
    if (items.isEmpty()) {
        done({});
        return;
    }

    runChecked(QStringList{QStringLiteral("site-exclusions"), QStringLiteral("add")} + items,
               [this, items, done](const QString &, const Error &error) {
        if (error.isOk()) {
            // Adding an entry that was switched off switches it back on.
            QStringList disabled = disabledSites();
            QStringList held = heldSites();
            for (const QString &item : items) {
                disabled.removeAll(item);
                held.removeAll(item);
            }
            saveDisabledSites(disabled);
            saveHeldSites(held);
            emit siteExclusionsChanged();
        }
        done(error);
    });
}

void VpnService::removeSiteExclusions(const QStringList &items, Done done)
{
    // Removal stays lenient so entries from older versions or the plain CLI can go too.
    for (const QString &item : items) {
        if (!isValidExclusion(item)) {
            done({errors::InvalidArgument, QStringLiteral("Invalid exclusion: %1").arg(item)});
            return;
        }
    }

    // Switched-off entries live only in our store; the rest are removed from the CLI.
    QStringList disabled = disabledSites();
    QStringList held = heldSites();
    QStringList fromCli;
    for (const QString &item : items) {
        if (!disabled.removeAll(item) && !held.removeAll(item))
            fromCli.append(item);
    }
    saveDisabledSites(disabled);
    saveHeldSites(held);
    if (fromCli.isEmpty()) {
        emit siteExclusionsChanged();
        done({});
        return;
    }

    runChecked(QStringList{QStringLiteral("site-exclusions"), QStringLiteral("remove")} + fromCli,
               [this, done](const QString &, const Error &error) {
        emit siteExclusionsChanged();
        done(error);
    });
}

void VpnService::listSiteExclusions(Reply<QList<QPair<QString, bool>>> reply)
{
    siteExclusions([this, reply](const QStringList &active, const Error &error) {
        if (!error.isOk()) {
            reply({}, error);
            return;
        }
        QList<QPair<QString, bool>> result;
        for (const QString &item : active)
            result.append({item, true});
        for (const QString &item : disabledSites()) {
            if (!active.contains(item))
                result.append({item, false});
        }
        // Held entries keep their own "on": the GUI shows them under the switched-off site.
        for (const QString &item : heldSites()) {
            if (!active.contains(item))
                result.append({item, true});
        }
        reply(result, {});
    });
}

void VpnService::setSiteExclusionEnabled(const QString &item, bool enabled, const QStringList &members, Done done)
{
    for (const QString &entry : QStringList{item} + members) {
        if (!isValidExclusion(entry)) {
            done({errors::InvalidArgument, QStringLiteral("Invalid exclusion: %1").arg(entry)});
            return;
        }
    }
    const QStringList disabled = disabledSites();
    const QStringList held = heldSites();
    if (enabled) {
        // Back on together with the members it held off.
        QStringList items{item};
        for (const QString &member : members) {
            if (held.contains(member))
                items.append(member);
        }
        addSiteExclusions(items, std::move(done));
        return;
    }

    // Off: out of the CLI's list, remembered here so it can come back. Members still on are held.
    QStringList hold;
    for (const QString &member : members) {
        if (!disabled.contains(member) && !held.contains(member))
            hold.append(member);
    }
    runChecked(QStringList{QStringLiteral("site-exclusions"), QStringLiteral("remove"), item} + hold,
               [this, item, hold, done](const QString &, const Error &error) {
        if (error.isOk()) {
            QStringList disabled = disabledSites();
            if (!disabled.contains(item))
                disabled.append(item);
            saveDisabledSites(disabled);
            QStringList held = heldSites();
            for (const QString &member : hold) {
                if (!held.contains(member))
                    held.append(member);
            }
            saveHeldSites(held);
            emit siteExclusionsChanged();
        }
        done(error);
    });
}

QString VpnService::disabledSitesPath() const
{
    return m_cli->home() + QStringLiteral("/site-exclusions-disabled.json");
}

QStringList VpnService::storedSites(const QString &key) const
{
    // { "general": [...], "general-held": [...], "selective": [...], ... }: the CLI keeps one list
    // per mode, and so do we.
    QFile file(disabledSitesPath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QStringList items;
    for (const QJsonValue &value : root.value(key).toArray())
        items.append(value.toString());
    return items;
}

void VpnService::saveStoredSites(const QString &key, const QStringList &items)
{
    QJsonObject root;
    QFile in(disabledSitesPath());
    if (in.open(QIODevice::ReadOnly))
        root = QJsonDocument::fromJson(in.readAll()).object();
    if (items.isEmpty())
        root.remove(key);
    else
        root.insert(key, QJsonArray::fromStringList(items));

    QSaveFile out(disabledSitesPath());
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    out.write(QJsonDocument(root).toJson());
    out.commit();
}

QString VpnService::currentSiteMode() const
{
    return m_siteExclusionMode.isEmpty() ? QStringLiteral("general") : m_siteExclusionMode;
}

void VpnService::setSiteExclusionMode(const QString &mode, Done done)
{
    if (mode != QLatin1String("general") && mode != QLatin1String("selective")) {
        done({errors::InvalidArgument, QStringLiteral("Mode must be general or selective")});
        return;
    }

    runChecked({QStringLiteral("site-exclusions"), QStringLiteral("mode"), mode},
               [this, mode, done](const QString &, const Error &error) {
        if (error.isOk() && m_siteExclusionMode != mode) {
            m_siteExclusionMode = mode;
            emit siteExclusionsChanged();
        }
        done(error);
    });
}

void VpnService::config(Reply<QMap<QString, cli::ConfigValue>> reply)
{
    runChecked({QStringLiteral("config"), QStringLiteral("show")},
               [reply](const QString &output, const Error &error) {
        if (!error.isOk()) {
            reply({}, error);
            return;
        }
        const auto config = cli::parseConfig(output);
        if (!config)
            reply({}, {errors::UnexpectedOutput, lastMeaningfulLine(output)});
        else
            reply(*config, {});
    });
}

void VpnService::setConfig(const QString &key, const QString &value, Done done)
{
    if (!isValidConfig(key, value)) {
        done({errors::InvalidArgument, QStringLiteral("Unsupported setting %1=%2").arg(key, value)});
        return;
    }
    runChecked({QStringLiteral("config"), QStringLiteral("set-") + key, value},
               [done](const QString &, const Error &error) { done(error); });
}

void VpnService::startLogin(Done done)
{
    if (m_login) {
        done({errors::Busy, QStringLiteral("Login is already in progress")});
        return;
    }

    m_login = m_cli->createProcess({QStringLiteral("login")}, this);
    QProcess *process = m_login;
    auto output = std::make_shared<QString>();
    auto urlSent = std::make_shared<bool>(false);

    connect(process, &QProcess::readyRead, this, [this, process, output, urlSent] {
        output->append(QString::fromUtf8(process->readAll()));
        if (*urlSent)
            return;
        const QString url = cli::parseLoginUrl(*output);
        if (!url.isEmpty()) {
            *urlSent = true;
            emit loginUrlReady(url);
        }
    });
    connect(process, &QProcess::finished, this,
            [this, process, output](int exitCode, QProcess::ExitStatus status) {
        output->append(QString::fromUtf8(process->readAll()));
        process->deleteLater();
        const bool success = status == QProcess::NormalExit && exitCode == 0;
        emit loginFinished(success, success ? QString() : lastMeaningfulLine(*output));
        refreshStatus([this](const Error &) { refreshSiteExclusionMode(); });
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        emit loginFinished(false, process->errorString());
    });

    QTimer::singleShot(LoginTimeoutMs, process, [process] { process->kill(); });
    process->start();
    done({});
}

void VpnService::cancelLogin()
{
    if (m_login)
        m_login->kill();
}

void VpnService::logout(Done done)
{
    if (m_wanted || m_reconnecting) {
        m_wanted = false;
        stopReconnecting();
        emit statusChanged();
    }
    runChecked({QStringLiteral("logout")}, [this, done](const QString &, const Error &error) {
        refreshStatus([done, error](const Error &) { done(error); });
    });
}

bool VpnService::isValidExclusion(const QString &item)
{
    // Domains (optionally "*." or "www."), IPv4/IPv6 with optional port or /prefix, IDN letters.
    static const QRegularExpression pattern(QStringLiteral("^[\\p{L}\\p{N}*\\[][\\p{L}\\p{N}.*:/\\[\\]_-]{0,299}$"));
    return pattern.match(item).hasMatch();
}

bool VpnService::isValidConfig(const QString &key, const QString &value)
{
    static const QStringList onOff{
        QStringLiteral("change-system-dns"),        QStringLiteral("post-quantum"),
        QStringLiteral("crash-reporting"),          QStringLiteral("telemetry"),
        QStringLiteral("debug-logging"),            QStringLiteral("exclusions-tcp-early-ack"),
        QStringLiteral("exclusions-preresolve"),
    };
    static const QRegularExpression ports(QStringLiteral("^\\d{1,5}(,\\d{1,5})*$"));
    static const QRegularExpression iface(QStringLiteral("^[A-Za-z0-9_.:][A-Za-z0-9_.:-]{0,14}$"));

    if (onOff.contains(key))
        return isOnOff(value);
    if (key == QLatin1String("mode"))
        return value == QLatin1String("tun") || value == QLatin1String("socks");
    if (key == QLatin1String("protocol"))
        return value == QLatin1String("auto") || value == QLatin1String("http2")
            || value == QLatin1String("quic");
    if (key == QLatin1String("update-channel"))
        return value == QLatin1String("release") || value == QLatin1String("beta")
            || value == QLatin1String("nightly");
    if (key == QLatin1String("socks-port"))
        return isUnsigned(value, 65535) && value.toUInt() > 0;
    if (key == QLatin1String("exclusions-preresolve-max-queries"))
        return isUnsigned(value, 100'000);
    if (key == QLatin1String("exclusions-scannable-ports"))
        return value.isEmpty() || ports.match(value).hasMatch();
    if (key == QLatin1String("bound-if-override"))
        return value.isEmpty() || iface.match(value).hasMatch();
    if (key == QLatin1String("dns") || key == QLatin1String("socks-host"))
        return isPlainToken(value);

    // tun-routing-mode and tun-device-name are owned by the daemon's routing engine.
    return false;
}

void VpnService::setStatus(State state, const QString &location, const QString &mode,
                           const QString &tunDevice)
{
    if (m_state == state && m_location == location && m_mode == mode && m_tunDevice == tunDevice)
        return;
    m_state = state;
    m_location = location;
    m_mode = mode;
    m_tunDevice = tunDevice;
    trackConnection();
    emit statusChanged();
}

void VpnService::refreshCliInfo()
{
    m_cli->run({QStringLiteral("--version")}, [this](const CliRunner::Result &result) {
        static const QRegularExpression version(QStringLiteral("v?(\\d+\\.\\d+\\.\\d+)"));
        const QString path = QFileInfo(m_cli->program()).canonicalFilePath();
        QString install;
        QString number;
        if (result.failedToStart || path.isEmpty()) {
            install = QStringLiteral("missing");
        } else {
            number = version.match(result.output).captured(1);
            if (path.startsWith(QLatin1String("/opt/adguardvpn_cli/")))
                install = QStringLiteral("script");
            else if (path.startsWith(QLatin1String("/usr/bin/")) || path.startsWith(QLatin1String("/usr/lib")))
                install = QStringLiteral("package");
            else
                install = QStringLiteral("other");
        }
        if (install != m_cliInstall || number != m_cliVersion) {
            m_cliInstall = install;
            m_cliVersion = number;
            emit statusChanged();
        }
    });
}

void VpnService::updateCli(Done done)
{
    if (m_cliInstall != QLatin1String("script")) {
        done({errors::NotSupported, QStringLiteral("This adguardvpn-cli was installed by a package; update it with the package manager")});
        return;
    }
    runChecked({QStringLiteral("update"), QStringLiteral("-y")}, [this, done](const QString &, const Error &error) {
        refreshCliInfo();
        done(error);
    }, 5 * 60'000);
}

void VpnService::trackConnection()
{
    switch (m_state) {
    case State::Connected:
        // However it got here: our connect, a reconnect, or already up when agvd started.
        m_wanted = true;
        if (!m_tunDevice.isEmpty())
            m_lastTunDevice = m_tunDevice;
        if (!m_mode.isEmpty())
            m_lastMode = m_mode;
        stopReconnecting();
        break;
    case State::NotLoggedIn:
        m_wanted = false;
        stopReconnecting();
        break;
    case State::Disconnected:
    case State::Error:
        // Dropped while it should be up: the network went away, the CLI exited, the server
        // closed the connection. Our own failed connects never set wanted, so they end here.
        if (m_wanted && !m_transitioning && !m_reconnecting) {
            m_reconnecting = true;
            scheduleReconnect();
        }
        break;
    default:
        break;
    }
}

void VpnService::scheduleReconnect()
{
    const int last = int(std::size(ReconnectDelaysMs)) - 1;
    m_reconnect->start(ReconnectDelaysMs[std::min(m_reconnectAttempts, last)]);
}

void VpnService::reconnectNow()
{
    if (!m_reconnecting)
        return;
    if (m_transitioning) {
        scheduleReconnect();
        return;
    }
    ++m_reconnectAttempts;
    // No location: the CLI goes back to the last one used.
    connectWith({QStringLiteral("connect"), QStringLiteral("-y"), QStringLiteral("--no-progress")},
                [this](const Error &error) {
        if (m_disconnectAfterAttempt) {
            m_disconnectAfterAttempt = false;
            m_wanted = false;
            stopReconnecting();
            if (m_state == State::Connected)
                disconnectVpn([](const Error &) {});
            else
                emit statusChanged();
            return;
        }
        if (error.isOk() || m_state == State::Connected || !m_reconnecting)
            return;
        if (error.name == errors::NotLoggedIn) {
            m_wanted = false;
            stopReconnecting();
            emit statusChanged();
            return;
        }
        scheduleReconnect();
    });
}

void VpnService::stopReconnecting()
{
    m_reconnecting = false;
    m_reconnectAttempts = 0;
    m_reconnect->stop();
}

void VpnService::setLastError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    emit statusChanged();
}

void VpnService::refreshSiteExclusionMode()
{
    m_cli->run({QStringLiteral("site-exclusions"), QStringLiteral("mode")},
               [this](const CliRunner::Result &result) {
        const auto mode = cli::parseExclusionMode(result.output);
        if (!mode)
            return;
        const QString name = *mode == cli::ExclusionMode::General ? QStringLiteral("general")
                                                                  : QStringLiteral("selective");
        if (m_siteExclusionMode != name) {
            m_siteExclusionMode = name;
            emit siteExclusionsChanged();
        }
    });
}

void VpnService::schedulePoll()
{
    m_poll->start(PollIntervalMs);
}

Error VpnService::errorFor(const QStringList &args, int exitCode, const QString &output,
                           bool timedOut, bool failedToStart) const
{
    const QString command = QStringLiteral("adguardvpn-cli ") + args.join(QLatin1Char(' '));
    if (failedToStart)
        return {errors::CliNotFound, QStringLiteral("Cannot run %1: %2").arg(m_cli->program(), output)};
    if (timedOut)
        return {errors::Timeout, QStringLiteral("%1 timed out").arg(command)};
    if (cli::isNotLoggedIn(exitCode, output))
        return {errors::NotLoggedIn, QStringLiteral("Not logged in to AdGuard VPN")};
    if (exitCode != 0 || cli::isUsageError(output)) {
        const QString line = lastMeaningfulLine(output);
        return {errors::CliFailed, line.isEmpty()
                    ? QStringLiteral("%1 failed with code %2").arg(command).arg(exitCode)
                    : line};
    }
    return {};
}

void VpnService::runChecked(const QStringList &args,
                            std::function<void(const QString &, const Error &)> then, int timeoutMs)
{
    m_cli->run(args, [this, args, then = std::move(then)](const CliRunner::Result &result) {
        then(result.output, errorFor(args, result.exitCode, result.output, result.timedOut,
                                     result.failedToStart));
    }, timeoutMs > 0 ? timeoutMs : CliRunner::DefaultTimeoutMs);
}

} // namespace agv
