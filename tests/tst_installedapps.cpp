#include "installedapps.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace agv;

class TestInstalledApps : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // A fake PATH with a few programs.
        QVERIFY(m_bin.isValid());
        for (const QString &name : {QStringLiteral("firefox"), QStringLiteral("lutris"), QStringLiteral("steam"),
                                    QStringLiteral("my game")}) {
            QFile file(m_bin.path() + QLatin1Char('/') + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        }
        m_path = {m_bin.path()};
    }

    void execProgram_data()
    {
        QTest::addColumn<QString>("exec");
        QTest::addColumn<QString>("program");
        QTest::addColumn<bool>("flatpak");

        QTest::newRow("plain") << "firefox %u" << "firefox" << false;
        QTest::newRow("env") << "env LUTRIS_SKIP_INIT=1 lutris lutris:rungameid/5" << "lutris" << false;
        QTest::newRow("quoted") << "\"my game\" --fullscreen" << "my game" << false;
        QTest::newRow("steam game") << "steam steam://rungameid/1234" << "steam" << false;
        QTest::newRow("flatpak") << "/usr/bin/flatpak run --branch=stable org.telegram.desktop -- %u" << "" << true;
        QTest::newRow("missing") << "nonexistent-program" << "" << false;
        QTest::newRow("only codes") << "%F" << "" << false;
    }

    void execProgram()
    {
        QFETCH(QString, exec);
        QFETCH(QString, program);
        QFETCH(bool, flatpak);

        bool isFlatpak = false;
        const QString result = agv::execProgram(exec, m_path, &isFlatpak);
        QCOMPARE(result, program.isEmpty() ? QString() : m_bin.path() + QLatin1Char('/') + program);
        QCOMPARE(isFlatpak, flatpak);
    }

    void absoluteExec()
    {
        bool flatpak = false;
        const QString firefox = m_bin.path() + QStringLiteral("/firefox");
        QCOMPARE(agv::execProgram(firefox + QStringLiteral(" --new-window"), {}, &flatpak), firefox);
        QCOMPARE(agv::execProgram(QStringLiteral("/nonexistent/firefox"), {}, &flatpak), QString());
    }

    void desktopFile()
    {
        QTemporaryDir dir;
        const QString path = write(dir, QStringLiteral("firefox.desktop"),
                                   "[Desktop Entry]\n"
                                   "Type=Application\n"
                                   "Name=Firefox\n"
                                   "Name[ru]=Файрфокс\n"
                                   "Name[ru_RU]=Огненный лис\n"
                                   "Icon=firefox\n"
                                   "Exec=firefox %u\n"
                                   "\n"
                                   "[Desktop Action new-window]\n"
                                   "Name=New Window\n"
                                   "Exec=firefox --new-window %u\n");

        auto app = parseDesktopFile(path, QStringLiteral("ru_RU"), m_path);
        QVERIFY(app);
        QCOMPARE(app->name, QStringLiteral("Огненный лис"));
        QCOMPARE(app->icon, QStringLiteral("firefox"));
        QCOMPARE(app->desktopFile, QStringLiteral("firefox.desktop"));
        QCOMPARE(app->exe, m_bin.path() + QStringLiteral("/firefox"));

        QCOMPARE(parseDesktopFile(path, QStringLiteral("ru_UA"), m_path)->name, QStringLiteral("Файрфокс"));
        QCOMPARE(parseDesktopFile(path, QStringLiteral("de_DE"), m_path)->name, QStringLiteral("Firefox"));
    }

    void skippedEntries_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("hidden") << QByteArray("[Desktop Entry]\nType=Application\nName=X\nExec=firefox\nNoDisplay=true\n");
        QTest::newRow("terminal") << QByteArray("[Desktop Entry]\nType=Application\nName=X\nExec=firefox\nTerminal=true\n");
        QTest::newRow("link") << QByteArray("[Desktop Entry]\nType=Link\nName=X\nURL=https://example.com\n");
        QTest::newRow("no exec") << QByteArray("[Desktop Entry]\nType=Application\nName=X\n");
        QTest::newRow("missing program") << QByteArray("[Desktop Entry]\nType=Application\nName=X\nExec=nope\n");
        QTest::newRow("exec in action only")
            << QByteArray("[Desktop Entry]\nType=Application\nName=X\n[Desktop Action a]\nExec=firefox\n");
    }

    void skippedEntries()
    {
        QFETCH(QByteArray, content);
        QTemporaryDir dir;
        QVERIFY(!parseDesktopFile(write(dir, QStringLiteral("x.desktop"), content), QStringLiteral("en_US"), m_path));
    }

    void flatpakEntryIsKeptButMarked()
    {
        QTemporaryDir dir;
        const auto app = parseDesktopFile(
            write(dir, QStringLiteral("org.telegram.desktop.desktop"),
                  "[Desktop Entry]\nType=Application\nName=Telegram\n"
                  "Exec=/usr/bin/flatpak run --command=telegram-desktop org.telegram.desktop -- %u\n"),
            QStringLiteral("en_US"), m_path);
        QVERIFY(app);
        QVERIFY(app->flatpak);
        QVERIFY(app->exe.isEmpty());
    }

private:
    QString write(QTemporaryDir &dir, const QString &name, const QByteArray &content)
    {
        const QString path = dir.path() + QLatin1Char('/') + name;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(content);
        return path;
    }

    QTemporaryDir m_bin;
    QStringList m_path;
};

QTEST_GUILESS_MAIN(TestInstalledApps)
#include "tst_installedapps.moc"
