#include "locationmodel.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>

#include <algorithm>

namespace agv {

namespace {

const QString FavoritesKey = QStringLiteral("locations/favorites");

const QLatin1String VirtualSuffix(" (Virtual)");

} // namespace

LocationModel::LocationModel(QObject *parent)
    : QAbstractListModel(parent)
{
    const QStringList favorites = QSettings().value(FavoritesKey).toStringList();
    m_favorites = QSet<QString>(favorites.cbegin(), favorites.cend());
    loadNames();
}

void LocationModel::reloadNames()
{
    m_countryNames.clear();
    m_cityNames.clear();
    m_virtualWord.clear();
    loadNames();
    rebuild();
}

void LocationModel::loadNames()
{
    // :/i18n/locations_<language>.json: { countries: {ISO: name}, cities: {CLI name: name}, virtual }
    QFile file(QStringLiteral(":/i18n/locations_%1.json").arg(QLocale().name().section(QLatin1Char('_'), 0, 0)));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject countries = root.value(QStringLiteral("countries")).toObject();
    for (auto it = countries.begin(); it != countries.end(); ++it)
        m_countryNames.insert(it.key(), it.value().toString());
    const QJsonObject cities = root.value(QStringLiteral("cities")).toObject();
    for (auto it = cities.begin(); it != cities.end(); ++it)
        m_cityNames.insert(it.key(), it.value().toString());
    m_virtualWord = root.value(QStringLiteral("virtual")).toString();
}

QString LocationModel::countryName(const AgvLocation &location) const
{
    return m_countryNames.value(location.iso, location.country);
}

QString LocationModel::cityName(const AgvLocation &location) const
{
    // "Moscow (Virtual)" → "Москва (виртуальная)"
    QString base = location.city;
    const bool isVirtual = base.endsWith(VirtualSuffix);
    if (isVirtual)
        base.chop(VirtualSuffix.size());
    const QString name = m_cityNames.value(base);
    if (name.isEmpty())
        return location.city;
    return isVirtual && !m_virtualWord.isEmpty() ? QStringLiteral("%1 (%2)").arg(name, m_virtualWord) : name;
}

int LocationModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_visible.size());
}

QVariant LocationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_visible.size())
        return {};
    const AgvLocation &location = m_all.at(m_visible.at(index.row()));
    switch (role) {
    case IsoRole: return location.iso;
    case CountryRole: return location.country;
    case CityRole: return location.city;
    case PingRole: return location.ping;
    case FavoriteRole: return m_favorites.contains(location.city);
    case FlagRole: return flagEmoji(location.iso);
    case CountryNameRole: return countryName(location);
    case CityNameRole: return cityName(location);
    }
    return {};
}

QHash<int, QByteArray> LocationModel::roleNames() const
{
    return {{IsoRole, "iso"},   {CountryRole, "country"},   {CityRole, "city"},
            {PingRole, "ping"}, {FavoriteRole, "favorite"}, {FlagRole, "flag"},
            {CountryNameRole, "countryName"}, {CityNameRole, "cityName"}};
}

void LocationModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    rebuild();
}

void LocationModel::setSortByName(bool byName)
{
    if (m_sortByName == byName)
        return;
    m_sortByName = byName;
    emit sortByNameChanged();
    rebuild();
}

void LocationModel::setFavoritesOnly(bool only)
{
    if (m_favoritesOnly == only)
        return;
    m_favoritesOnly = only;
    emit favoritesOnlyChanged();
    rebuild();
}

QVariantList LocationModel::favorites() const
{
    QVariantList result;
    for (const AgvLocation &location : m_all) {
        if (!m_favorites.contains(location.city))
            continue;
        result.append(QVariantMap{{QStringLiteral("city"), location.city},
                                  {QStringLiteral("country"), location.country},
                                  {QStringLiteral("cityName"), cityName(location)},
                                  {QStringLiteral("countryName"), countryName(location)},
                                  {QStringLiteral("flag"), flagEmoji(location.iso)},
                                  {QStringLiteral("ping"), location.ping}});
    }
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap left = a.toMap();
        const QVariantMap right = b.toMap();
        const int country = left.value(QStringLiteral("countryName")).toString().localeAwareCompare(
            right.value(QStringLiteral("countryName")).toString());
        return country != 0 ? country < 0
                            : left.value(QStringLiteral("cityName")).toString().localeAwareCompare(
                                  right.value(QStringLiteral("cityName")).toString()) < 0;
    });
    return result;
}

void LocationModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void LocationModel::setLocations(const AgvLocationList &locations)
{
    m_all = locations;
    rebuild();
}

void LocationModel::toggleFavorite(const QString &city)
{
    if (!m_favorites.remove(city))
        m_favorites.insert(city);
    saveFavorites();
    rebuild();
}

int LocationModel::pingOf(const QString &city) const
{
    for (const AgvLocation &location : m_all) {
        if (location.city.compare(city, Qt::CaseInsensitive) == 0)
            return location.ping;
    }
    return -1;
}

QString LocationModel::flagOf(const QString &cityOrIso) const
{
    for (const AgvLocation &location : m_all) {
        if (location.city.compare(cityOrIso, Qt::CaseInsensitive) == 0
            || location.iso.compare(cityOrIso, Qt::CaseInsensitive) == 0) {
            return flagEmoji(location.iso);
        }
    }
    return {};
}

QString LocationModel::countryOf(const QString &city) const
{
    for (const AgvLocation &location : m_all) {
        if (location.city.compare(city, Qt::CaseInsensitive) == 0)
            return countryName(location);
    }
    return {};
}

QString LocationModel::cityName(const QString &city) const
{
    for (const AgvLocation &location : m_all) {
        if (location.city.compare(city, Qt::CaseInsensitive) == 0)
            return cityName(location);
    }
    return city;
}

QString LocationModel::flagEmoji(const QString &iso)
{
    if (iso.size() != 2 || !iso.at(0).isLetter() || !iso.at(1).isLetter())
        return {};
    QString flag;
    for (const QChar letter : iso.toUpper()) {
        const char32_t indicator = 0x1F1E6 + (letter.unicode() - u'A');
        flag += QString::fromUcs4(&indicator, 1);
    }
    return flag;
}

void LocationModel::rebuild()
{
    beginResetModel();
    m_visible.clear();
    const QString filter = m_filter.trimmed();
    for (int i = 0; i < m_all.size(); ++i) {
        if (m_favoritesOnly && !m_favorites.contains(m_all.at(i).city))
            continue;
        const AgvLocation &location = m_all.at(i);
        // Search finds both the original and the translated names.
        if (filter.isEmpty() || location.city.contains(filter, Qt::CaseInsensitive)
            || location.country.contains(filter, Qt::CaseInsensitive)
            || cityName(location).contains(filter, Qt::CaseInsensitive)
            || countryName(location).contains(filter, Qt::CaseInsensitive)
            || location.iso.compare(filter, Qt::CaseInsensitive) == 0) {
            m_visible.append(i);
        }
    }
    std::stable_sort(m_visible.begin(), m_visible.end(), [this](int a, int b) {
        const AgvLocation &left = m_all.at(a);
        const AgvLocation &right = m_all.at(b);
        const bool leftFavorite = m_favorites.contains(left.city);
        const bool rightFavorite = m_favorites.contains(right.city);
        if (leftFavorite != rightFavorite)
            return leftFavorite;
        if (m_sortByName) {
            const int country = countryName(left).localeAwareCompare(countryName(right));
            return country != 0 ? country < 0 : cityName(left).localeAwareCompare(cityName(right)) < 0;
        }
        // Unknown pings (-1) go last.
        const uint leftPing = uint(left.ping);
        const uint rightPing = uint(right.ping);
        return leftPing < rightPing;
    });
    endResetModel();
    emit countChanged();
}

void LocationModel::saveFavorites() const
{
    QStringList favorites(m_favorites.cbegin(), m_favorites.cend());
    favorites.sort();
    QSettings().setValue(FavoritesKey, favorites);
}

} // namespace agv
