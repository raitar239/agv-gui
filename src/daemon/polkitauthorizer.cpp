#include "polkitauthorizer.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace {

// polkit's Subject: (sa{sv})
struct PolkitSubject {
    QString kind;
    QVariantMap details;
};

QDBusArgument &operator<<(QDBusArgument &arg, const PolkitSubject &subject)
{
    arg.beginStructure();
    arg << subject.kind << subject.details;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PolkitSubject &subject)
{
    arg.beginStructure();
    arg >> subject.kind >> subject.details;
    arg.endStructure();
    return arg;
}

constexpr uint AllowUserInteraction = 0x1;
constexpr int InteractiveTimeoutMs = 5 * 60'000; // the user may take a while to type a password

} // namespace

Q_DECLARE_METATYPE(PolkitSubject)

namespace agv {

PolkitAuthorizer::PolkitAuthorizer(QDBusConnection bus, bool enabled, QObject *parent)
    : QObject(parent)
    , m_bus(std::move(bus))
    , m_enabled(enabled)
{
    qDBusRegisterMetaType<PolkitSubject>();
}

void PolkitAuthorizer::check(const QString &sender, const QString &actionId, Callback callback)
{
    if (!m_enabled) {
        callback(true, {});
        return;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.PolicyKit1"),
        QStringLiteral("/org/freedesktop/PolicyKit1/Authority"),
        QStringLiteral("org.freedesktop.PolicyKit1.Authority"),
        QStringLiteral("CheckAuthorization"));

    PolkitSubject subject{QStringLiteral("system-bus-name"),
                          {{QStringLiteral("name"), QVariant::fromValue(sender)}}};
    call << QVariant::fromValue(subject) << actionId
         << QVariant::fromValue(QMap<QString, QString>()) << AllowUserInteraction << QString();

    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, InteractiveTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [callback = std::move(callback)](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            callback(false, reply.errorMessage());
            return;
        }

        // Result: (bba{ss}) is_authorized, is_challenge, details
        bool authorized = false;
        bool challenge = false;
        QMap<QString, QString> details;
        const auto result = reply.arguments().value(0).value<QDBusArgument>();
        result.beginStructure();
        result >> authorized >> challenge >> details;
        result.endStructure();

        callback(authorized, authorized ? QString() : QStringLiteral("Not authorized"));
    });
}

} // namespace agv
