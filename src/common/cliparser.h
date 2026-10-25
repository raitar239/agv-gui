#pragma once

// Parsers for the text output of adguardvpn-cli.
//
// The CLI has no machine-readable output, so everything here works on human-oriented text.
// All format knowledge lives in this file and is covered by tests on captured samples
// (tests/fixtures/cli/<version>/), so a CLI update breaks one place, not the whole app.
//
// Every parser is a pure function: it takes the raw output (ANSI codes included) and
// returns std::nullopt when the text does not look like what it expects.

#include <QDate>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace agv::cli {

// Removes ANSI escape sequences. The CLI emits them even when stdout is not a TTY.
QString stripAnsi(const QString &text);

// Exit codes observed in CLI 1.8.1.
constexpr int ExitNotLoggedIn = 11;
constexpr int ExitUsageError = 106;

// True for "A subcommand is required" and similar usage errors followed by a help dump.
bool isUsageError(const QString &output);

// True when a command failed because there is no logged-in account.
bool isNotLoggedIn(int exitCode, const QString &output);

// adguardvpn-cli login prints a device-code URL and waits until it is confirmed in a browser.
// Returns the URL, or an empty string when the output does not contain it (yet).
QString parseLoginUrl(const QString &output);

struct Status {
    enum class State { Disconnected, Connected, NotLoggedIn };

    State state = State::Disconnected;
    QString location;   // "ATHENS"; empty when disconnected
    QString mode;       // "TUN" or "SOCKS"
    QString tunDevice;  // "tun0"; empty in SOCKS mode
};

// adguardvpn-cli status
std::optional<Status> parseStatus(const QString &output);

struct Location {
    QString iso;              // "FR"
    QString country;          // "France"
    QString city;             // "Marseille"
    std::optional<int> ping;  // ms; nullopt when the CLI shows no estimate
};

// adguardvpn-cli list-locations [count]
std::optional<QList<Location>> parseLocations(const QString &output);

struct ConfigValue {
    QString value;           // "on", "1080", "" for "not set"
    bool isDefault = false;  // true for "Default (...)" and "not set"
};

// adguardvpn-cli config show; keys are the labels as printed ("Tunnel routing mode").
std::optional<QMap<QString, ConfigValue>> parseConfig(const QString &output);

enum class ExclusionMode { General, Selective };

// adguardvpn-cli site-exclusions mode
std::optional<ExclusionMode> parseExclusionMode(const QString &output);

struct SiteExclusions {
    ExclusionMode mode = ExclusionMode::General;
    QStringList domains;
};

// adguardvpn-cli site-exclusions show
std::optional<SiteExclusions> parseSiteExclusions(const QString &output);

struct License {
    QString account;                // email
    QString plan;                   // "PREMIUM", "FREE"
    std::optional<int> maxDevices;
    std::optional<QDate> validUntil;
};

// adguardvpn-cli license
std::optional<License> parseLicense(const QString &output);

} // namespace agv::cli
