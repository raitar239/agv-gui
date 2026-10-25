#pragma once

#include <QObject>
#include <QTranslator>
#include <QVariantList>

namespace agv {

// The interface language: the system's, or one the user picked (stored with QSettings).
// Switching swaps the translators and sets the default QLocale; listeners retranslate.
class Language : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString preference READ preference WRITE setPreference NOTIFY preferenceChanged)
    // [{ value, label }], labels in their own language so everyone finds theirs.
    Q_PROPERTY(QVariantList options READ options NOTIFY changed)

public:
    explicit Language(QObject *parent = nullptr);

    QString preference() const { return m_preference; }  // "system", "en", "ru"
    void setPreference(const QString &preference);
    QVariantList options() const;

signals:
    void preferenceChanged();
    // Translators were swapped; UI text must be refreshed.
    void changed();

private:
    void apply();

    QString m_preference = QStringLiteral("system");
    QTranslator m_app;
    QTranslator m_qt;
};

} // namespace agv
