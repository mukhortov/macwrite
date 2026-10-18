#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QShortcut>
#include <QStatusBar>
#include <QVBoxLayout>

#include "backend.h"
#include "findbar.h"
#include "markdowneditor.h"

MainWindow::MainWindow(Backend *backend, QWidget *parent)
    : QMainWindow(parent), m_backend(backend) {
    setMinimumSize(720, 520);
    resize(1280, 820);

    auto *central = new QWidget(this);
    m_editor = new MarkdownEditor(m_backend, central);
    m_findBar = new FindBar(m_editor, m_backend, central);
    auto *column = new QVBoxLayout(central);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(m_findBar);
    column->addWidget(m_editor, 1);
    setCentralWidget(central);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    m_wordCount = new QLabel(this);
    m_wordCount->setObjectName(QStringLiteral("wordCountLabel"));
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(m_status, 1);
    statusBar()->addPermanentWidget(m_wordCount);

    buildMenus();

    connect(m_backend, &Backend::fileUrlChanged, this, &MainWindow::updateTitle);
    connect(m_backend, &Backend::modifiedChanged, this, &MainWindow::updateTitle);
    connect(m_backend, &Backend::statusChanged, this, &MainWindow::updateStatus);
    connect(m_backend, &Backend::wordCountChanged, this, &MainWindow::updateWordCount);
    updateTitle();
    updateStatus();
    updateWordCount();
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
    addCommand(view, QStringLiteral("Font…"), QKeySequence(), []() {});
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
    return true;
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

void MainWindow::applyGeometry(const WindowGeometry &geometry) {
    if (geometry.x >= 0 && geometry.y >= 0)
        move(geometry.x, geometry.y);
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
    // The print dialog sits on the window it was asked from.
    m_backend->setParentWindow(windowHandle());
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateStatus();
}
