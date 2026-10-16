#pragma once

#include <QObject>

class WindowManager;

// Answers what the platform asks of the whole application rather than of one
// window: macOS opens files from Finder, quits from Cmd+Q, and reopens from
// the Dock this way.
class AppEventFilter : public QObject {
public:
    explicit AppEventFilter(WindowManager *windows, QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    WindowManager *m_windows;
    bool m_active;
};
