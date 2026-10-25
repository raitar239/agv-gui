#include "installedapps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace agv {

namespace {

// Splits an Exec= value into arguments, honoring the spec's double quotes and backslash escapes.
QStringList splitExec(const QString &exec)
{
    QStringList args;
    QString current;
    bool quoted = false;
    bool any = false;
    for (int i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (quoted && c == QLatin1Char('\\') && i + 1 < exec.size()) {
            current += exec.at(++i);
        } else if (c == QLatin1Char('"')) {
            quoted = !quoted;
            any = true;
        } else if (c.isSpace() && !quoted) {
            if (any || !current.isEmpty())
                args.append(current);
            current.clear();
            any = false;
        } else {
            current += c;
        }
    }
    if (any || !current.isEmpty())
        args.append(current);
    return args;
}

} // namespace

QString execProgram(const QString &exec, const QStringList &searchPath, bool *flatpak)
{
    *flatpak = false;
    QStringList args = splitExec(exec);
    // Drop field codes (%f, %U, …).
    args.erase(std::remove_if(args.begin(), args.end(),
                              [](const QString &arg) { return arg.size() == 2 && arg.startsWith(QLatin1Char('%')); }),
               args.end());

    // "env VAR=value … program"
    if (!args.isEmpty() && QFileInfo(args.first()).fileName() == QLatin1String("env")) {
        args.removeFirst();
        while (!args.isEmpty() && (args.first().contains(QLatin1Char('=')) || args.first().startsWith(QLatin1Char('-'))))
            args.removeFirst();
    }
    if (args.isEmpty())
        return {};

    const QString program = args.first();
    if (QFileInfo(program).fileName() == QLatin1String("flatpak")) {
        *flatpak = true;
        return {};
    }
    if (program.startsWith(QLatin1Char('/')))
        return QFileInfo(program).isExecutable() ? program : QString();
    return QStandardPaths::findExecutable(program, searchPath);
}

std::optional<DesktopApp> parseDesktopFile(const QString &path, const QString &locale,
                                           const QStringList &searchPath)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;

    // Localized keys: Name[ru_RU] beats Name[ru] beats Name.
    const QString language = locale.section(QLatin1Char('_'), 0, 0);
    QString name;
    QString nameLanguage;
    QString nameLocale;
    QString icon;
    QString exec;
    QString type;
    bool hidden = false;
    bool inEntry = false;

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inEntry || line.startsWith(QLatin1Char('#')))
            continue;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();

        if (key == QLatin1String("Name"))
            name = value;
        else if (key == QStringLiteral("Name[%1]").arg(locale))
            nameLocale = value;
        else if (key == QStringLiteral("Name[%1]").arg(language))
            nameLanguage = value;
        else if (key == QLatin1String("Icon"))
            icon = value;
        else if (key == QLatin1String("Exec"))
            exec = value;
        else if (key == QLatin1String("Type"))
            type = value;
        else if ((key == QLatin1String("NoDisplay") || key == QLatin1String("Hidden")
                  || key == QLatin1String("Terminal")) && value == QLatin1String("true"))
            hidden = true;
    }

    if (hidden || type != QLatin1String("Application") || exec.isEmpty())
        return std::nullopt;

    DesktopApp app;
    app.desktopFile = QFileInfo(path).fileName();
    app.name = !nameLocale.isEmpty() ? nameLocale : !nameLanguage.isEmpty() ? nameLanguage : name;
    app.icon = icon;
    app.exe = execProgram(exec, searchPath, &app.flatpak);
    if (app.name.isEmpty() || (app.exe.isEmpty() && !app.flatpak))
        return std::nullopt;
    return app;
}

InstalledApps::InstalledApps(QObject *parent)
    : QAbstractListModel(parent)
{
}

int InstalledApps::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_visible.size());
}

QVariant InstalledApps::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_visible.size())
        return {};
    const DesktopApp &app = m_all.at(m_visible.at(index.row()));
    switch (role) {
    case DesktopFileRole: return app.desktopFile;
    case NameRole: return app.name;
    case IconRole: return app.icon;
    case ExeRole: return app.exe;
    case FlatpakRole: return app.flatpak;
    }
    return {};
}

QHash<int, QByteArray> InstalledApps::roleNames() const
{
    return {{DesktopFileRole, "desktopFile"}, {NameRole, "name"}, {IconRole, "icon"},
            {ExeRole, "exe"}, {FlatpakRole, "flatpak"}};
}

void InstalledApps::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    rebuild();
}

QStringList InstalledApps::applicationDirs()
{
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    // Flatpak exports are in XDG_DATA_DIRS on most systems, but not everywhere.
    for (const QString &extra : {QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications"),
                                 QStringLiteral("/var/lib/flatpak/exports/share/applications")}) {
        if (!dirs.contains(extra))
            dirs.append(extra);
    }
    return dirs;
}

void InstalledApps::reload()
{
    const QString locale = QLocale().name();
    const QStringList searchPath = qEnvironmentVariable("PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);

    QList<DesktopApp> apps;
    QSet<QString> seen;
    for (const QString &dir : applicationDirs()) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const QFileInfo &file : files) {
            // The first directory with a given file name wins, like in application menus.
            if (seen.contains(file.fileName()))
                continue;
            seen.insert(file.fileName());
            if (auto app = parseDesktopFile(file.filePath(), locale, searchPath))
                apps.append(*app);
        }
    }
    std::sort(apps.begin(), apps.end(), [](const DesktopApp &a, const DesktopApp &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });

    m_all = apps;
    rebuild();
}

void InstalledApps::rebuild()
{
    beginResetModel();
    m_visible.clear();
    const QString filter = m_filter.trimmed();
    for (int i = 0; i < m_all.size(); ++i) {
        const DesktopApp &app = m_all.at(i);
        if (filter.isEmpty() || app.name.contains(filter, Qt::CaseInsensitive)
            || app.exe.contains(filter, Qt::CaseInsensitive)) {
            m_visible.append(i);
        }
    }
    endResetModel();
    emit countChanged();
}

} // namespace agv
