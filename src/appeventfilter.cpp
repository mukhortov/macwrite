#include "appeventfilter.h"

#include <QFileOpenEvent>
#include <QWindow>

#include "backend.h"

AppEventFilter::AppEventFilter(Backend *backend, QWindow *window, QObject *parent)
    : QObject(parent), m_backend(backend), m_window(window) {}

bool AppEventFilter::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
    case QEvent::FileOpen:
        // Files opened from Finder or `open` arrive as events, not arguments.
        m_backend->openRequestedFile(static_cast<QFileOpenEvent *>(event)->url());
        return true;
    case QEvent::Quit:
        // QApplication closes only widget windows before it quits, so ask the
        // Macwrite window here. A window that stays open has unsaved changes
        // to ask about, and the quit waits for the answer.
        if (m_window && m_window->isVisible() && !m_window->close()) {
            event->ignore();
            return true;
        }
        break;
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}
