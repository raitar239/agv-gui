#include "polkitauthorizer.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusVirtualObject>
#include <QTest>

using namespace agv;

namespace {

const QString PolkitService = QStringLiteral("org.freedesktop.PolicyKit1");
const QString PolkitPath = QStringLiteral("/org/freedesktop/PolicyKit1/Authority");

// Answers CheckAuthorization like polkit does and remembers the last request.
class FakePolkit : public QDBusVirtualObject
{
public:
    bool authorize = true;
    bool challenge = false;
    QDBusMessage lastCall;
    QDBusConnection bus = QDBusConnection::sessionBus();

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.member() != QLatin1String("CheckAuthorization"))
            return false;
        lastCall = message;

        QDBusArgument result;
        result.beginStructure();
        result << authorize << challenge << QMap<QString, QString>();
        result.endStructure();
        connection.send(message.createReply(QVariant::fromValue(result)));
        return true;
    }
};

} // namespace

class TestPolkit : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_bus = QDBusConnection::sessionBus();
        if (!m_bus.isConnected())
            QSKIP("No session bus");
        if (!m_bus.registerVirtualObject(PolkitPath, &m_polkit))
            QSKIP("Cannot register the fake polkit object");
        if (!m_bus.registerService(PolkitService))
            QSKIP("org.freedesktop.PolicyKit1 is taken on the session bus");
    }

    void cleanupTestCase()
    {
        m_bus.unregisterService(PolkitService);
        m_bus.unregisterObject(PolkitPath);
    }

    void sendsPolkitRequestAndReadsAnswer()
    {
        PolkitAuthorizer authorizer(m_bus, true);
        m_polkit.authorize = true;
        m_polkit.challenge = false;

        std::optional<bool> authorized;
        authorizer.check(QStringLiteral(":1.42"), ActionControl, true,
                         [&](const PolkitAuthorizer::Result &r) { authorized = r.authorized; });
        QTRY_VERIFY(authorized.has_value());
        QVERIFY(*authorized);

        // CheckAuthorization(subject (sa{sv}), action_id s, details a{ss}, flags u, cancellation_id s)
        QCOMPARE(m_polkit.lastCall.signature(), QStringLiteral("(sa{sv})sa{ss}us"));
        const QList<QVariant> args = m_polkit.lastCall.arguments();
        QCOMPARE(args.at(1).toString(), ActionControl);
        QCOMPARE(args.at(3).toUInt(), 1u); // AllowUserInteraction

        QString kind;
        QVariantMap details;
        const auto subject = args.at(0).value<QDBusArgument>();
        subject.beginStructure();
        subject >> kind >> details;
        subject.endStructure();
        QCOMPARE(kind, QStringLiteral("system-bus-name"));
        QCOMPARE(details.value(QStringLiteral("name")).toString(), QStringLiteral(":1.42"));
    }

    void deniedRequest()
    {
        PolkitAuthorizer authorizer(m_bus, true);
        m_polkit.authorize = false;
        m_polkit.challenge = false;

        std::optional<PolkitAuthorizer::Result> result;
        authorizer.check(QStringLiteral(":1.42"), ActionConfigure, true,
                         [&](const PolkitAuthorizer::Result &r) { result = r; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(!result->authorized);
        QVERIFY(!result->challenge);
    }

    void respectsCallerWithoutInteraction()
    {
        PolkitAuthorizer authorizer(m_bus, true);
        m_polkit.authorize = false;
        m_polkit.challenge = true;

        std::optional<PolkitAuthorizer::Result> result;
        authorizer.check(QStringLiteral(":1.42"), ActionConfigure, false,
                         [&](const PolkitAuthorizer::Result &r) { result = r; });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(m_polkit.lastCall.arguments().at(3).toUInt(), 0u); // no AllowUserInteraction
        QVERIFY(!result->authorized);
        QVERIFY(result->challenge);
    }

    void disabledAuthorizerAllowsEverything()
    {
        PolkitAuthorizer authorizer(m_bus, false);
        std::optional<bool> authorized;
        authorizer.check(QStringLiteral(":1.42"), ActionConfigure, false,
                         [&](const PolkitAuthorizer::Result &r) { authorized = r.authorized; });
        QVERIFY(authorized.value_or(false));
    }

private:
    QDBusConnection m_bus = QDBusConnection::sessionBus();
    FakePolkit m_polkit;
};

QTEST_GUILESS_MAIN(TestPolkit)
#include "tst_polkit.moc"
