#include "appeventfilter.h"

#include <QFileOpenEvent>
#include <QGuiApplication>

#include "windowmanager.h"

AppEventFilter::AppEventFilter(WindowManager *windows, QObject *parent)
    : QObject(parent), m_windows(windows),
      m_active(QGuiApplication::applicationState() == Qt::ApplicationActive) {}

bool AppEventFilter::eventFilter(QObject *watched, QEvent *event) {
    if (watched != QCoreApplication::instance())
        return QObject::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::FileOpen:
        // Files opened from Finder or `open` arrive as events, not arguments.
        m_windows->openFile(static_cast<QFileOpenEvent *>(event)->url());
        return true;
    case QEvent::Quit:
        // QApplication would ask every window at once before it quits, so
        // ask them here, one at a time from the front. A window that stays
        // open has unsaved changes to ask about, and the quit waits for the
        // answer.
        if (!m_windows->closeAllWindows()) {
            event->ignore();
            return true;
        }
        break;
    case QEvent::ApplicationDeactivate:
        m_active = false;
        break;
    case QEvent::ApplicationActivate:
        // Qt reports a click on the Dock icon as one more activation, sent
        // even while Write is active. From another app, macOS activates
        // Write before it reports the click, so the click is always the
        // activation of an app already active. Switching to Write, as with
        // Cmd+Tab, is the first one alone and opens nothing.
        if (m_active)
            m_windows->reopen();
        m_active = true;
        break;
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}
