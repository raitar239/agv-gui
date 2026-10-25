#pragma once

#include <QObject>

#include <sys/types.h>

class QSocketNotifier;

namespace agv {

// Kernel notifications about every exec() through the netlink process connector (CN_PROC).
// Needs CAP_NET_ADMIN, i.e. root.
class ExecWatcher : public QObject
{
    Q_OBJECT

public:
    explicit ExecWatcher(QObject *parent = nullptr);
    ~ExecWatcher() override;

    bool start();
    QString errorString() const { return m_error; }

signals:
    void executed(pid_t pid);
    // Events were dropped (the socket buffer overflowed); callers should rescan processes.
    void overflowed();

private:
    void readEvents();

    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QString m_error;
};

} // namespace agv
