#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QTimer;

namespace agv {

// The newest adguardvpn-cli release (GitHub), looked up at most once a day while AGV runs and
// remembered between starts. The installed version comes from agvd.
class CliUpdates : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString latest READ latest NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)

public:
    explicit CliUpdates(QObject *parent = nullptr);

    QString latest() const { return m_latest; }
    bool checking() const { return m_checking; }

    Q_INVOKABLE void check();
    // True when version b is newer than a ("1.8.1" < "1.10.0").
    Q_INVOKABLE static bool isNewer(const QString &a, const QString &b);
    // "v1.9.0-release" → "1.9.0"; empty for anything else.
    static QString versionFromTag(const QString &tag);

signals:
    void changed();

private:
    void checkIfDue();

    QNetworkAccessManager *m_network = nullptr;
    QTimer *m_timer;
    QString m_latest;
    bool m_checking = false;
};

} // namespace agv
