#pragma once

#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QUrl>

#include <optional>
#include <utility>

#include <functional>

class Backend;
class FindBar;
class MarkdownEditor;
class QAction;
class QDialog;
class QLabel;
class QMenu;
class QMessageBox;
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
    // Opens a file here, asking first about unsaved changes.
    void requestOpen(const QUrl &url);

signals:
    void activated();
    // The writer closed the window, and it is about to go.
    void closed();
    // A close the writer was asked about is off, and with it any quit that
    // was waiting on this window.
    void closeCancelled();

protected:
    void closeEvent(QCloseEvent *event) override;
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
    // The window around the editor takes the page's colours: its title bar,
    // background and status line.
    void applyTheme();
    void updateCommands();
    // Keeps the commands off while the dialog waits on the writer.
    void waitOn(QDialog *dialog);
    void askAboutUnsavedChanges();
    void completePending();
    void cancelPending();
    void showOpenPanel();
    void showSavePanel(const QUrl &suggestedUrl);
    void askAboutExternalChange(bool deleted, bool locallyModified);
    void showFontsPanel();

    // What the unsaved-changes sheet was asked for.
    enum class Pending { None, Close, Open };

    Backend *m_backend;
    MarkdownEditor *m_editor;
    FindBar *m_findBar;
    QWidget *m_statusLine;
    QLabel *m_status;
    QLabel *m_wordCount;
    QAction *m_fullScreen = nullptr;
    QList<QAction *> m_heldCommands;
    QSet<QObject *> m_waitingOn;
    QPointer<QMessageBox> m_unsavedSheet;
    QPointer<QMessageBox> m_changeSheet;
    QPointer<QDialog> m_fontsPanel;
    Pending m_pending = Pending::None;
    QUrl m_pendingUrl;
    // Only a save chosen in the sheet carries out the pending close or open,
    // never an auto-save that lands meanwhile.
    bool m_awaitingPendingSave = false;
    bool m_closeConfirmed = false;
    // An outside change reported while another question was open: deleted,
    // and changed here too.
    std::optional<std::pair<bool, bool>> m_deferredChange;
};
