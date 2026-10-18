#pragma once

#include <QList>
#include <QMainWindow>

#include <functional>

class Backend;
class FindBar;
class MarkdownEditor;
class QAction;
class QLabel;
class QMenu;
struct WindowGeometry;

// One document's window: its menus, the find bar, the editor and the status
// line, with Backend as the document behind them.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Backend *backend, QWidget *parent = nullptr);

    MarkdownEditor *editor() const { return m_editor; }
    FindBar *findBar() const { return m_findBar; }
    // Off while a sheet or the Fonts panel waits on the writer: the native
    // menu bar matches its keys before the sheet would see them.
    bool commandsEnabled() const;
    void applyGeometry(const WindowGeometry &geometry);

signals:
    void activated();
    // A close the writer was asked about is off, and with it any quit that
    // was waiting on this window.
    void closeCancelled();

protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildMenus();
    QAction *addCommand(QMenu *menu, const QString &text, const QKeySequence &keys,
                        const std::function<void()> &run, bool held = true);
    // The field the writer is typing in: the find or replace field, or the
    // text, as the Edit menu works on anywhere on the Mac.
    QWidget *editTarget() const;
    void updateTitle();
    void updateStatus();
    void updateWordCount();
    void updateCommands();

    Backend *m_backend;
    MarkdownEditor *m_editor;
    FindBar *m_findBar;
    QLabel *m_status;
    QLabel *m_wordCount;
    QAction *m_fullScreen = nullptr;
    QList<QAction *> m_heldCommands;
};
