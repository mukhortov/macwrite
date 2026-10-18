#include "mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QHBoxLayout>
#include <QTimer>
#include <QVBoxLayout>

#include "backend.h"
#include "findbar.h"
#include "macwindow.h"
#include "markdowneditor.h"

namespace {

// Presses a sheet's Don't Save button for Cmd+D. The key reaches the sheet
// from whichever of its buttons has the focus, as none of them takes it.
class DontSaveKey : public QObject {
public:
    DontSaveKey(QAbstractButton *button, QObject *parent) : QObject(parent), m_button(button) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_D && key->modifiers() == Qt::ControlModifier) {
                m_button->click();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QAbstractButton *m_button;
};

}

MainWindow::MainWindow(Backend *backend, QWidget *parent)
    : QMainWindow(parent), m_backend(backend) {
    setMinimumSize(720, 520);
    resize(1280, 820);

    auto *central = new QWidget(this);
    m_editor = new MarkdownEditor(m_backend, central);
    m_findBar = new FindBar(m_editor, m_backend, central);
    // A plain row rather than a QStatusBar, which brings a colour and a
    // border of its own and runs its words into the window's corners.
    m_statusLine = new QWidget(central);
    m_statusLine->setObjectName(QStringLiteral("statusLine"));
    m_statusLine->setAutoFillBackground(true);
    m_status = new QLabel(m_statusLine);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    m_wordCount = new QLabel(m_statusLine);
    m_wordCount->setObjectName(QStringLiteral("wordCountLabel"));
    QFont small = m_status->font();
    small.setPointSizeF(11);
    m_status->setFont(small);
    m_wordCount->setFont(small);
    auto *statusRow = new QHBoxLayout(m_statusLine);
    statusRow->setContentsMargins(14, 4, 14, 8);
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_wordCount);

    auto *column = new QVBoxLayout(central);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(m_findBar);
    column->addWidget(m_editor, 1);
    column->addWidget(m_statusLine);
    setCentralWidget(central);

    buildMenus();

    connect(m_backend, &Backend::fileUrlChanged, this, &MainWindow::updateTitle);
    connect(m_backend, &Backend::modifiedChanged, this, &MainWindow::updateTitle);
    connect(m_backend, &Backend::statusChanged, this, &MainWindow::updateStatus);
    connect(m_backend, &Backend::wordCountChanged, this, &MainWindow::updateWordCount);
    connect(m_backend, &Backend::themeColorsChanged, this, &MainWindow::applyTheme);
    connect(m_backend, &Backend::darkModeChanged, this, &MainWindow::applyTheme);
    connect(m_backend, &Backend::openDialogRequested, this, &MainWindow::showOpenPanel);
    connect(m_backend, &Backend::saveDialogRequested, this, &MainWindow::showSavePanel);
    connect(m_backend, &Backend::externalChangeDetected, this, &MainWindow::askAboutExternalChange);
    connect(m_backend, &Backend::saveSucceeded, this, [this]() {
        const bool confirmed = m_awaitingPendingSave;
        m_awaitingPendingSave = false;
        if (confirmed && m_pending != Pending::None)
            completePending();
    });
    // A failed save leaves the document as it was, so the close or open it
    // was for is off; a later save must not carry it out.
    connect(m_backend, &Backend::saveFailed, this, &MainWindow::cancelPending);
    updateTitle();
    updateStatus();
    updateWordCount();
    applyTheme();
    m_editor->setFocus();
}

void MainWindow::buildMenus() {
    // Every command is in the menu bar, which macOS draws at the top of the
    // screen, and each key belongs to its menu item alone: Qt finds a key
    // claimed twice ambiguous and runs neither.
    QMenu *file = menuBar()->addMenu(QStringLiteral("File"));
    addCommand(file, QStringLiteral("New Window"), QKeySequence(QStringLiteral("Ctrl+N")),
               [this]() { m_backend->newWindow(); });
    addCommand(file, QStringLiteral("Open…"), QKeySequence(QStringLiteral("Ctrl+O")),
               [this]() { m_backend->openDialog(); });
    file->addSeparator();
    addCommand(file, QStringLiteral("Save"), QKeySequence(QStringLiteral("Ctrl+S")),
               [this]() { m_backend->save(); });
    addCommand(file, QStringLiteral("Save As…"), QKeySequence(QStringLiteral("Ctrl+Shift+S")),
               [this]() { m_backend->saveAsDialog(); });
    file->addSeparator();
    addCommand(file, QStringLiteral("Print…"), QKeySequence(QStringLiteral("Ctrl+P")),
               [this]() { m_backend->printDocument(); });
    file->addSeparator();
    addCommand(file, QStringLiteral("Close Window"), QKeySequence(QStringLiteral("Ctrl+W")),
               [this]() { close(); });

    QMenu *edit = menuBar()->addMenu(QStringLiteral("Edit"));
    const auto onTarget = [this](auto command) {
        return [this, command]() {
            if (auto *field = qobject_cast<QLineEdit *>(editTarget()))
                command(field);
            else
                command(m_editor);
        };
    };
    addCommand(edit, QStringLiteral("Undo"), QKeySequence(QStringLiteral("Ctrl+Z")),
               onTarget([](auto *target) { target->undo(); }));
    QAction *redo = addCommand(edit, QStringLiteral("Redo"), QKeySequence(QStringLiteral("Ctrl+Shift+Z")),
                               onTarget([](auto *target) { target->redo(); }));
    edit->addSeparator();
    addCommand(edit, QStringLiteral("Cut"), QKeySequence(QStringLiteral("Ctrl+X")),
               onTarget([](auto *target) { target->cut(); }));
    addCommand(edit, QStringLiteral("Copy"), QKeySequence(QStringLiteral("Ctrl+C")),
               onTarget([](auto *target) { target->copy(); }));
    addCommand(edit, QStringLiteral("Paste"), QKeySequence(QStringLiteral("Ctrl+V")),
               onTarget([](auto *target) { target->paste(); }));
    addCommand(edit, QStringLiteral("Select All"), QKeySequence(QStringLiteral("Ctrl+A")),
               onTarget([](auto *target) { target->selectAll(); }));
    addCommand(edit, QStringLiteral("Copy as HTML"), QKeySequence(QStringLiteral("Ctrl+Shift+C")),
               [this]() { m_backend->copyAsHtml(); });
    edit->addSeparator();
    addCommand(edit, QStringLiteral("Find"), QKeySequence(QStringLiteral("Ctrl+F")),
               [this]() { m_findBar->open(false); });
    // Cmd+H belongs to macOS, which hides the app with it.
    addCommand(edit, QStringLiteral("Find and Replace"), QKeySequence(QStringLiteral("Ctrl+Alt+F")),
               [this]() { m_findBar->open(true); });
    edit->addSeparator();
    addCommand(edit, QStringLiteral("Bold"), QKeySequence(QStringLiteral("Ctrl+B")),
               [this]() { m_editor->toggleWrap(QStringLiteral("**")); });
    addCommand(edit, QStringLiteral("Italic"), QKeySequence(QStringLiteral("Ctrl+I")),
               [this]() { m_editor->toggleWrap(QStringLiteral("*")); });
    addCommand(edit, QStringLiteral("Strikethrough"), QKeySequence(QStringLiteral("Ctrl+Shift+X")),
               [this]() { m_editor->toggleWrap(QStringLiteral("~~")); });
    addCommand(edit, QStringLiteral("Link"), QKeySequence(QStringLiteral("Ctrl+K")),
               [this]() { m_editor->insertLink(); });

    QMenu *view = menuBar()->addMenu(QStringLiteral("View"));
    QAction *bigger = addCommand(view, QStringLiteral("Bigger"), QKeySequence(QStringLiteral("Ctrl+=")),
                                 [this]() { m_backend->increaseTextSize(); });
    addCommand(view, QStringLiteral("Smaller"), QKeySequence(QStringLiteral("Ctrl+-")),
               [this]() { m_backend->decreaseTextSize(); });
    addCommand(view, QStringLiteral("Actual Size"), QKeySequence(QStringLiteral("Ctrl+0")),
               [this]() { m_backend->resetTextSize(); });
    // The font is a family only: the size is Bigger and Smaller's, and bold
    // and italic are the Markdown's.
    QMenu *fonts = view->addMenu(QStringLiteral("Font"));
    fonts->menuAction()->setMenuRole(QAction::NoRole);
    m_fontChoice = new QActionGroup(fonts);
    for (const QString &name : Backend::editorFonts()) {
        QAction *item = addCommand(fonts, name, QKeySequence(),
                                   [this, name]() { m_backend->setEditorFont(name); });
        item->setCheckable(true);
        m_fontChoice->addAction(item);
    }
    tickFont();
    connect(m_backend, &Backend::editorFontChanged, this, &MainWindow::tickFont);
    view->addSeparator();
    // Cmd+Ctrl+F, as Qt spells Cmd as Ctrl and the Control key as Meta.
    m_fullScreen = addCommand(view, QStringLiteral("Enter Full Screen"),
                              QKeySequence(QStringLiteral("Ctrl+Meta+F")), [this]() {
                                  if (isFullScreen())
                                      showNormal();
                                  else
                                      showFullScreen();
                              });

    QMenu *window = menuBar()->addMenu(QStringLiteral("Window"));
    addCommand(window, QStringLiteral("Minimize"), QKeySequence(QStringLiteral("Ctrl+M")),
               [this]() { showMinimized(); }, false);
    addCommand(window, QStringLiteral("Zoom"), QKeySequence(), [this]() {
        if (isMaximized())
            showNormal();
        else
            showMaximized();
    }, false);

    // Ctrl++ is a different key to Qt, not a shifted Ctrl+=, and it is the
    // only one a layout that puts + on its own key can reach.
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl++")), this), &QShortcut::activated,
            bigger, &QAction::trigger);
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+Y")), this), &QShortcut::activated,
            redo, &QAction::trigger);
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+G")), this), &QShortcut::activated,
            this, [this]() {
                if (m_findBar->isOpen())
                    m_findBar->step(1);
            });
}

QAction *MainWindow::addCommand(QMenu *menu, const QString &text, const QKeySequence &keys,
                                const std::function<void()> &run, bool held) {
    QAction *action = menu->addAction(text);
    action->setShortcut(keys);
    // macOS moves an item to the application menu by its words alone, and
    // takes one starting with "Exit" for Quit.
    action->setMenuRole(QAction::NoRole);
    connect(action, &QAction::triggered, this, run);
    if (held)
        m_heldCommands.append(action);
    return action;
}

bool MainWindow::commandsEnabled() const {
    return m_waitingOn.isEmpty();
}

// A dialog is only taken down without an answer along with the window, so
// its answer is what ends the wait. An outside change that came meanwhile is
// asked about next, once the answer has been carried out.
void MainWindow::waitOn(QDialog *dialog) {
    m_waitingOn.insert(dialog);
    connect(dialog, &QDialog::finished, this, [this, dialog]() {
        m_waitingOn.remove(dialog);
        updateCommands();
        if (m_waitingOn.isEmpty() && m_deferredChange) {
            QTimer::singleShot(0, this, [this]() {
                if (!m_waitingOn.isEmpty() || !m_deferredChange || !isVisible())
                    return;
                const auto [deleted, locallyModified] = *m_deferredChange;
                m_deferredChange.reset();
                askAboutExternalChange(deleted, locallyModified);
            });
        }
    });
    updateCommands();
}

void MainWindow::updateCommands() {
    const bool enabled = commandsEnabled();
    for (QAction *action : std::as_const(m_heldCommands))
        action->setEnabled(enabled);
}

QWidget *MainWindow::editTarget() const {
    QWidget *focus = QApplication::focusWidget();
    if (focus == m_findBar->searchField() || focus == m_findBar->replaceField())
        return focus;
    return m_editor;
}

// The place is the window's inside, past its title bar, as it is saved.
void MainWindow::applyGeometry(const WindowGeometry &geometry) {
    if (geometry.x >= 0 && geometry.y >= 0)
        setGeometry(geometry.x, geometry.y, geometry.width, geometry.height);
    else
        resize(geometry.width, geometry.height);
    if (geometry.maximized)
        setWindowState(windowState() | Qt::WindowMaximized);
}

void MainWindow::updateTitle() {
    // macOS shows unsaved changes as the dot in the close button, and a
    // saved document's icon beside its name.
    setWindowTitle(m_backend->fileName() + QStringLiteral("[*]"));
    setWindowModified(m_backend->modified());
    const QUrl url = m_backend->fileUrl();
    setWindowFilePath(url.isLocalFile() ? QFileInfo(url.toLocalFile()).absoluteFilePath() : QString());
}

void MainWindow::updateStatus() {
    const QString status = m_backend->status();
    const int room = qMin(360, width() / 3);
    m_status->setText(m_status->fontMetrics().elidedText(status, Qt::ElideRight, room));
    m_status->setToolTip(status);
}

void MainWindow::applyTheme() {
    const QColor page(m_backend->themeBackground());
    const QColor muted(m_backend->darkMode() ? QStringLiteral("#909191") : QStringLiteral("#aeb1b5"));
    QPalette colours = palette();
    colours.setColor(QPalette::Window, page);
    setPalette(colours);
    QPalette line = m_statusLine->palette();
    line.setColor(QPalette::Window, page);
    line.setColor(QPalette::WindowText, muted);
    m_statusLine->setPalette(line);
    m_status->setPalette(line);
    m_wordCount->setPalette(line);
    MacWindow::setTitleBarColour(this, page);
}

void MainWindow::updateWordCount() {
    const int words = m_backend->wordCount();
    m_wordCount->setText(words == 1 ? QStringLiteral("1 Word") : QStringLiteral("%1 Words").arg(words));
}

void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        emit activated();
    else if (event->type() == QEvent::WindowStateChange && m_fullScreen)
        m_fullScreen->setText(isFullScreen() ? QStringLiteral("Exit Full Screen")
                                             : QStringLiteral("Enter Full Screen"));
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    // AppKit's window exists now, if it did not before.
    applyTheme();
    // The print dialog sits on the window it was asked from.
    m_backend->setParentWindow(windowHandle());
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateStatus();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Another question is open, so it is answered first: a close now would
    // stack a second sheet on it. A quit waiting on this window is called
    // off once the window manager has begun to wait.
    const bool askingAlready = m_unsavedSheet && m_waitingOn.contains(m_unsavedSheet);
    if (!m_closeConfirmed && !m_waitingOn.isEmpty() && !askingAlready) {
        event->ignore();
        QMetaObject::invokeMethod(this, &MainWindow::closeCancelled, Qt::QueuedConnection);
        return;
    }
    if (m_closeConfirmed || !m_backend->modified()) {
        QMainWindow::closeEvent(event);
        emit closed();
        return;
    }
    event->ignore();
    m_pending = Pending::Close;
    askAboutUnsavedChanges();
}

void MainWindow::requestOpen(const QUrl &url) {
    // A panel closed without a choice can still say so with an empty one.
    if (url.isEmpty())
        return;
    if (!m_backend->modified()) {
        m_backend->open(url);
        m_editor->setFocus();
        return;
    }
    m_pendingUrl = url;
    m_pending = Pending::Open;
    askAboutUnsavedChanges();
}

void MainWindow::askAboutUnsavedChanges() {
    if (m_unsavedSheet && m_waitingOn.contains(m_unsavedSheet))
        return;

    auto *sheet = new QMessageBox(this);
    sheet->setObjectName(QStringLiteral("unsavedChangesSheet"));
    sheet->setAttribute(Qt::WA_DeleteOnClose);
    sheet->setWindowModality(Qt::WindowModal);
    sheet->setText(QStringLiteral("Do you want to save the changes you made to “%1”?")
                       .arg(m_backend->fileName()));
    sheet->setInformativeText(QStringLiteral("Your changes will be lost if you don't save them."));
    // macOS names the Discard button "Don't Save".
    sheet->setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    sheet->setDefaultButton(QMessageBox::Save);
    sheet->setEscapeButton(QMessageBox::Cancel);
    // Qt draws this sheet itself, and gives Don't Save Cmd+Delete; every Mac
    // app answers it with Cmd+D.
    sheet->installEventFilter(new DontSaveKey(sheet->button(QMessageBox::Discard), sheet));
    connect(sheet, &QMessageBox::finished, this, [this, sheet]() {
        const QMessageBox::StandardButton answer = sheet->standardButton(sheet->clickedButton());
        if (answer == QMessageBox::Discard) {
            m_backend->discardRecovery();
            // A window that closes takes its document with it; anything else
            // the sheet was for goes on with auto-save as before.
            const bool closing = m_pending == Pending::Close;
            completePending();
            if (!closing)
                m_backend->holdAutoSave(false);
            return;
        }
        m_backend->holdAutoSave(false);
        if (answer == QMessageBox::Save) {
            m_awaitingPendingSave = true;
            m_backend->save();
        } else {
            cancelPending();
        }
    });
    m_unsavedSheet = sheet;
    // Discard must still mean discard, so nothing is auto-saved while the
    // writer decides.
    m_backend->holdAutoSave(true);
    waitOn(sheet);
    sheet->open();
}

void MainWindow::completePending() {
    const Pending action = m_pending;
    m_pending = Pending::None;
    if (action == Pending::Close) {
        m_closeConfirmed = true;
        close();
    } else if (action == Pending::Open) {
        m_backend->open(m_pendingUrl);
        m_editor->setFocus();
    }
}

void MainWindow::cancelPending() {
    const Pending action = m_pending;
    m_pending = Pending::None;
    m_awaitingPendingSave = false;
    if (action == Pending::Close)
        emit closeCancelled();
}

static const QStringList documentFilters = {QStringLiteral("Markdown files (*.md *.markdown)"),
                                            QStringLiteral("All files (*)")};

// Open is a panel of its own, as in other Mac apps; Save As is a sheet on
// the window it saves.
void MainWindow::showOpenPanel() {
    auto *panel = new QFileDialog(this, QStringLiteral("Open File"));
    panel->setObjectName(QStringLiteral("openPanel"));
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setAcceptMode(QFileDialog::AcceptOpen);
    panel->setFileMode(QFileDialog::ExistingFile);
    panel->setNameFilters(documentFilters);
    // macOS shows an application-modal panel only from exec(); shown any
    // other way it never appears, and still blocks every window. Without a
    // modality the panel stands on its own, as the Open panel of a Mac app does.
    panel->setWindowModality(Qt::NonModal);
    connect(panel, &QFileDialog::urlSelected, this, &MainWindow::requestOpen);
    waitOn(panel);
    panel->show();
}

void MainWindow::showSavePanel(const QUrl &suggestedUrl) {
    auto *panel = new QFileDialog(this, QStringLiteral("Save File"));
    panel->setObjectName(QStringLiteral("savePanel"));
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setAcceptMode(QFileDialog::AcceptSave);
    panel->setNameFilters(documentFilters);
    if (suggestedUrl.isLocalFile()) {
        const QFileInfo suggested(suggestedUrl.toLocalFile());
        panel->setDirectory(suggested.absolutePath());
        panel->selectFile(suggested.fileName());
    }
    connect(panel, &QFileDialog::urlSelected, m_backend, &Backend::saveAs);
    connect(panel, &QDialog::rejected, this, &MainWindow::cancelPending);
    waitOn(panel);
    panel->open();
}

void MainWindow::askAboutExternalChange(bool deleted, bool locallyModified) {
    if (m_changeSheet && m_waitingOn.contains(m_changeSheet))
        return;
    if (!m_waitingOn.isEmpty()) {
        m_deferredChange = std::pair{deleted, locallyModified};
        return;
    }

    auto *sheet = new QMessageBox(this);
    sheet->setObjectName(QStringLiteral("externalChangeSheet"));
    sheet->setAttribute(Qt::WA_DeleteOnClose);
    sheet->setWindowModality(Qt::WindowModal);
    sheet->setText(deleted ? QStringLiteral("File removed") : QStringLiteral("File changed"));
    sheet->setInformativeText(
        deleted ? QStringLiteral("This file was removed outside Write. Keep your text as an unsaved document?")
        : locallyModified
            ? QStringLiteral("This file changed outside Write. Reloading will discard your changes.")
            : QStringLiteral("This file changed outside Write."));
    QPushButton *keep = sheet->addButton(QStringLiteral("Keep Mine"), QMessageBox::RejectRole);
    keep->setObjectName(QStringLiteral("keepButton"));
    QPushButton *reload = sheet->addButton(QStringLiteral("Reload"), QMessageBox::AcceptRole);
    reload->setObjectName(QStringLiteral("reloadButton"));
    // Closing without a choice keeps the writer's text: it is the only copy
    // of what they wrote.
    sheet->setEscapeButton(keep);
    sheet->setDefaultButton(deleted ? keep : reload);
    connect(sheet, &QMessageBox::finished, this, [this, sheet, reload]() {
        if (sheet->clickedButton() == reload)
            m_backend->reloadFromDisk();
        else
            m_backend->keepExternalVersion();
    });
    m_changeSheet = sheet;
    waitOn(sheet);
    sheet->open();
}

void MainWindow::tickFont() {
    for (QAction *item : m_fontChoice->actions())
        item->setChecked(item->text() == m_backend->editorFont());
}
