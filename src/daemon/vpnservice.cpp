#include "vpnservice.h"

#include "clirunner.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
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
constexpr int ExternalStopTimeoutMs = 10'000;

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
{
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
    refreshStatus([this](const Error &) { refreshSiteExclusionMode(); });
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
    for (const QString &item : items) {
        if (!isValidExclusion(item)) {
            done({errors::InvalidArgument, QStringLiteral("Invalid exclusion: %1").arg(item)});
            return;
        }
    }
    if (items.isEmpty()) {
        done({});
        return;
    }

    runChecked(QStringList{QStringLiteral("site-exclusions"), QStringLiteral("add")} + items,
               [this, done](const QString &, const Error &error) {
        if (error.isOk())
            emit siteExclusionsChanged();
        done(error);
    });
}

void VpnService::removeSiteExclusions(const QStringList &items, Done done)
{
    for (const QString &item : items) {
        if (!isValidExclusion(item)) {
            done({errors::InvalidArgument, QStringLiteral("Invalid exclusion: %1").arg(item)});
            return;
        }
    }
    if (items.isEmpty()) {
        done({});
        return;
    }

    runChecked(QStringList{QStringLiteral("site-exclusions"), QStringLiteral("remove")} + items,
               [this, done](const QString &, const Error &error) {
        if (error.isOk())
            emit siteExclusionsChanged();
        done(error);
    });
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
    runChecked({QStringLiteral("logout")}, [this, done](const QString &, const Error &error) {
        refreshStatus([done, error](const Error &) { done(error); });
    });
}

bool VpnService::isValidExclusion(const QString &item)
{
    // Domains (optionally "*." or "www."), IPv4/IPv6 with optional port, IDN letters allowed.
    static const QRegularExpression pattern(QStringLiteral("^[\\p{L}\\p{N}*\\[][\\p{L}\\p{N}.*:\\[\\]_-]{0,299}$"));
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
    emit statusChanged();
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
