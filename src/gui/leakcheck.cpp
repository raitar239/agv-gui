#include "leakcheck.h"

#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>

namespace agv {

namespace {

const QString Service = QStringLiteral("https://bash.ws");
constexpr int Lookups = 6;
constexpr int TimeoutMs = 15'000;

QVariantMap server(const QJsonObject &entry)
{
    return {{QStringLiteral("ip"), entry.value(QStringLiteral("ip")).toString()},
            {QStringLiteral("country"), entry.value(QStringLiteral("country_name")).toString()},
            {QStringLiteral("code"), entry.value(QStringLiteral("country")).toString().toUpper()},
            {QStringLiteral("org"), entry.value(QStringLiteral("org")).toString()},
            {QStringLiteral("asn"), entry.value(QStringLiteral("asn")).toString().section(QLatin1Char(' '), 0, 0)}};
}

} // namespace

LeakCheck::LeakCheck(QObject *parent)
    : QObject(parent)
{
}

void LeakCheck::start()
{
    if (m_running)
        return;
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        m_network->setTransferTimeout(TimeoutMs);
    }
    m_running = true;
    m_result.clear();
    emit changed();

    QNetworkReply *reply = m_network->get(QNetworkRequest(QUrl(Service + QStringLiteral("/id"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QString id = QString::fromUtf8(reply->readAll()).trimmed();
        static const QRegularExpression valid(QStringLiteral("^[0-9a-z]{4,40}$"));
        if (reply->error() != QNetworkReply::NoError || !valid.match(id).hasMatch()) {
            fail(tr("The check service is not reachable: %1").arg(reply->errorString()));
            return;
        }
        resolveNames(id);
    });
}

void LeakCheck::clear()
{
    if (m_running)
        return;
    m_result.clear();
    emit changed();
}

void LeakCheck::resolveNames(const QString &id)
{
    // Each lookup reaches the service's name server through whatever resolver the system uses.
    m_pendingLookups = Lookups;
    for (int i = 1; i <= Lookups; ++i) {
        QHostInfo::lookupHost(QStringLiteral("%1.%2.bash.ws").arg(i).arg(id), this, [this, id](const QHostInfo &) {
            if (--m_pendingLookups == 0)
                fetchReport(id);
        });
    }
}

void LeakCheck::fetchReport(const QString &id)
{
    QNetworkReply *reply = m_network->get(
        QNetworkRequest(QUrl(Service + QStringLiteral("/dnsleak/test/%1?json").arg(id))));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError || !document.isArray()) {
            fail(tr("The check service did not answer: %1").arg(reply->errorString()));
            return;
        }
        finish(evaluate(document.array()));
    });
}

QVariantMap LeakCheck::evaluate(const QJsonArray &answer)
{
    QVariantMap result;
    QVariantList dns;
    QVariantMap ip;
    for (const QJsonValue &value : answer) {
        const QJsonObject entry = value.toObject();
        const QString type = entry.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("ip") && ip.isEmpty())
            ip = server(entry);
        else if (type == QLatin1String("dns"))
            dns.append(server(entry));
    }
    if (ip.isEmpty() || dns.isEmpty()) {
        result.insert(QStringLiteral("verdict"), QStringLiteral("error"));
        result.insert(QStringLiteral("error"), tr("The check did not see this computer's traffic. Try again."));
        return result;
    }

    // DNS stays private when it is answered by AdGuard or from inside the VPN provider's network
    // (the exit server's own network or country); a resolver elsewhere, typically the internet
    // provider's, sees every site visited.
    bool leak = false;
    for (const QVariant &entry : std::as_const(dns)) {
        const QVariantMap server = entry.toMap();
        const bool adguard = server.value(QStringLiteral("org")).toString().contains(QLatin1String("adguard"), Qt::CaseInsensitive);
        const bool sameNetwork = server.value(QStringLiteral("asn")) == ip.value(QStringLiteral("asn"));
        const bool sameCountry = server.value(QStringLiteral("code")) == ip.value(QStringLiteral("code"));
        if (!adguard && !sameNetwork && !sameCountry)
            leak = true;
    }
    result = ip;
    result.insert(QStringLiteral("dns"), dns);
    result.insert(QStringLiteral("verdict"), leak ? QStringLiteral("leak") : QStringLiteral("ok"));
    return result;
}

void LeakCheck::finish(const QVariantMap &result)
{
    m_running = false;
    m_result = result;
    emit changed();
}

void LeakCheck::fail(const QString &message)
{
    finish({{QStringLiteral("verdict"), QStringLiteral("error")}, {QStringLiteral("error"), message}});
}

} // namespace agv
