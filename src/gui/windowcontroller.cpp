#include "windowcontroller.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

namespace agv {

namespace {

const QString GuiService = QStringLiteral("org.agv.Gui");
const QString GuiPath = QStringLiteral("/Gui");
const QUrl MainQml(QStringLiteral("qrc:/qt/qml/Agv/qml/Main.qml"));

} // namespace

WindowController::WindowController(bool quitOnClose, QObject *parent)
    : QObject(parent)
    , m_quitOnClose(quitOnClose)
{
}

WindowController::~WindowController()
{
    delete m_engine;
}

void WindowController::show()
{
    QQuickWindow *window = m_window ? m_window.data() : create();
    if (!window)
        return;
    window->show();
    window->raise();
    window->requestActivate();
}

void WindowController::screenshot(const QString &path, const QSize &size, int page, int delayMs)
{
    QQuickWindow *window = create();
    if (!window) {
        // Called before app.exec(); exit() only works once the loop runs.
        QMetaObject::invokeMethod(qApp, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
        return;
    }
    window->setProperty("currentPage", page);
    window->resize(size);
    window->show();
    QTimer::singleShot(delayMs, this, [window, path] {
        const bool saved = window->grabWindow().save(path);
        QCoreApplication::exit(saved ? 0 : 1);
    });
}

QQuickWindow *WindowController::create()
{
    m_engine = new QQmlApplicationEngine(this);
    // Default since Qt 6.5; Qt 6.4 needs it to find the Agv module.
    m_engine->addImportPath(QStringLiteral("qrc:/qt/qml"));
    m_engine->load(MainQml);
    if (m_engine->rootObjects().isEmpty()) {
        qCritical("agv-gui: cannot load the user interface");
        destroy();
        return nullptr;
    }

    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    connect(m_window, &QQuickWindow::closing, this, [this] {
        // Let the close finish, then free the whole scene.
        QTimer::singleShot(0, this, [this] {
            destroy();
            if (m_quitOnClose)
                QCoreApplication::quit();
        });
    });
    return m_window;
}

void WindowController::destroy()
{
    if (m_engine) {
        m_engine->deleteLater();
        m_engine = nullptr;
    }
    m_window.clear();
}

bool SingleInstance::claim()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return true; // cannot tell; just run

    if (bus.registerService(GuiService)) {
        bus.registerObject(GuiPath, this, QDBusConnection::ExportScriptableSlots);
        return true;
    }
    bus.call(QDBusMessage::createMethodCall(GuiService, GuiPath, GuiService, QStringLiteral("Show")),
             QDBus::Block, 2000);
    return false;
}

} // namespace agv
