#include "windowmanager.h"

#include <QAction>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QScreen>
#include <QWindow>

#include <algorithm>

#include "backend.h"
#include "mainwindow.h"

// How far each new window steps down and to the right of the front one, as
// macOS cascades document windows.
constexpr int cascadeStep = 22;

namespace {

// Two names for one file compare equal once links and relative parts are
// resolved; a file that is not there yet goes by its absolute path.
QString fileKey(const QUrl &url) {
    if (!url.isLocalFile())
        return {};
    const QFileInfo info(url.toLocalFile());
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

void bringForward(MainWindow *window) {
    if (window->windowState() & Qt::WindowMinimized)
        window->setWindowState(window->windowState() & ~Qt::WindowMinimized);
    window->raise();
    window->activateWindow();
}

}

WindowManager::WindowManager(QObject *parent)
    : QObject(parent), m_textScale(Backend::storedTextScale()) {
    buildNoWindowMenuBar();
}

// The backends are children, so they go after every window, which still
// reads them.
WindowManager::~WindowManager() {
    for (const Window &entry : std::as_const(m_windows)) {
        if (entry.window) {
            disconnect(entry.window, nullptr, this, nullptr);
            delete entry.window;
        }
    }
}

// Each window brings its own menus. Once every window has closed, macOS shows
// this parentless menu bar instead, so a window can still be opened. Its
// commands are off while any window is open, so a key never belongs to two
// menus at once.
void WindowManager::buildNoWindowMenuBar() {
    m_noWindowMenuBar = std::make_unique<QMenuBar>();
    QMenu *file = m_noWindowMenuBar->addMenu(QStringLiteral("File"));

    QAction *newWindow = file->addAction(QStringLiteral("New Window"));
    newWindow->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    connect(newWindow, &QAction::triggered, this, [this]() { this->newWindow(); });

    QAction *open = file->addAction(QStringLiteral("Open…"));
    open->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    connect(open, &QAction::triggered, this, [this]() {
        const QUrl url = QFileDialog::getOpenFileUrl(
            nullptr, QStringLiteral("Open File"), QUrl(),
            QStringLiteral("Markdown files (*.md *.markdown);;All files (*)"));
        if (url.isValid())
            openFile(url);
    });

    const auto enableWithNoWindow = [this, file]() {
        const bool noWindow = windows().isEmpty();
        for (QAction *action : file->actions())
            action->setEnabled(noWindow);
    };
    connect(this, &WindowManager::windowCountChanged, file, enableWithNoWindow);
    enableWithNoWindow();
}

QList<MainWindow *> WindowManager::windows() const {
    QList<MainWindow *> open;
    for (const Window &entry : m_windows) {
        if (entry.open && entry.window)
            open.append(entry.window);
    }
    return open;
}

MainWindow *WindowManager::activeWindow() const {
    const QList<MainWindow *> open = windows();
    return open.isEmpty() ? nullptr : open.constFirst();
}

Backend *WindowManager::activeBackend() const {
    return backendFor(activeWindow());
}

Backend *WindowManager::backendFor(const MainWindow *window) const {
    const int index = indexOf(window);
    return index < 0 ? nullptr : m_windows.at(index).backend;
}

int WindowManager::indexOf(const MainWindow *window) const {
    if (!window)
        return -1;
    for (int index = 0; index < m_windows.size(); ++index) {
        if (m_windows.at(index).window == window)
            return index;
    }
    return -1;
}

MainWindow *WindowManager::newWindow() {
    auto *backend = new Backend(this);
    backend->setDarkMode(m_darkMode);
    // The first window opens where the last one closed; the rest step out
    // from the front window, so a new one does not hide another.
    const WindowGeometry geometry = windows().isEmpty() ? backend->windowGeometry() : cascadedGeometry();
    auto *window = new MainWindow(backend);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->applyGeometry(geometry);
    m_windows.prepend({window, backend});

    connect(backend, &Backend::newWindowRequested, this, &WindowManager::newWindow);
    connect(backend, &Backend::textScaleChanged, this, [this, backend]() {
        shareTextScale(backend);
    });
    connect(backend, &Backend::editorFontChanged, this, [this, backend]() {
        shareEditorFont(backend);
    });
    connect(window, &MainWindow::activated, this, [this, window]() { windowActivated(window); });
    connect(window, &MainWindow::closed, this, [this, window]() { windowClosed(window); });
    connect(window, &QObject::destroyed, this, [this, backend]() { windowDestroyed(backend); });
    // A quit waiting on this window's answer is off once the writer cancels.
    connect(window, &MainWindow::closeCancelled, this, [this, window]() {
        if (m_quitWaitingOn == window)
            m_quitWaitingOn = nullptr;
    });

    window->show();
    emit windowCountChanged();
    return window;
}

void WindowManager::openStartupWindows() {
    if (!newWindow())
        return;

    // Each window claims one draft as it opens, so a crash that left several
    // windows unsaved brings all of them back.
    for (int drafts = Backend::unclaimedRecoveryDrafts(); drafts > 0; --drafts)
        newWindow();
}

void WindowManager::openFile(const QUrl &url) {
    const QString key = fileKey(url);
    MainWindow *target = nullptr;
    for (MainWindow *window : windows()) {
        if (!key.isEmpty() && fileKey(backendFor(window)->fileUrl()) == key) {
            target = window;
            break;
        }
    }

    if (!target) {
        for (MainWindow *window : windows()) {
            if (backendFor(window)->isUntouched()) {
                target = window;
                break;
            }
        }
        if (!target)
            target = newWindow();
        if (!target)
            return;
        backendFor(target)->openOrStartFile(url);
    }

    bringForward(target);
}

bool WindowManager::closeAllWindows() {
    m_quitWaitingOn = nullptr;

    // Every question comes before any window closes, so cancelling one
    // leaves the windows nobody was asked about open. The front window asks
    // first.
    for (MainWindow *window : windows()) {
        if (!backendFor(window)->modified())
            continue;
        bringForward(window);
        if (!window->close()) {
            m_quitWaitingOn = window;
            return false;
        }
    }

    // The front window closes last, so its place is the one remembered.
    const QList<MainWindow *> open = windows();
    for (auto it = open.crbegin(); it != open.crend(); ++it) {
        if (!(*it)->close()) {
            m_quitWaitingOn = *it;
            return false;
        }
    }
    return true;
}

// With every window closed, the Dock icon opens a new one, as in other Mac
// apps. A minimized window is still open, and macOS brings it back itself.
void WindowManager::reopen() {
    if (windows().isEmpty())
        newWindow();
}

void WindowManager::setDarkMode(bool darkMode) {
    m_darkMode = darkMode;
    for (const Window &entry : std::as_const(m_windows))
        entry.backend->setDarkMode(darkMode);
}

WindowGeometry WindowManager::cascadedGeometry() const {
    const MainWindow *front = activeWindow();
    // A full-screen or zoomed window has no place of its own to step from.
    if (front->windowState() & (Qt::WindowFullScreen | Qt::WindowMaximized))
        return backendFor(front)->windowGeometry();

    const QScreen *screen = front->screen();
    const QRect available = screen ? screen->availableGeometry() : QRect();
    const QMargins frame = front->windowHandle() ? front->windowHandle()->frameMargins() : QMargins();
    const QList<MainWindow *> open = windows();
    const auto taken = [&open](const QPoint &position) {
        return std::any_of(open.cbegin(), open.cend(), [&position](const MainWindow *window) {
            return window->geometry().topLeft() == position;
        });
    };

    // Step on past a window already sitting there. Past the screen's lower
    // right corner, start again from its upper left.
    QRect geometry = front->geometry();
    for (int step = 0; step <= open.size(); ++step) {
        geometry.translate(cascadeStep, cascadeStep);
        if (available.isValid() && !available.contains(geometry.bottomRight()))
            geometry.moveTopLeft(available.topLeft() + QPoint(frame.left(), frame.top()));
        if (!taken(geometry.topLeft()))
            break;
    }

    return {geometry.x(), geometry.y(), geometry.width(), geometry.height(), false};
}

void WindowManager::windowActivated(MainWindow *window) {
    const int index = indexOf(window);
    if (index > 0)
        m_windows.move(index, 0);
}

void WindowManager::windowClosed(MainWindow *window) {
    const int index = indexOf(window);
    if (index < 0 || !m_windows.at(index).open)
        return;

    m_windows[index].open = false;
    // The last window to close says where the next launch opens. A full-screen
    // window's size is the screen's, not one to come back to.
    if (windows().isEmpty() && !(window->windowState() & Qt::WindowFullScreen)) {
        const QRect geometry = window->isMaximized() ? window->normalGeometry() : window->geometry();
        m_windows.at(index).backend->saveWindowGeometry(
            geometry.x(), geometry.y(), geometry.width(), geometry.height(), window->isMaximized());
    }
    emit windowCountChanged();

    // The writer answered this window's question, so the quit carries on to
    // the next window, as the Quit event Cmd+Q sent would have.
    if (window == m_quitWaitingOn) {
        m_quitWaitingOn = nullptr;
        QCoreApplication::postEvent(QCoreApplication::instance(), new QEvent(QEvent::Quit));
    }
}

// The window is still coming down, so its backend waits until it is gone.
void WindowManager::windowDestroyed(Backend *backend) {
    for (int index = 0; index < m_windows.size(); ++index) {
        if (m_windows.at(index).backend == backend) {
            m_windows.removeAt(index);
            break;
        }
    }
    backend->deleteLater();
}

void WindowManager::shareTextScale(Backend *source) {
    const qreal scale = source->textScale();
    if (qFuzzyCompare(scale, m_textScale))
        return;

    m_textScale = scale;
    for (const Window &entry : std::as_const(m_windows))
        entry.backend->setTextScale(scale);
    emit textScaleChanged(scale);
}

void WindowManager::shareEditorFont(Backend *source) {
    for (const Window &entry : std::as_const(m_windows))
        entry.backend->setEditorFont(source->editorFont());
}
