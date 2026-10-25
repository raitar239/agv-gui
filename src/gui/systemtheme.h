#pragma once

#include <QDBusVariant>
#include <QObject>
#include <QVariant>

namespace agv {

// Light or dark, following the desktop. Asks the XDG desktop portal first (works on KDE, GNOME,
// Hyprland and others with a portal running), then Qt's own color scheme, then the palette.
// The user can pin light or dark; the choice is stored with QSettings.
class SystemTheme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    Q_PROPERTY(QString preference READ preference WRITE setPreference NOTIFY preferenceChanged)

public:
    explicit SystemTheme(QObject *parent = nullptr);

    bool dark() const;
    QString preference() const { return m_preference; }  // "system", "light" or "dark"
    void setPreference(const QString &preference);

signals:
    void darkChanged();
    void preferenceChanged();

private slots:
    void onPortalSettingChanged(const QString &group, const QString &key, const QDBusVariant &value);

private:
    void readPortal();
    void setSystemDark(bool dark);
    static bool qtThinksDark();

    QString m_preference = QStringLiteral("system");
    bool m_systemDark = false;
    bool m_portalKnows = false;
};

} // namespace agv
