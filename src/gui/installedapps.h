#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace agv {

// One launcher from a .desktop file, reduced to what an app exclusion needs.
struct DesktopApp {
    QString desktopFile;  // "firefox.desktop"
    QString name;         // localized Name=
    QString icon;         // Icon= (theme name or absolute path)
    QString exe;          // absolute path of the program Exec= starts
    bool flatpak = false; // sandboxed apps cannot be excluded yet
};

// Parses a .desktop file; nullopt for entries that are hidden, not applications or have no Exec.
std::optional<DesktopApp> parseDesktopFile(const QString &path, const QString &locale,
                                           const QStringList &searchPath);

// The program an Exec= line runs: field codes and "env VAR=…" stripped, resolved through PATH.
// Sets flatpak for "flatpak run …" launchers.
QString execProgram(const QString &exec, const QStringList &searchPath, bool *flatpak);

// Installed applications from $XDG_DATA_HOME and $XDG_DATA_DIRS (later directories lose to
// earlier ones, as in the menu), filtered by a search string, sorted by name.
class InstalledApps : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role { DesktopFileRole = Qt::UserRole + 1, NameRole, IconRole, ExeRole, FlatpakRole };

    explicit InstalledApps(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);

    // Re-reads the application directories (cheap; done each time the picker opens).
    Q_INVOKABLE void reload();

    static QStringList applicationDirs();

signals:
    void filterChanged();
    void countChanged();

private:
    void rebuild();

    QList<DesktopApp> m_all;
    QList<int> m_visible;
    QString m_filter;
};

} // namespace agv
