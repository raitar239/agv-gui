#include "execwatcher.h"

#include <QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <unistd.h>

namespace agv {

namespace {

constexpr int ReceiveBufferBytes = 1 << 20; // bursts of exec() during logins and builds
// PROC_EVENT_EXEC; spelled as a number because older kernel headers nest the enum inside
// struct proc_event and newer ones do not. The value is kernel ABI.
constexpr unsigned ExecEvent = 0x00000002;

bool subscribe(int fd, proc_cn_mcast_op op)
{
    // nlmsghdr | cn_msg | proc_cn_mcast_op, laid out by hand: cn_msg ends in a flexible array.
    alignas(nlmsghdr) char buffer[NLMSG_SPACE(sizeof(cn_msg) + sizeof(op))] = {};
    auto *header = reinterpret_cast<nlmsghdr *>(buffer);
    header->nlmsg_len = NLMSG_LENGTH(sizeof(cn_msg) + sizeof(op));
    header->nlmsg_type = NLMSG_DONE;
    auto *message = static_cast<cn_msg *>(NLMSG_DATA(header));
    message->id.idx = CN_IDX_PROC;
    message->id.val = CN_VAL_PROC;
    message->len = sizeof(op);
    std::memcpy(message->data, &op, sizeof(op));
    return ::send(fd, header, header->nlmsg_len, 0) == ssize_t(header->nlmsg_len);
}

} // namespace

ExecWatcher::ExecWatcher(QObject *parent)
    : QObject(parent)
{
}

ExecWatcher::~ExecWatcher()
{
    if (m_fd >= 0) {
        subscribe(m_fd, PROC_CN_MCAST_IGNORE);
        ::close(m_fd);
    }
}

bool ExecWatcher::start()
{
    m_fd = ::socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_CONNECTOR);
    if (m_fd < 0) {
        m_error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    ::setsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, &ReceiveBufferBytes, sizeof(ReceiveBufferBytes));

    sockaddr_nl address{};
    address.nl_family = AF_NETLINK;
    address.nl_groups = CN_IDX_PROC;
    address.nl_pid = 0; // let the kernel pick a unique port id
    if (::bind(m_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 ||
        !subscribe(m_fd, PROC_CN_MCAST_LISTEN)) {
        m_error = QString::fromLocal8Bit(std::strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &ExecWatcher::readEvents);
    return true;
}

void ExecWatcher::readEvents()
{
    alignas(nlmsghdr) char buffer[8192];
    for (;;) {
        const ssize_t length = ::recv(m_fd, buffer, sizeof(buffer), 0);
        if (length < 0) {
            if (errno == ENOBUFS)
                emit overflowed();
            if (errno == EINTR || errno == ENOBUFS)
                continue;
            return; // EAGAIN: drained
        }

        auto *header = reinterpret_cast<nlmsghdr *>(buffer);
        for (ssize_t left = length; NLMSG_OK(header, left); header = NLMSG_NEXT(header, left)) {
            if (header->nlmsg_type == NLMSG_ERROR || header->nlmsg_type == NLMSG_NOOP)
                continue;
            const auto *message = static_cast<const cn_msg *>(NLMSG_DATA(header));
            if (message->id.idx != CN_IDX_PROC || message->id.val != CN_VAL_PROC)
                continue;
            const auto *event = reinterpret_cast<const proc_event *>(message->data);
            if (unsigned(event->what) == ExecEvent)
                emit executed(event->event_data.exec.process_tgid);
        }
    }
}

} // namespace agv
