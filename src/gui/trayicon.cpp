#include "trayicon.h"

#include "appmark.h"

#include "daemonclient.h"
#include "locationmodel.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

namespace agv {

namespace {

// AdGuard VPN palette (adguard-vpn.com: --vpngreen400, --vpngreen700, --orange-50).
const QColor Green(0x74, 0xa3, 0x52);
const QColor Orange(0xff, 0x95, 0x00);
const QColor Gray(0x8f, 0x90, 0xa3);

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
    m_open = m_menu->addAction(QString(), this, &TrayIcon::showWindowRequested);
    m_quit = m_menu->addAction(QString(), this, &TrayIcon::quitRequested);
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

    retranslate();
    m_tray->show();
}

void TrayIcon::retranslate()
{
    m_open->setText(tr("Open AGV"));
    m_quit->setText(tr("Quit"));
    update();
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

    if (!m_daemon->available()) {
        status = tr("agvd service is not running");
    } else if (m_daemon->reconnecting()) {
        status = tr("Reconnecting…");
        color = Orange;
    } else if (m_daemon->busy() || m_daemon->transitioning()) {
        status = m_daemon->state() == QLatin1String("disconnecting") ? tr("Disconnecting…") : tr("Connecting…");
        color = Orange;
    } else if (m_daemon->connected()) {
        // "ATHENS" from the CLI → "Greece, Athens" (translated where names are known).
        const LocationModel *locations = m_daemon->locationModel();
        const QString city = locations->cityName(m_daemon->location());
        const QString country = locations->countryOf(m_daemon->location());
        status = tr("Connected: %1").arg(country.isEmpty() ? city : country + QStringLiteral(", ") + city);
        color = Green;
    } else if (!m_daemon->loggedIn()) {
        status = tr("Not signed in");
    } else {
        status = tr("Disconnected");
    }

    m_status->setText(status);
    m_toggle->setText(m_daemon->connected() ? tr("Disconnect") : tr("Connect"));
    m_toggle->setEnabled(m_daemon->available() && m_daemon->loggedIn() && !m_daemon->busy()
                         && !m_daemon->transitioning());
    m_tray->setIcon(appMarkIcon(color));
    m_tray->setToolTip(QStringLiteral("AGV — %1").arg(status));
}

} // namespace agv
