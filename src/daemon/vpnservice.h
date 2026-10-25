#pragma once

#include "cliparser.h"
#include "daemonerror.h"
#include "hostsystem.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

class QProcess;
class QTimer;

namespace agv {

class CliRunner;

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
    // VPN instances started outside the daemon; only looked for while we are not connected.
    QList<ExternalVpn> externalVpns() const { return m_externalVpns; }

    // For tests: where to look for processes (default /proc).
    void setProcDir(const QString &procDir) { m_procDir = procDir; }

    // Reads the initial state, starts polling, and connects if auto-connect is on.
    void start();

    // Connect to the last used location when the daemon starts, retrying while the network comes up.
    bool autoConnect() const { return m_autoConnect; }
    void setAutoConnect(bool on);
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

    // Copies the CLI login and settings of another HOME (the calling user's) into ours.
    void importLogin(const QString &fromHome, uid_t ownerUid, Done done);
    // Asks external VPN instances to stop (SIGTERM) and waits until they are gone.
    void stopExternalVpns(Done done);

    // Validation helpers, public for tests.
    static bool isValidExclusion(const QString &item);
    static bool isValidConfig(const QString &key, const QString &value);

signals:
    // State, Location, Mode, TunDevice, LastError, LoggedIn or the external VPN list changed.
    void statusChanged();
    void siteExclusionsChanged();
    void loginUrlReady(const QString &url);
    void loginFinished(bool success, const QString &message);

private:
    void connectWith(const QStringList &args, Done done);
    void setStatus(State state, const QString &location = {}, const QString &mode = {},
                   const QString &tunDevice = {});
    void setLastError(const QString &message);
    void updateExternalVpns();
    void refreshSiteExclusionMode();
    void schedulePoll();
    void tryAutoConnect();
    Error errorFor(const QStringList &args, int exitCode, const QString &output, bool timedOut,
                   bool failedToStart) const;
    void runChecked(const QStringList &args, std::function<void(const QString &, const Error &)> then,
                    int timeoutMs = 0);

    CliRunner *m_cli;
    QTimer *m_poll;
    QTimer *m_autoConnectRetry;
    bool m_autoConnect = false;
    int m_autoConnectAttempts = 0;
    QPointer<QProcess> m_login;
    bool m_transitioning = false;

    State m_state = State::Unknown;
    QString m_location;
    QString m_mode;
    QString m_tunDevice;
    QString m_lastError;
    QString m_siteExclusionMode;
    QString m_procDir = QStringLiteral("/proc");
    QList<ExternalVpn> m_externalVpns;
};

} // namespace agv
