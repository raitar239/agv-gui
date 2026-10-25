#include "keyedlistmodel.h"

#include <QSet>

namespace agv {

KeyedListModel::KeyedListModel(QByteArray keyRole, QByteArrayList roles, QObject *parent)
    : QAbstractListModel(parent)
    , m_key(QString::fromUtf8(keyRole))
    , m_roles(std::move(roles))
{
}

int KeyedListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant KeyedListModel::data(const QModelIndex &index, int role) const
{
    const int column = role - Qt::UserRole - 1;
    if (!index.isValid() || index.row() >= m_rows.size() || column < 0 || column >= m_roles.size())
        return {};
    return m_rows.at(index.row()).value(QString::fromUtf8(m_roles.at(column)));
}

QHash<int, QByteArray> KeyedListModel::roleNames() const
{
    QHash<int, QByteArray> names;
    for (int i = 0; i < m_roles.size(); ++i)
        names.insert(Qt::UserRole + 1 + i, m_roles.at(i));
    return names;
}

void KeyedListModel::setRows(const QList<QVariantMap> &rows)
{
    const int before = int(m_rows.size());

    // 1. Drop rows that are gone, from the end so indexes stay valid.
    QSet<QString> wanted;
    for (const QVariantMap &row : rows)
        wanted.insert(keyOf(row));
    for (int i = int(m_rows.size()) - 1; i >= 0; --i) {
        if (!wanted.contains(keyOf(m_rows.at(i)))) {
            beginRemoveRows(QModelIndex(), i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }

    // 2. Walk the target order: keep, move up, or insert; then refresh changed values.
    for (int i = 0; i < rows.size(); ++i) {
        const QVariantMap &target = rows.at(i);
        const QString key = keyOf(target);
        if (i < m_rows.size() && keyOf(m_rows.at(i)) == key) {
            // already in place
        } else if (const int from = indexOf(key, i + 1); from >= 0) {
            beginMoveRows(QModelIndex(), from, from, QModelIndex(), i);
            m_rows.move(from, i);
            endMoveRows();
        } else {
            beginInsertRows(QModelIndex(), i, i);
            m_rows.insert(i, target);
            endInsertRows();
            continue;
        }
        if (m_rows.at(i) != target) {
            m_rows[i] = target;
            emit dataChanged(index(i), index(i));
        }
    }

    if (before != m_rows.size())
        emit countChanged();
}

QString KeyedListModel::keyOf(const QVariantMap &row) const
{
    return row.value(m_key).toString();
}

int KeyedListModel::indexOf(const QString &key, int from) const
{
    for (int i = from; i < m_rows.size(); ++i) {
        if (keyOf(m_rows.at(i)) == key)
            return i;
    }
    return -1;
}

} // namespace agv
