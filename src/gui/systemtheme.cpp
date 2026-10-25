#include "systemtheme.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>

namespace agv {

namespace {

const QString PortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString PortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString PortalSettings = QStringLiteral("org.freedesktop.portal.Settings");
const QString AppearanceGroup = QStringLiteral("org.freedesktop.appearance");
const QString ColorSchemeKey = QStringLiteral("color-scheme");
const QString PreferenceKey = QStringLiteral("appearance/theme");

// The portal's color-scheme: 0 no preference, 1 prefer dark, 2 prefer light.
QVariant unwrap(QVariant value)
{
    while (value.canConvert<QDBusVariant>() && value.metaType() == QMetaType::fromType<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value;
}

} // namespace

SystemTheme::SystemTheme(QObject *parent)
    : QObject(parent)
{
    const QString saved = QSettings().value(PreferenceKey).toString();
    if (saved == QLatin1String("light") || saved == QLatin1String("dark"))
        m_preference = saved;

    m_systemDark = qtThinksDark();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (!m_portalKnows)
            setSystemDark(qtThinksDark());
    });
#endif

    QDBusConnection::sessionBus().connect(PortalService, PortalPath, PortalSettings,
                                          QStringLiteral("SettingChanged"), this,
                                          SLOT(onPortalSettingChanged(QString,QString,QDBusVariant)));
    readPortal();
}

bool SystemTheme::dark() const
{
    if (m_preference == QLatin1String("dark"))
        return true;
    if (m_preference == QLatin1String("light"))
        return false;
    return m_systemDark;
}

void SystemTheme::setPreference(const QString &preference)
{
    const QString value = (preference == QLatin1String("light") || preference == QLatin1String("dark"))
        ? preference
        : QStringLiteral("system");
    if (value == m_preference)
        return;
    const bool wasDark = dark();
    m_preference = value;
    QSettings().setValue(PreferenceKey, value);
    emit preferenceChanged();
    if (wasDark != dark())
        emit darkChanged();
}

void SystemTheme::onPortalSettingChanged(const QString &group, const QString &key, const QDBusVariant &value)
{
    if (group != AppearanceGroup || key != ColorSchemeKey)
        return;
    const uint scheme = unwrap(value.variant()).toUInt();
    m_portalKnows = scheme != 0;
    setSystemDark(scheme == 0 ? qtThinksDark() : scheme == 1);
}

void SystemTheme::readPortal()
{
    QDBusMessage call = QDBusMessage::createMethodCall(PortalService, PortalPath, PortalSettings,
                                                       QStringLiteral("Read"));
    call << AppearanceGroup << ColorSchemeKey;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ErrorMessage)
            return; // no portal: keep Qt's answer
        const uint scheme = unwrap(reply.arguments().value(0)).toUInt();
        m_portalKnows = scheme != 0;
        if (m_portalKnows)
            setSystemDark(scheme == 1);
    });
}

void SystemTheme::setSystemDark(bool dark)
{
    if (m_systemDark == dark)
        return;
    const bool wasDark = this->dark();
    m_systemDark = dark;
    if (wasDark != this->dark())
        emit darkChanged();
}

bool SystemTheme::qtThinksDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
#endif
    const QColor window = QGuiApplication::palette().color(QPalette::Window);
    return window.lightness() < 128;
}

} // namespace agv
