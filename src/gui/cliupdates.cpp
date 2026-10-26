#include "cliupdates.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QVersionNumber>

namespace agv {

namespace {

const QString LatestKey = QStringLiteral("cli/latest");
const QString CheckedKey = QStringLiteral("cli/checked");
constexpr qint64 CheckEverySeconds = 24 * 3600;

} // namespace

CliUpdates::CliUpdates(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_latest(QSettings().value(LatestKey).toString())
{
    // A little after start, then hourly to catch the day boundary on long sessions.
    m_timer->setInterval(3600 * 1000);
    connect(m_timer, &QTimer::timeout, this, &CliUpdates::checkIfDue);
    m_timer->start();
    QTimer::singleShot(30 * 1000, this, &CliUpdates::checkIfDue);
}

void CliUpdates::checkIfDue()
{
    const QDateTime last = QSettings().value(CheckedKey).toDateTime();
    if (!last.isValid() || last.secsTo(QDateTime::currentDateTimeUtc()) >= CheckEverySeconds)
        check();
}

void CliUpdates::check()
{
    if (m_checking)
        return;
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        m_network->setTransferTimeout(15'000);
    }
    m_checking = true;
    emit changed();

    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/AdguardTeam/AdGuardVPNCLI/releases/latest")));
    request.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_checking = false;
        const QString version = versionFromTag(
            QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("tag_name")).toString());
        if (reply->error() == QNetworkReply::NoError && !version.isEmpty()) {
            m_latest = version;
            QSettings settings;
            settings.setValue(LatestKey, version);
            settings.setValue(CheckedKey, QDateTime::currentDateTimeUtc());
        }
        emit changed();
    });
}

bool CliUpdates::isNewer(const QString &a, const QString &b)
{
    const QVersionNumber first = QVersionNumber::fromString(a);
    const QVersionNumber second = QVersionNumber::fromString(b);
    return !first.isNull() && !second.isNull() && second > first;
}

QString CliUpdates::versionFromTag(const QString &tag)
{
    static const QRegularExpression pattern(QStringLiteral("^v?(\\d+\\.\\d+\\.\\d+)(-release)?$"));
    return pattern.match(tag).captured(1);
}

} // namespace agv
