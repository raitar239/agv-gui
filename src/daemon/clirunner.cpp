#include "clirunner.h"

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

namespace agv {

namespace {

QProcessEnvironment cliEnvironment(const QString &home)
{
    // Start from an empty environment so the daemon's own variables (and anything a caller
    // could influence) never reach the CLI.
    QProcessEnvironment env;
    env.insert(QStringLiteral("HOME"), home);
    env.insert(QStringLiteral("PATH"),
               QStringLiteral("/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"));
    env.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    return env;
}

} // namespace

CliRunner::CliRunner(QString program, QString home, QObject *parent)
    : QObject(parent)
    , m_program(std::move(program))
    , m_home(std::move(home))
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (!m_process)
            return;
        m_timedOut = true;
        m_process->kill();
    });
}

CliRunner::~CliRunner()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void CliRunner::run(const QStringList &args, Callback callback, int timeoutMs)
{
    m_queue.enqueue({args, std::move(callback), timeoutMs});
    if (!m_process)
        startNext();
}

QProcess *CliRunner::createProcess(const QStringList &args, QObject *parent) const
{
    auto *process = new QProcess(parent);
    process->setProgram(m_program);
    process->setArguments(args);
    process->setProcessEnvironment(cliEnvironment(m_home));
    process->setWorkingDirectory(m_home);
    process->setProcessChannelMode(QProcess::MergedChannels);
    return process;
}

QString CliRunner::findProgram()
{
    const QString name = QStringLiteral("adguardvpn-cli");
    for (const QString &dir : {QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/bin"),
                               QStringLiteral("/opt/adguardvpn_cli")}) {
        const QFileInfo candidate(dir + QLatin1Char('/') + name);
        if (candidate.isExecutable())
            return candidate.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(name);
}

void CliRunner::startNext()
{
    if (m_queue.isEmpty())
        return;

    const Job &job = m_queue.head();
    if (m_findProgram && !QFileInfo(m_program).isExecutable())
        m_program = findProgram();
    m_timedOut = false;
    m_process = createProcess(job.args, this);

    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        Result result;
        result.exitCode = status == QProcess::NormalExit ? exitCode : -1;
        result.output = QString::fromUtf8(m_process->readAll());
        result.timedOut = m_timedOut;
        finish(std::move(result));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return; // crashes and timeouts are reported through finished()
        Result result;
        result.failedToStart = true;
        result.output = m_process->errorString();
        finish(std::move(result));
    });

    m_timer->start(job.timeoutMs);
    m_process->start();
    m_process->closeWriteChannel(); // never wait for answers on stdin
}

void CliRunner::finish(Result result)
{
    m_timer->stop();
    m_process->disconnect(this);
    m_process->deleteLater();
    m_process = nullptr;

    const Job job = m_queue.dequeue();
    if (job.callback)
        job.callback(result);

    // The callback may have queued a command, which run() already started.
    if (!m_process)
        startNext();
}

} // namespace agv
