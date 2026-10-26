#pragma once

#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>

#include <functional>

class QProcess;
class QTimer;

namespace agv {

// Runs adguardvpn-cli with a fixed data directory and a clean environment.
//
// The CLI keeps its state in $HOME/.local/share/adguardvpn-cli, so every call must use the same
// HOME, or "status" from one call will not see a VPN started by another. Commands are executed
// one at a time: the CLI was not designed for concurrent invocations on one data directory.
class CliRunner : public QObject
{
    Q_OBJECT

public:
    struct Result {
        int exitCode = -1;
        QString output;  // stdout and stderr merged, ANSI codes kept
        bool timedOut = false;
        bool failedToStart = false;

        bool ok() const { return !timedOut && !failedToStart && exitCode == 0; }
    };
    using Callback = std::function<void(const Result &)>;

    static constexpr int DefaultTimeoutMs = 15'000;

    CliRunner(QString program, QString home, QObject *parent = nullptr);
    ~CliRunner() override;

    QString program() const { return m_program; }
    QString home() const { return m_home; }

    // Queues a command; the callback runs on the event loop once it finishes.
    void run(const QStringList &args, Callback callback, int timeoutMs = DefaultTimeoutMs);

    // Creates a process for long-running interactive commands (login) that bypass the queue.
    // The caller owns the process and starts it.
    QProcess *createProcess(const QStringList &args, QObject *parent) const;

    // Looks for adguardvpn-cli in the usual install locations, then in PATH.
    static QString findProgram();
    // Looks again before each command while the program is missing, so installing the CLI later
    // needs no restart. Off when the path was given explicitly.
    void setFindProgram(bool on) { m_findProgram = on; }

private:
    struct Job {
        QStringList args;
        Callback callback;
        int timeoutMs;
    };

    void startNext();
    void finish(Result result);

    QString m_program;
    bool m_findProgram = false;
    QString m_home;
    QQueue<Job> m_queue;
    QProcess *m_process = nullptr;
    QTimer *m_timer = nullptr;
    bool m_timedOut = false;
};

} // namespace agv
