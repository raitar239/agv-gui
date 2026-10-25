#pragma once

#include "dbustypes.h"

#include <QAbstractListModel>
#include <QSet>

namespace agv {

// VPN locations for QML: filtered by a search string, sorted by ping or name, favorites first.
// Favorites are a per-user preference stored with QSettings.
class LocationModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool sortByName READ sortByName WRITE setSortByName NOTIFY sortByNameChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY favoritesOnlyChanged)
    // Favorite locations that exist in the current list, in display order: [{ city, country, flag, ping }].
    Q_PROPERTY(QVariantList favorites READ favorites NOTIFY countChanged)

public:
    enum Role { IsoRole = Qt::UserRole + 1, CountryRole, CityRole, PingRole, FavoriteRole, FlagRole };

    explicit LocationModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    bool sortByName() const { return m_sortByName; }
    void setSortByName(bool byName);
    bool loading() const { return m_loading; }
    void setLoading(bool loading);
    int total() const { return int(m_all.size()); }
    bool favoritesOnly() const { return m_favoritesOnly; }
    void setFavoritesOnly(bool only);
    QVariantList favorites() const;

    void setLocations(const AgvLocationList &locations);

    Q_INVOKABLE void toggleFavorite(const QString &city);
    // Ping of a city, -1 when unknown; used to show the current location's ping.
    Q_INVOKABLE int pingOf(const QString &city) const;
    Q_INVOKABLE QString flagOf(const QString &cityOrIso) const;
    Q_INVOKABLE QString countryOf(const QString &city) const;

    // Regional-indicator emoji for an ISO 3166 code ("FR" → 🇫🇷).
    static QString flagEmoji(const QString &iso);

signals:
    void filterChanged();
    void sortByNameChanged();
    void loadingChanged();
    void favoritesOnlyChanged();
    void countChanged();

private:
    void rebuild();
    void saveFavorites() const;

    AgvLocationList m_all;
    QList<int> m_visible;  // indexes into m_all
    QSet<QString> m_favorites;
    QString m_filter;
    bool m_sortByName = false;
    bool m_loading = false;
    bool m_favoritesOnly = false;
};

} // namespace agv
