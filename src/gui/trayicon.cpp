#include "trayicon.h"

#include "daemonclient.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QSystemTrayIcon>

namespace agv {

namespace {

// AdGuard VPN palette (adguard-vpn.com: --vpngreen400, --vpngreen700, --orange-50).
const QColor Green(0x74, 0xa3, 0x52);
const QColor Orange(0xff, 0x95, 0x00);
const QColor Gray(0x8f, 0x90, 0xa3);

QPixmap shield(const QColor &color, bool filled, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 24.0, size / 24.0);

    QPainterPath path;
    path.moveTo(12, 2);
    path.lineTo(20, 5);
    path.lineTo(20, 11);
    path.cubicTo(20, 16.5, 16.5, 20.5, 12, 22);
    path.cubicTo(7.5, 20.5, 4, 16.5, 4, 11);
    path.lineTo(4, 5);
    path.closeSubpath();

    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(filled ? color : Qt::transparent);
    painter.drawPath(path);

    if (filled) {
        // Check mark when protected.
        painter.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath check;
        check.moveTo(8.5, 12);
        check.lineTo(11, 14.5);
        check.lineTo(15.5, 9.5);
        painter.drawPath(check);
    }
    return pixmap;
}

QIcon shieldIcon(const QColor &color, bool filled)
{
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64})
        icon.addPixmap(shield(color, filled, size));
    return icon;
}

} // namespace

TrayIcon::TrayIcon(DaemonClient *daemon, QObject *parent)
    : QObject(parent)
    , m_daemon(daemon)
    , m_tray(new QSystemTrayIcon(this))
    , m_menu(new QMenu)
{
    m_status = m_menu->addAction(QString());
    m_status->setEnabled(false);
    m_toggle = m_menu->addAction(QString(), m_daemon, &DaemonClient::toggle);
    m_menu->addSeparator();
    m_menu->addAction(tr("Open AdGuard VPN"), this, &TrayIcon::showWindowRequested);
    m_menu->addAction(tr("Quit"), this, &TrayIcon::quitRequested);
    m_tray->setContextMenu(m_menu);

    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit showWindowRequested();
        else if (reason == QSystemTrayIcon::MiddleClick)
            m_daemon->toggle();
    });
    connect(m_daemon, &DaemonClient::statusChanged, this, &TrayIcon::update);
    connect(m_daemon, &DaemonClient::availableChanged, this, &TrayIcon::update);
    connect(m_daemon, &DaemonClient::busyChanged, this, &TrayIcon::update);

    update();
    m_tray->show();
}

TrayIcon::~TrayIcon()
{
    delete m_menu;
}

bool TrayIcon::isAvailable()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayIcon::update()
{
    QString status;
    QColor color = Gray;
    bool filled = false;

    if (!m_daemon->available()) {
        status = tr("agvd service is not running");
    } else if (m_daemon->busy() || m_daemon->transitioning()) {
        status = m_daemon->state() == QLatin1String("disconnecting") ? tr("Disconnecting…") : tr("Connecting…");
        color = Orange;
    } else if (m_daemon->connected()) {
        status = tr("Connected: %1").arg(m_daemon->location());
        color = Green;
        filled = true;
    } else if (!m_daemon->loggedIn()) {
        status = tr("Not signed in");
    } else {
        status = tr("Disconnected");
    }

    m_status->setText(status);
    m_toggle->setText(m_daemon->connected() ? tr("Disconnect") : tr("Connect"));
    m_toggle->setEnabled(m_daemon->available() && m_daemon->loggedIn() && !m_daemon->busy()
                         && !m_daemon->transitioning());
    m_tray->setIcon(shieldIcon(color, filled));
    m_tray->setToolTip(QStringLiteral("AdGuard VPN — %1").arg(status));
}

} // namespace agv
