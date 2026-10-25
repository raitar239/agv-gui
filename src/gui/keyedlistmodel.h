#pragma once

#include <QAbstractListModel>
#include <QByteArrayList>
#include <QVariantMap>

namespace agv {

// A list of maps for QML that applies updates as row inserts, removals, moves and changes
// instead of a reset, so views keep their scroll position and delegates while data refreshes.
// Rows are identified by one key field.
class KeyedListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    KeyedListModel(QByteArray keyRole, QByteArrayList roles, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Makes the model equal to rows (in that order) with the fewest changes.
    void setRows(const QList<QVariantMap> &rows);
    const QList<QVariantMap> &rows() const { return m_rows; }

signals:
    void countChanged();

private:
    QString keyOf(const QVariantMap &row) const;
    int indexOf(const QString &key, int from) const;

    QString m_key;
    QByteArrayList m_roles;
    QList<QVariantMap> m_rows;
};

} // namespace agv
