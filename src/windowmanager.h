#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QUrl>
#include <QVariantMap>

#include <memory>

class Backend;
class QMenuBar;
class QQmlContext;
class QQmlEngine;
class QQuickWindow;

// Runs every Macwrite window in this one process. Each window has a Backend of
// its own, with its document, file watcher, auto-save, and recovery slot,
// while the text size and font are one preference all windows share.
class WindowManager : public QObject {
    Q_OBJECT

public:
    WindowManager(QQmlEngine *engine, const QUrl &mainQml, QObject *parent = nullptr);
    ~WindowManager() override;

    // The windows still open, the most recently active first.
    QList<QQuickWindow *> windows() const;
    QQuickWindow *activeWindow() const;
    Backend *activeBackend() const;
    Backend *backendFor(const QQuickWindow *window) const;

    QQuickWindow *newWindow();
    // The first windows at launch: one for each draft a crash left behind, or
    // a blank one.
    void openStartupWindows();
    // A file from Finder, `open`, or the command line comes forward in the
    // window that has it, fills an untouched blank window, or opens a window
    // of its own.
    void openFile(const QUrl &url);
    // Asks every window with unsaved changes before any window closes. False
    // while a window waits for an answer; the quit is retried once it closes,
    // and called off if the writer cancels.
    bool closeAllWindows();
    // The Dock icon was clicked.
    void reopen();

    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textScale; }

signals:
    void textScaleChanged(qreal textScale);
    void windowCountChanged();

private slots:
    void closeCancelled();

private:
    friend class MacwriteTest;

    // A window stays listed after it closes, until it is gone, so the manager
    // can still take it down along with its backend.
    struct Window {
        QPointer<QQuickWindow> window;
        Backend *backend = nullptr;
        QQmlContext *context = nullptr;
        bool open = true;
    };

    void buildNoWindowMenuBar();
    // Needs a window open to step from.
    QVariantMap cascadedGeometry() const;
    void windowActivated(QQuickWindow *window);
    void windowClosed(QQuickWindow *window);
    void windowDestroyed(Backend *backend);
    void shareTextScale(Backend *source);
    void shareEditorFont(Backend *source);
    int indexOf(const QQuickWindow *window) const;

    QQmlEngine *m_engine;
    QQmlComponent m_component;
    QList<Window> m_windows;
    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    // The window whose unsaved changes a quit is waiting on.
    QPointer<QQuickWindow> m_quitWaitingOn;
    // What macOS shows at the top of the screen once every window has closed.
    std::unique_ptr<QMenuBar> m_noWindowMenuBar;
};
