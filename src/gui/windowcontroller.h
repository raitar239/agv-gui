#pragma once

#include <QObject>
#include <QPointer>
#include <QSize>

class QQmlApplicationEngine;
class QQuickWindow;

namespace agv {

// Owns the QML scene only while the window is open. Closing the window destroys the engine, so
// in the background only the tray and the D-Bus client stay in memory.
class WindowController : public QObject
{
    Q_OBJECT

public:
    explicit WindowController(bool quitOnClose, QObject *parent = nullptr);
    ~WindowController() override;

    void show();
    // Re-evaluates every qsTr() in the open window after the language changed.
    void retranslate();
    // Development aid: render the window at a size and page, save a PNG, then quit.
    void screenshot(const QString &path, const QSize &size, int page, int delayMs);

private:
    QQuickWindow *create();
    void destroy();

    bool m_quitOnClose;
    QQmlApplicationEngine *m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
};

// Exported on the session bus as org.agv.Gui so a second launch shows the running window.
class SingleInstance : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.agv.Gui")

public:
    explicit SingleInstance(QObject *parent = nullptr) : QObject(parent) {}

    // Returns false when another instance is running (and has been asked to show itself).
    bool claim();

signals:
    void showRequested();

public slots:
    Q_SCRIPTABLE void Show() { emit showRequested(); }
};

} // namespace agv
