#pragma once

#include <QObject>

namespace agv {

// Starts AGV in the tray at login through an XDG autostart entry
// ($XDG_CONFIG_HOME/autostart/agv-gui.desktop). Desktops that follow the XDG autostart spec
// (KDE, GNOME, Xfce, systemd's xdg-autostart generator) pick it up; bare compositors may not.
class Autostart : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // The line to put into the compositor config where XDG autostart is not used (Hyprland), else "".
    Q_PROPERTY(QString compositorLine READ compositorLine CONSTANT)

public:
    explicit Autostart(QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool enabled);

    static QString entryPath();
    QString compositorLine() const;

    Q_INVOKABLE void copyToClipboard(const QString &text) const;

signals:
    void enabledChanged();

private:
    bool write();
};

} // namespace agv
