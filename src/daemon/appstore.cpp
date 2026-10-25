#include "appstore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>

namespace agv {

namespace {

constexpr int FormatVersion = 1;
constexpr qsizetype MaxNameLength = 200;
constexpr qsizetype MaxFieldLength = 4096;

const QString KeyName = QStringLiteral("name");
const QString KeyIcon = QStringLiteral("icon");
const QString KeyDesktopFile = QStringLiteral("desktop_file");
const QString KeyExe = QStringLiteral("exe");
const QString KeyCmdlineMatch = QStringLiteral("cmdline_match");
const QString KeyEnabled = QStringLiteral("enabled");

bool takeString(const QVariantMap &map, const QString &key, QString *target, QString *error)
{
    if (!map.contains(key))
        return true;
    const QVariant value = map.value(key);
    if (value.metaType().id() != QMetaType::QString) {
        *error = QStringLiteral("%1 must be a string").arg(key);
        return false;
    }
    *target = value.toString();
    if (target->size() > MaxFieldLength || target->contains(QChar(0))) {
        *error = QStringLiteral("%1 is too long or contains NUL").arg(key);
        return false;
    }
    return true;
}

} // namespace

AppStore::AppStore(QString path)
    : m_path(std::move(path))
{
}

QString AppStore::load()
{
    m_rules.clear();
    m_mode = QStringLiteral("bypass");
    QFile file(m_path);
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("Cannot read %1").arg(m_path);

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("%1 is not valid JSON: %2").arg(m_path, parseError.errorString());

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != FormatVersion)
        return QStringLiteral("%1 has an unsupported version").arg(m_path);

    const QString mode = root.value(QStringLiteral("mode")).toString(QStringLiteral("bypass"));
    if (!isValidMode(mode))
        return QStringLiteral("%1 has an unknown mode %2").arg(m_path, mode);
    m_mode = mode;

    for (const QJsonValue &value : root.value(QStringLiteral("apps")).toArray()) {
        const QVariantMap map = value.toObject().toVariantMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        QString error;
        QVariantMap fields = map;
        const qint64 owner = fields.take(QStringLiteral("owner_uid")).toLongLong();
        auto rule = fromMap(fields, &error);
        if (!isValidId(id) || !rule || find(id))
            return QStringLiteral("%1 contains an invalid entry %2 %3").arg(m_path, id, error);
        rule->id = id;
        rule->ownerUid = map.contains(QStringLiteral("owner_uid")) ? owner : -1;
        m_rules.append(*rule);
    }
    return {};
}

QString AppStore::save() const
{
    QJsonArray apps;
    for (const AppRule &rule : m_rules) {
        QVariantMap map = toMap(rule);
        map.insert(QStringLiteral("id"), rule.id);
        map.insert(QStringLiteral("owner_uid"), rule.ownerUid);
        apps.append(QJsonObject::fromVariantMap(map));
    }
    const QJsonObject root{{QStringLiteral("version"), FormatVersion},
                           {QStringLiteral("mode"), m_mode},
                           {QStringLiteral("apps"), apps}};

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot write %1").arg(m_path);
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(root).toJson());
    if (!file.commit())
        return QStringLiteral("Cannot write %1").arg(m_path);
    return {};
}

const AppRule *AppStore::find(const QString &id) const
{
    for (const AppRule &rule : m_rules) {
        if (rule.id == id)
            return &rule;
    }
    return nullptr;
}

std::optional<AppRule> AppStore::fromMap(const QVariantMap &map, QString *error, const AppRule *base)
{
    static const QStringList known{QStringLiteral("id"), KeyName, KeyIcon, KeyDesktopFile,
                                   KeyExe, KeyCmdlineMatch, KeyEnabled};
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!known.contains(it.key())) {
            *error = QStringLiteral("Unknown field %1").arg(it.key());
            return std::nullopt;
        }
    }

    AppRule rule = base ? *base : AppRule{};
    if (!takeString(map, KeyName, &rule.name, error) || !takeString(map, KeyIcon, &rule.icon, error)
        || !takeString(map, KeyDesktopFile, &rule.desktopFile, error)
        || !takeString(map, KeyExe, &rule.exe, error)
        || !takeString(map, KeyCmdlineMatch, &rule.cmdlineMatch, error)) {
        return std::nullopt;
    }
    if (map.contains(KeyEnabled)) {
        if (map.value(KeyEnabled).metaType().id() != QMetaType::Bool) {
            *error = QStringLiteral("enabled must be a boolean");
            return std::nullopt;
        }
        rule.enabled = map.value(KeyEnabled).toBool();
    }

    rule.name = rule.name.trimmed();
    if (rule.name.isEmpty() || rule.name.size() > MaxNameLength) {
        *error = QStringLiteral("name must be 1-%1 characters").arg(MaxNameLength);
        return std::nullopt;
    }
    if (!rule.exe.isEmpty() && !rule.exe.startsWith(QLatin1Char('/'))) {
        *error = QStringLiteral("exe must be an absolute path");
        return std::nullopt;
    }
    if (!rule.desktopFile.isEmpty() && !rule.desktopFile.endsWith(QLatin1String(".desktop"))) {
        *error = QStringLiteral("desktop_file must end with .desktop");
        return std::nullopt;
    }
    return rule;
}

QVariantMap AppStore::toMap(const AppRule &rule)
{
    return {
        {KeyName, rule.name},
        {KeyIcon, rule.icon},
        {KeyDesktopFile, rule.desktopFile},
        {KeyExe, rule.exe},
        {KeyCmdlineMatch, rule.cmdlineMatch},
        {KeyEnabled, rule.enabled},
    };
}

bool AppStore::isValidMode(const QString &mode)
{
    return mode == QLatin1String("bypass") || mode == QLatin1String("vpn-only");
}

bool AppStore::isValidId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^app-[0-9a-f]{8}$"));
    return pattern.match(id).hasMatch();
}

QString AppStore::add(AppRule rule)
{
    do {
        rule.id = QStringLiteral("app-%1").arg(QRandomGenerator::system()->generate(), 8, 16, QLatin1Char('0'));
    } while (find(rule.id));
    m_rules.append(rule);
    return rule.id;
}

bool AppStore::replace(const AppRule &rule)
{
    for (AppRule &existing : m_rules) {
        if (existing.id == rule.id) {
            existing = rule;
            return true;
        }
    }
    return false;
}

bool AppStore::remove(const QString &id)
{
    return m_rules.removeIf([&](const AppRule &rule) { return rule.id == id; }) > 0;
}

} // namespace agv
