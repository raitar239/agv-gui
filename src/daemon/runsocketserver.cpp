#include "runsocketserver.h"

#include "appexclusionservice.h"

#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

#include <cerrno>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef SO_PEERPIDFD
#define SO_PEERPIDFD 77 // Linux 6.5
#endif

namespace agv {

namespace {

constexpr int RequestTimeoutMs = 5'000;
constexpr qint64 MaxRequestLength = 128;

// The client's pid, and a pidfd that pins it when the kernel supports SO_PEERPIDFD.
struct Peer {
    pid_t pid = -1;
    int pidfd = -1;

    ~Peer()
    {
        if (pidfd >= 0)
            ::close(pidfd);
    }
};

pid_t pidFromPidfd(int pidfd)
{
    QFile info(QStringLiteral("/proc/self/fdinfo/%1").arg(pidfd));
    if (!info.open(QIODevice::ReadOnly))
        return -1;
    for (const QByteArray &line : info.readAll().split('\n')) {
        if (line.startsWith("Pid:"))
            return pid_t(line.mid(4).trimmed().toInt());
    }
    return -1;
}

bool pidfdAlive(int pidfd)
{
    return ::syscall(SYS_pidfd_send_signal, pidfd, 0, nullptr, 0) == 0;
}

void readPeer(int fd, Peer &peer)
{
    socklen_t length = sizeof(peer.pidfd);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERPIDFD, &peer.pidfd, &length) == 0 && peer.pidfd >= 0) {
        peer.pid = pidFromPidfd(peer.pidfd);
        return;
    }
    peer.pidfd = -1;

    ucred credentials{};
    length = sizeof(credentials);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0)
        peer.pid = credentials.pid;
}

} // namespace

RunSocketServer::RunSocketServer(AppExclusionService *apps, QString path, QObject *parent)
    : QObject(parent)
    , m_apps(apps)
    , m_path(std::move(path))
{
}

RunSocketServer::~RunSocketServer()
{
    if (m_server)
        m_server->close();
}

bool RunSocketServer::listen()
{
    QLocalServer::removeServer(m_path);
    m_server = new QLocalServer(this);
    // Any local user may exclude their own processes, the same as with a VPN-off switch.
    m_server->setSocketOptions(QLocalServer::WorldAccessOption);
    if (!m_server->listen(m_path)) {
        m_error = m_server->errorString();
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection())
            handle(socket);
    });
    return true;
}

void RunSocketServer::handle(QLocalSocket *socket)
{
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    QTimer::singleShot(RequestTimeoutMs, socket, [socket] { socket->abort(); });

    connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
        if (!socket->canReadLine()) {
            if (socket->bytesAvailable() > MaxRequestLength)
                respond(socket, "ERR request too long\n");
            return;
        }

        const QByteArray line = socket->readLine(MaxRequestLength).trimmed();
        if (line != "RUN" && !line.startsWith("RUN ")) {
            respond(socket, "ERR unknown request\n");
            return;
        }
        const QString appId = QString::fromUtf8(line.mid(3)).trimmed();

        Peer peer;
        readPeer(int(socket->socketDescriptor()), peer);
        if (peer.pid <= 0 || (peer.pidfd >= 0 && !pidfdAlive(peer.pidfd))) {
            respond(socket, "ERR cannot identify the client process\n");
            return;
        }

        const Error error = m_apps->moveProcess(peer.pid, appId);
        if (!error.isOk()) {
            respond(socket, "ERR " + error.message.toUtf8() + '\n');
            return;
        }
        // With a pidfd we can tell whether the pid still belonged to the client after the move.
        if (peer.pidfd >= 0 && !pidfdAlive(peer.pidfd))
            qWarning("agvd: agv-run client %d exited during the move", peer.pid);
        respond(socket, "OK\n");
    });
}

void RunSocketServer::respond(QLocalSocket *socket, const QByteArray &line)
{
    socket->write(line);
    socket->flush();
    socket->disconnectFromServer();
}

} // namespace agv
