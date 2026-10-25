#pragma once

#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace agv {

class DaemonClient;

// The tray icon and its menu; stays alive while the window is closed. The icon is painted in code
// (a shield whose color follows the connection) so there are no image assets to theme.
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    TrayIcon(DaemonClient *daemon, QObject *parent = nullptr);
    ~TrayIcon() override;

    static bool isAvailable();

signals:
    void showWindowRequested();
    void quitRequested();

private:
    void update();

    DaemonClient *m_daemon;
    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    QAction *m_status;
    QAction *m_toggle;
};

} // namespace agv
