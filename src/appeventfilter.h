#pragma once

#include <QObject>
#include <QPointer>

class Backend;
class QWindow;

// Answers what the platform asks of the whole application rather than of one
// window: macOS opens files from Finder and quits from Cmd+Q this way.
class AppEventFilter : public QObject {
public:
    AppEventFilter(Backend *backend, QWindow *window, QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Backend *m_backend;
    QPointer<QWindow> m_window;
};
