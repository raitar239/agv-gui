#pragma once

#include <QJsonArray>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class QNetworkAccessManager;

namespace agv {

// Checks on request what the internet sees: the public IP and the DNS servers that resolve this
// computer's lookups. Uses the bash.ws DNS leak test: fetch an id, resolve a few names under it
// through the system resolver, then ask which servers asked for them. Nothing runs on its own.
class LeakCheck : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    // { ip, country, org, dns: [{ ip, country, org }], verdict: "ok" | "leak" | "error", error }
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)

public:
    explicit LeakCheck(QObject *parent = nullptr);

    bool running() const { return m_running; }
    QVariantMap result() const { return m_result; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void clear();

    // Turns the service's answer into result(); public for tests.
    static QVariantMap evaluate(const QJsonArray &answer);

signals:
    void changed();

private:
    void resolveNames(const QString &id);
    void fetchReport(const QString &id);
    void finish(const QVariantMap &result);
    void fail(const QString &message);

    QNetworkAccessManager *m_network = nullptr;
    bool m_running = false;
    int m_pendingLookups = 0;
    QVariantMap m_result;
};

} // namespace agv
