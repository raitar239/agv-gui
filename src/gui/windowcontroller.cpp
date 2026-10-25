#include "windowcontroller.h"

#include "iconprovider.h"

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

void WindowController::retranslate()
{
    if (m_engine)
        m_engine->retranslate();
}

void WindowController::screenshot(const QString &path, const QSize &size, int page, int delayMs)
{
    // Development: open and close a window first, as the tray does, to check that a second
    // window in the same process still works.
    if (qEnvironmentVariableIntValue("AGV_SCREENSHOT_REOPEN")) {
        m_quitOnClose = false; // the shot ends the program itself
        if (QQuickWindow *first = create()) {
            first->show();
            first->close(); // the same path as closing it from the window manager
            for (int i = 0; i < 10 && m_engine; ++i) {
                QCoreApplication::processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
            if (m_engine)
                qCritical("agv-gui: closing the window did not free its scene");
        }
    }
    QQuickWindow *window = create();
    if (!window) {
        // Called before app.exec(); exit() only works once the loop runs.
        QMetaObject::invokeMethod(qApp, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
        return;
    }
    window->setProperty("currentPage", page);
    // e.g. "apps" or "apps-picker": which tab or dialog to show in the shot.
    window->setProperty("screenshotState", qEnvironmentVariable("AGV_SCREENSHOT_STATE"));
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
    m_engine->addImageProvider(QStringLiteral("appicon"), new AppIconProvider);
    m_engine->load(MainQml);
    if (m_engine->rootObjects().isEmpty()) {
        qCritical("agv-gui: cannot load the user interface");
        destroy();
        return nullptr;
    }

    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    // A closed window gets hidden. (Not QQuickWindow::closing: its QQuickCloseEvent argument is an
    // incomplete type in Qt 6.4 headers, which breaks the functor connect there.)
    connect(m_window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible)
            return;
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
