#include "autostart.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>
#include <QStandardPaths>

namespace agv {

Autostart::Autostart(QObject *parent)
    : QObject(parent)
{
}

QString Autostart::entryPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/agv-gui.desktop");
}

QString Autostart::compositorLine() const
{
    // Hyprland sets HYPRLAND_INSTANCE_SIGNATURE for everything it starts.
    const bool hyprland = !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
        || qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("Hyprland"), Qt::CaseInsensitive);
    if (!hyprland)
        return {};
    // Plain "agv-gui" when that is what PATH finds, else this build's full path.
    const QString self = QCoreApplication::applicationFilePath();
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("agv-gui"));
    const QString program = !inPath.isEmpty() && QFileInfo(inPath).canonicalFilePath() == QFileInfo(self).canonicalFilePath()
        ? QStringLiteral("agv-gui")
        : self;
    return QStringLiteral("exec-once = %1 --hidden").arg(program);
}

void Autostart::copyToClipboard(const QString &text) const
{
    QGuiApplication::clipboard()->setText(text);
}

bool Autostart::enabled() const
{
    return QFileInfo::exists(entryPath());
}

void Autostart::setEnabled(bool enabled)
{
    if (enabled == this->enabled())
        return;

    const QString path = entryPath();
    if (!enabled) {
        QFile::remove(path);
        emit enabledChanged();
        return;
    }

    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    // The program's own path, so a development build starts itself too.
    QString program = QCoreApplication::applicationFilePath();
    program.replace(QLatin1Char('"'), QLatin1String("\\\""));
    file.write(QStringLiteral("[Desktop Entry]\n"
                              "Type=Application\n"
                              "Name=AGV\n"
                              "Exec=\"%1\" --hidden\n"
                              "Icon=network-vpn\n"
                              "Terminal=false\n"
                              "X-GNOME-Autostart-enabled=true\n").arg(program).toUtf8());
    if (file.commit())
        emit enabledChanged();
}

} // namespace agv
