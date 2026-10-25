#include "keyedlistmodel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

using namespace agv;

namespace {

QList<QVariantMap> apps(const QList<QPair<QString, int>> &entries)
{
    QList<QVariantMap> rows;
    for (const auto &[id, bytes] : entries)
        rows.append({{QStringLiteral("id"), id}, {QStringLiteral("bytes"), bytes}});
    return rows;
}

QStringList ids(const KeyedListModel &model)
{
    QStringList result;
    for (const QVariantMap &row : model.rows())
        result << row.value(QStringLiteral("id")).toString();
    return result;
}

} // namespace

class TestKeyedListModel : public QObject
{
    Q_OBJECT

private slots:
    void updatesWithoutReset()
    {
        KeyedListModel model("id", {"id", "bytes"});
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setRows(apps({{"a", 0}, {"b", 0}, {"c", 0}}));

        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

        // Removing one item is exactly one removal.
        model.setRows(apps({{"a", 0}, {"c", 0}}));
        QCOMPARE(removed.count(), 1);
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(changed.count(), 0);

        // A traffic refresh only changes data.
        model.setRows(apps({{"a", 10}, {"c", 0}}));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(model.data(model.index(0), Qt::UserRole + 2).toInt(), 10);

        // Adding goes to its place.
        model.setRows(apps({{"a", 10}, {"b2", 0}, {"c", 0}}));
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(ids(model), QStringList({"a", "b2", "c"}));

        QCOMPARE(reset.count(), 0);
    }

    void reorders()
    {
        KeyedListModel model("id", {"id", "bytes"});
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setRows(apps({{"a", 0}, {"b", 0}, {"c", 0}, {"d", 0}}));

        model.setRows(apps({{"d", 0}, {"a", 1}, {"c", 0}, {"e", 0}}));
        QCOMPARE(ids(model), QStringList({"d", "a", "c", "e"}));
        QCOMPARE(model.data(model.index(1), Qt::UserRole + 2).toInt(), 1);

        model.setRows({});
        QCOMPARE(model.rowCount(), 0);
    }

    void roles()
    {
        KeyedListModel model("item", {"item"});
        model.setRows({{{QStringLiteral("item"), QStringLiteral("example.com")}}});
        QCOMPARE(model.roleNames().value(Qt::UserRole + 1), QByteArray("item"));
        QCOMPARE(model.data(model.index(0), Qt::UserRole + 1).toString(), QStringLiteral("example.com"));
        QVERIFY(!model.data(model.index(0), Qt::UserRole + 5).isValid());
    }
};

QTEST_GUILESS_MAIN(TestKeyedListModel)
#include "tst_keyedlistmodel.moc"
