#include "language.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

namespace agv {

namespace {

const QString PreferenceKey = QStringLiteral("appearance/language");
const QStringList Supported{QStringLiteral("en"), QStringLiteral("ru")};

} // namespace

Language::Language(QObject *parent)
    : QObject(parent)
{
    const QString saved = QSettings().value(PreferenceKey).toString();
    if (Supported.contains(saved))
        m_preference = saved;
    apply();
}

void Language::setPreference(const QString &preference)
{
    const QString value = Supported.contains(preference) ? preference : QStringLiteral("system");
    if (value == m_preference)
        return;
    m_preference = value;
    QSettings().setValue(PreferenceKey, value);
    emit preferenceChanged();
    apply();
    emit changed();
}

QVariantList Language::options() const
{
    return {
        QVariantMap{{QStringLiteral("value"), QStringLiteral("system")}, {QStringLiteral("label"), tr("System")}},
        QVariantMap{{QStringLiteral("value"), QStringLiteral("en")}, {QStringLiteral("label"), QStringLiteral("English")}},
        QVariantMap{{QStringLiteral("value"), QStringLiteral("ru")}, {QStringLiteral("label"), QStringLiteral("Русский")}},
    };
}

void Language::apply()
{
    const QLocale locale = m_preference == QLatin1String("system") ? QLocale::system() : QLocale(m_preference);
    QLocale::setDefault(locale);

    QCoreApplication::removeTranslator(&m_app);
    QCoreApplication::removeTranslator(&m_qt);
    // English is built in, so a missing file simply means English.
    if (m_app.load(locale, QStringLiteral("agv-gui"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(&m_app);
    if (m_qt.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&m_qt);
}

} // namespace agv
