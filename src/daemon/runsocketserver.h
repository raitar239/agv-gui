#pragma once

#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

namespace agv {

class AppExclusionService;

// The unix socket agv-run talks to (/run/agvd/run.sock).
//
// Protocol, one exchange per connection:
//   client → "RUN [app-id]\n"
//   daemon → "OK\n" once the client's process is in the app's cgroup, or "ERR <reason>\n"
// The daemon identifies the client by the socket's peer credentials (pidfd where the kernel
// has SO_PEERPIDFD, else SO_PEERCRED), so a client can only move itself. agv-run then execs the
// command, which keeps the pid and therefore the cgroup.
class RunSocketServer : public QObject
{
    Q_OBJECT

public:
    RunSocketServer(AppExclusionService *apps, QString path, QObject *parent = nullptr);
    ~RunSocketServer() override;

    bool listen();
    QString errorString() const { return m_error; }

private:
    void handle(QLocalSocket *socket);
    void respond(QLocalSocket *socket, const QByteArray &line);

    AppExclusionService *m_apps;
    QString m_path;
    QLocalServer *m_server = nullptr;
    QString m_error;
};

} // namespace agv
