#pragma once

#include "cliparser.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

class QProcess;
class QTimer;

namespace agv {

class CliRunner;

// Error names returned over D-Bus. An empty name means success.
namespace errors {
inline const QString NotLoggedIn = QStringLiteral("org.agv.Error.NotLoggedIn");
inline const QString CliFailed = QStringLiteral("org.agv.Error.CliFailed");
inline const QString CliNotFound = QStringLiteral("org.agv.Error.CliNotFound");
inline const QString Timeout = QStringLiteral("org.agv.Error.Timeout");
inline const QString UnexpectedOutput = QStringLiteral("org.agv.Error.UnexpectedOutput");
inline const QString InvalidArgument = QStringLiteral("org.agv.Error.InvalidArgument");
inline const QString Busy = QStringLiteral("org.agv.Error.Busy");
inline const QString NotAuthorized = QStringLiteral("org.agv.Error.NotAuthorized");
inline const QString NotImplemented = QStringLiteral("org.agv.Error.NotImplemented");
} // namespace errors

struct Error {
    QString name;
    QString message;

    bool isOk() const { return name.isEmpty(); }
};

// The daemon's model of the VPN: wraps CLI commands, keeps the last known state and polls the
// CLI to notice changes made outside the daemon. Knows nothing about D-Bus.
class VpnService : public QObject
{
    Q_OBJECT

public:
    enum class State { Unknown, NotLoggedIn, Disconnected, Connecting, Connected, Disconnecting, Error };

    using Done = std::function<void(const Error &)>;
    template<class T>
    using Reply = std::function<void(const T &, const Error &)>;

    explicit VpnService(CliRunner *cli, QObject *parent = nullptr);

    State state() const { return m_state; }
    QString stateName() const;
    QString location() const { return m_location; }
    QString mode() const { return m_mode; }
    QString tunDevice() const { return m_tunDevice; }
    QString lastError() const { return m_lastError; }
    bool loggedIn() const { return m_state != State::NotLoggedIn && m_state != State::Unknown; }
    QString siteExclusionMode() const { return m_siteExclusionMode; }

    // Reads the initial state and starts polling.
    void start();
    void refreshStatus(Done done = {});

    void connectTo(const QString &location, Done done);
    void connectFastest(Done done);
    void disconnectVpn(Done done);

    void listLocations(Reply<QList<cli::Location>> reply);
    void license(Reply<cli::License> reply);

    void siteExclusions(Reply<QStringList> reply);
    void addSiteExclusions(const QStringList &items, Done done);
    void removeSiteExclusions(const QStringList &items, Done done);
    void setSiteExclusionMode(const QString &mode, Done done);

    void config(Reply<QMap<QString, cli::ConfigValue>> reply);
    // key is a set-* subcommand without the prefix; only a known safe subset is accepted.
    void setConfig(const QString &key, const QString &value, Done done);

    // Starts the device-code login; loginUrlReady() delivers the URL to open in a browser.
    void startLogin(Done done);
    void cancelLogin();
    void logout(Done done);

    // Validation helpers, public for tests.
    static bool isValidExclusion(const QString &item);
    static bool isValidConfig(const QString &key, const QString &value);

signals:
    // State, Location, Mode, TunDevice, LastError or LoggedIn changed.
    void statusChanged();
    void siteExclusionsChanged();
    void loginUrlReady(const QString &url);
    void loginFinished(bool success, const QString &message);

private:
    void connectWith(const QStringList &args, Done done);
    void setStatus(State state, const QString &location = {}, const QString &mode = {},
                   const QString &tunDevice = {});
    void setLastError(const QString &message);
    void refreshSiteExclusionMode();
    void schedulePoll();
    Error errorFor(const QStringList &args, int exitCode, const QString &output, bool timedOut,
                   bool failedToStart) const;
    void runChecked(const QStringList &args, std::function<void(const QString &, const Error &)> then,
                    int timeoutMs = 0);

    CliRunner *m_cli;
    QTimer *m_poll;
    QPointer<QProcess> m_login;
    bool m_transitioning = false;

    State m_state = State::Unknown;
    QString m_location;
    QString m_mode;
    QString m_tunDevice;
    QString m_lastError;
    QString m_siteExclusionMode;
};

} // namespace agv
