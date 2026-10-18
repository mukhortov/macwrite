#include <QtTest>
#include <QFont>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QStandardPaths>
#include <QClipboard>
#include <QMimeData>
#include <QWheelEvent>
#include <QAction>
#include <QMenuBar>
#include <QPainter>
#include <QRandomGenerator>
#include <QLineEdit>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QShortcut>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QInputMethodEvent>
#include <QTextFrame>

#include <functional>
#include "cli.h"
#include "findbar.h"

#include "appeventfilter.h"
#include "backend.h"
#include "mainwindow.h"
#include "markdowneditor.h"
#include "markdownedits.h"
#include "markdownhighlighter.h"
#include "plaintextlayout.h"
#include "windowmanager.h"

// Stands in for QApplication at the end of the event filters: a quit that
// gets this far was accepted, and stopping it here keeps the test running.
class QuitRecorder : public QObject {
public:
    int acceptedQuits = 0;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == QCoreApplication::instance() && event->type() == QEvent::Quit) {
            if (event->isAccepted())
                ++acceptedQuits;
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};

// Text that exercises line breaking: wrapped paragraphs, words too long for a
// line, tabs, right-to-left and unspaced scripts, line separators, emoji.
static QString layoutSample() {
    return QStringLiteral(
        "# A heading for the sample\n"
        "\n"
        "A paragraph long enough to wrap over several lines at any of the widths "
        "the tests use, with **bold**, *italic*, and `code` spans along the way, "
        "so that the highlighter's formats change the widths of the words.\n"
        "- a list item\n"
        "  - a nested item that is also long enough to wrap onto a second line here\n"
        "https://example.com/a/very/long/address/that/has/no/spaces/at/all/and/keeps/going/past/the/edge/of/any/line\n"
        "\tTabbed\tcolumns\tline\n"
        "مرحبا بالعالم، هذا سطر من اليمين إلى اليسار يكفي طوله ليلتف على أكثر من سطر واحد\n"
        "שלום עולם mixed with English words in one line\n"
        "日本語のテキストは単語の区切りがないので、どこでも折り返すことができます。日本語のテキストは単語の区切りがありません。\n"
        "Emoji 😀🎉 in a line 👩‍👩‍👧‍👦\n"
        "```\n"
        "code fence line\n"
        "```\n"
        "A line with trailing spaces                                                                 \n"
        "first part second part after a line separator\n"
        "> a quote\n"
        "\n"
        "\n"
        "Last line");
}

// Sets up a document the way TextEdit does, then fills it the way Write does.
static void setUpLayoutDocument(QTextDocument *document, const QFont &font, qreal width,
                                qreal margin, bool designMetrics) {
    document->setDefaultFont(font);
    document->setDocumentMargin(margin);
    QTextOption option = document->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setUseDesignMetrics(designMetrics);
    document->setDefaultTextOption(option);
    document->setTextWidth(width);
    // As TextEdit does, so an undo cannot take the margin back.
    document->clearUndoRedoStacks();
}

static void fillLayoutDocument(QTextDocument *document, const QString &text) {
    QTextBlockFormat format;
    format.setLineHeight(140, QTextBlockFormat::ProportionalHeight);
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    cursor.setBlockFormat(format);
    cursor.insertText(text);
    cursor.endEditBlock();
}

static bool sameRect(const QRectF &a, const QRectF &b) {
    return a.x() == b.x() && a.y() == b.y() && a.width() == b.width() && a.height() == b.height();
}

static QString describe(const QRectF &rect) {
    return QStringLiteral("(%1, %2 %3x%4)").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}

// Where two documents with the same text are laid out differently, or an
// empty string. With hit tests, points all over the page must find the same
// position in both.
static QString layoutDifference(QTextDocument *qt, QTextDocument *ours, bool hitTests) {
    QAbstractTextDocumentLayout *expected = qt->documentLayout();
    QAbstractTextDocumentLayout *actual = ours->documentLayout();
    const QSizeF size = expected->documentSize();
    if (size != actual->documentSize()) {
        return QStringLiteral("document size %1x%2, Qt %3x%4")
            .arg(actual->documentSize().width()).arg(actual->documentSize().height())
            .arg(size.width()).arg(size.height());
    }
    if (qt->blockCount() != ours->blockCount())
        return QStringLiteral("block count");
    if (!sameRect(expected->frameBoundingRect(qt->rootFrame()), actual->frameBoundingRect(ours->rootFrame())))
        return QStringLiteral("root frame");

    for (QTextBlock a = qt->begin(), b = ours->begin(); a.isValid(); a = a.next(), b = b.next()) {
        const QRectF expectedRect = expected->blockBoundingRect(a);
        const QRectF actualRect = actual->blockBoundingRect(b);
        if (!sameRect(expectedRect, actualRect)) {
            return QStringLiteral("block %1 at %2, Qt %3")
                .arg(a.blockNumber()).arg(describe(actualRect), describe(expectedRect));
        }
        if (a.layout()->position() != b.layout()->position())
            return QStringLiteral("block %1 layout position").arg(a.blockNumber());
        if (a.layout()->lineCount() != b.layout()->lineCount()) {
            return QStringLiteral("block %1 has %2 lines, Qt %3").arg(a.blockNumber())
                .arg(b.layout()->lineCount()).arg(a.layout()->lineCount());
        }
        for (int i = 0; i < a.layout()->lineCount(); ++i) {
            const QTextLine x = a.layout()->lineAt(i);
            const QTextLine y = b.layout()->lineAt(i);
            if (x.position() != y.position() || x.textStart() != y.textStart()
                    || x.textLength() != y.textLength() || x.width() != y.width()
                    || x.naturalTextWidth() != y.naturalTextWidth())
                return QStringLiteral("block %1 line %2").arg(a.blockNumber()).arg(i);
        }
    }

    if (hitTests) {
        for (qreal y = -9.3; y < size.height() + 12; y += 1.7) {
            for (qreal x = -11.1; x < size.width() + 12; x += 19.3) {
                for (const Qt::HitTestAccuracy accuracy : {Qt::FuzzyHit, Qt::ExactHit}) {
                    const int want = expected->hitTest(QPointF(x, y), accuracy);
                    const int got = actual->hitTest(QPointF(x, y), accuracy);
                    if (want != got) {
                        return QStringLiteral("hit test at %1,%2 (%3) found %4, Qt %5")
                            .arg(x).arg(y).arg(accuracy == Qt::ExactHit ? "exact" : "fuzzy")
                            .arg(got).arg(want);
                    }
                }
            }
        }
    }
    return {};
}

// A document with a caret or a selection, for the editing rules alone.
struct EditCase {
    QTextDocument document;
    QTextCursor cursor{&document};

    void load(const QString &text, int anchor, int position = -1) {
        document.setPlainText(text);
        document.clearUndoRedoStacks();
        cursor = QTextCursor(&document);
        cursor.setPosition(anchor);
        if (position >= 0)
            cursor.setPosition(position, QTextCursor::KeepAnchor);
    }
    QString text() const { return document.toPlainText(); }
    int caret() const { return cursor.position(); }
    // Return in a document with no code fences.
    void returnKey(bool softBreak = false) { MarkdownEdits::smartReturn(cursor, softBreak, false); }
    // Backspace as the editor presses it: the paragraph break first, else a character.
    void backspaceKey() {
        if (!MarkdownEdits::deleteParagraphBreakBehind(cursor))
            cursor.deletePreviousChar();
    }
};

// A shown MarkdownEditor on a Backend of its own, under a field that can take
// the focus from it.
struct EditorFixture {
    Backend backend;
    QWidget window;
    MarkdownEditor *editor = nullptr;
    QLineEdit *other = nullptr;

    EditorFixture() {
        editor = new MarkdownEditor(&backend, &window);
        other = new QLineEdit(&window);
        auto *column = new QVBoxLayout(&window);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        column->addWidget(other);
        column->addWidget(editor, 1);
        window.resize(1280, 820);
    }
    bool show() {
        window.show();
        window.activateWindow();
        return QTest::qWaitForWindowActive(&window);
    }
    void load(const QString &text, int caret) {
        editor->setPlainText(text);
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(caret);
        editor->setTextCursor(cursor);
    }
    QString text() const { return editor->toPlainText(); }
    PlainTextLayout *layout() const {
        return qobject_cast<PlainTextLayout *>(editor->document()->documentLayout());
    }
};

// Whether an image holds a pixel of exactly this colour.
static bool holdsColour(const QImage &image, const QColor &colour) {
    const QRgb wanted = colour.rgb();
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if ((line[x] | 0xff000000) == wanted)
                return true;
        }
    }
    return false;
}

// A MarkdownEditor with the find bar above it, on a Backend of their own.
struct FindFixture {
    Backend backend;
    QWidget window;
    MarkdownEditor *editor = nullptr;
    FindBar *findBar = nullptr;

    FindFixture() {
        editor = new MarkdownEditor(&backend, &window);
        findBar = new FindBar(editor, &backend, &window);
        auto *column = new QVBoxLayout(&window);
        column->setContentsMargins(0, 0, 0, 0);
        column->addWidget(findBar);
        column->addWidget(editor, 1);
        window.resize(1000, 700);
    }
    bool show() {
        window.show();
        window.activateWindow();
        return QTest::qWaitForWindowActive(&window);
    }
    void load(const QString &text) { editor->setPlainText(text); }
    QString text() const { return editor->toPlainText(); }
    // How a match starting there is highlighted: "current", "other", or not at all.
    QString drawnAs(int start) const {
        const QColor current(backend.darkMode() ? QStringLiteral("#b36b20") : QStringLiteral("#ffad42"));
        for (const QTextEdit::ExtraSelection &selection : editor->extraSelections()) {
            if (selection.cursor.selectionStart() == start)
                return selection.format.background().color() == current ? QStringLiteral("current")
                                                                        : QStringLiteral("other");
        }
        return {};
    }
};

// A shown MainWindow on a Backend of its own.
struct WindowFixture {
    Backend backend;
    MainWindow window{&backend};

    bool show() {
        window.show();
        window.activateWindow();
        return QTest::qWaitForWindowActive(&window);
    }
    MarkdownEditor *editor() const { return window.editor(); }
    void load(const QString &text, int caret = 0) {
        editor()->setPlainText(text);
        QTextCursor cursor = editor()->textCursor();
        cursor.setPosition(caret);
        editor()->setTextCursor(cursor);
    }
    void select(int start, int end) {
        QTextCursor cursor = editor()->textCursor();
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        editor()->setTextCursor(cursor);
    }
    QString text() const { return editor()->toPlainText(); }
    // The menu command with this text.
    QAction *command(const QString &text) const {
        const QList<QAction *> actions = window.findChildren<QAction *>();
        for (QAction *action : actions) {
            if (action->text() == text)
                return action;
        }
        return nullptr;
    }
    // Runs a command as choosing it from the menu bar does, without its key.
    bool choose(const QString &text) const {
        QAction *action = command(text);
        if (!action || !action->isEnabled())
            return false;
        action->trigger();
        return true;
    }
};

// The window's sheet of this name once it is up, or nothing.
static QMessageBox *sheet(QWidget *window, const QString &name) {
    auto *box = window->findChild<QMessageBox *>(name);
    return box && box->isVisible() ? box : nullptr;
}

class WriteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_settingsDirectory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好世界")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("中文 iPad AI")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("foo中bar")), 3);
        QCOMPARE(Backend::countWords(QStringLiteral("日本語テスト")), 6);
        QCOMPARE(Backend::countWords(QStringLiteral("你好，世界。")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("コーヒー")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("か\u3099")), 1); // が, decomposed
        QCOMPARE(Backend::countWords(QStringLiteral("𠀀𠀁 foo")), 3);
        QCOMPARE(Backend::countWords(QStringLiteral("안녕하세요 세계")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void answersHelpBeforeOpeningAWindow() {
        const QString usage = Cli::usage();
        QVERIFY(usage.contains(QStringLiteral("Write [FILE]")));
        QVERIFY(usage.contains(QStringLiteral("-h, --help")));
        QVERIFY(usage.contains(QStringLiteral("-v, --version")));

        const QString version = QStringLiteral("1.2.3");
        const auto answer = [&](const QStringList &arguments) {
            return Cli::handleArguments(QStringList{QStringLiteral("Write")} + arguments,
                                        version);
        };
        QCOMPARE(answer({QStringLiteral("--help")}), std::optional<int>(0));
        QCOMPARE(answer({QStringLiteral("-h")}), std::optional<int>(0));
        QCOMPARE(answer({QStringLiteral("--version")}), std::optional<int>(0));
        QCOMPARE(answer({QStringLiteral("-v")}), std::optional<int>(0));

        // Everything else is the app's: no arguments, a file, and Qt's own
        // options, which a leading dash must not turn into an error.
        QCOMPARE(answer({}), std::nullopt);
        QCOMPARE(answer({QStringLiteral("draft.md")}), std::nullopt);
        QCOMPARE(answer({QStringLiteral("-platform"), QStringLiteral("offscreen")}),
                 std::nullopt);
        QCOMPARE(answer({QStringLiteral("-notes.md")}), std::nullopt);
    }

    void printsInPointsNotPixels() {
        QFont editorFont;
        editorFont.setPixelSize(20);
        const QFont printed = Backend::printFont(editorFont, 96.0);
        QCOMPARE(printed.pixelSize(), -1);
        QCOMPARE(printed.pointSizeF(), 15.0);
        QCOMPARE(Backend::printFont(editorFont, 0.0).pointSizeF(), 15.0);

        QFont pointFont;
        pointFont.setPointSizeF(11.0);
        QCOMPARE(Backend::printFont(pointFont, 144.0).pointSizeF(), 11.0);
    }

    void restylesALongDocumentAtOnce() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("long.md"));
        QByteArray contents;
        for (int line = 0; line < 20000; ++line)
            contents += "Some `code` and **bold** text on line " + QByteArray::number(line) + "\n";
        writeFile(path, contents);

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        qobject_cast<QWidget *>(editor.data())->resize(600, 400);
        backend.open(QUrl::fromLocalFile(path));
        QTextDocument *document = qobject_cast<QTextEdit *>(editor.data())->document();
        const qreal height = document->documentLayout()->documentSize().height();
        const auto codeBackground = [document]() {
            for (const QTextLayout::FormatRange &range : document->lastBlock().previous().layout()->formats()) {
                if (range.format.hasProperty(QTextFormat::BackgroundBrush))
                    return range.format.background().color();
            }
            return QColor();
        };
        const QColor darkCode = codeBackground();
        QVERIFY(darkCode.isValid());

        // Each restyled line is laid out on its own, not again with every line
        // below it, which over this many lines would take many seconds.
        QElapsedTimer timer;
        timer.start();
        backend.setDarkMode(false);
        QVERIFY2(timer.elapsed() < 3000, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(codeBackground().isValid() && codeBackground() != darkCode);
        QCOMPARE(document->documentLayout()->documentSize().height(), height);
    }

    // The layout Write installs puts every line exactly where Qt's own would,
    // and finds the same character under every point.
    void plainTextLayoutMatchesQtsLayout() {
        QFont system;
        system.setPixelSize(18);
        QFont mono(QStringLiteral("Menlo"));
        mono.setPixelSize(15);
        mono.setStyleStrategy(QFont::PreferNoShaping);
        QFont serif(QStringLiteral("Georgia"));
        serif.setPointSizeF(17.5);

        int compared = 0;
        for (const QFont &font : {system, mono, serif}) {
            for (const qreal width : {300.0, 517.3, 900.0}) {
                for (const qreal margin : {0.0, 4.0}) {
                    for (const bool designMetrics : {false, true}) {
                        QTextDocument qt;
                        QTextDocument ours;
                        ours.setDocumentLayout(new PlainTextLayout(&ours));
                        for (QTextDocument *document : {&qt, &ours}) {
                            setUpLayoutDocument(document, font, width, margin, designMetrics);
                            new MarkdownHighlighter(document);
                            fillLayoutDocument(document, layoutSample());
                        }
                        const QString difference = layoutDifference(&qt, &ours, compared++ % 4 == 0);
                        QVERIFY2(difference.isEmpty(),
                                 qPrintable(QStringLiteral("%1 at width %2, margin %3, design metrics %4: %5")
                                                .arg(font.family()).arg(width).arg(margin)
                                                .arg(designMetrics).arg(difference)));
                    }
                }
            }
        }
    }

    void plainTextLayoutMatchesQtsLayoutWithBlockFormats() {
        QFont font;
        font.setPixelSize(16);
        QTextDocument qt;
        QTextDocument ours;
        ours.setDocumentLayout(new PlainTextLayout(&ours));
        for (QTextDocument *document : {&qt, &ours}) {
            setUpLayoutDocument(document, font, 420, 4, false);
            fillLayoutDocument(document, layoutSample());
            int index = 0;
            for (QTextBlock block = document->begin(); block.isValid(); block = block.next(), ++index) {
                QTextBlockFormat format;
                if (index % 3 == 0)
                    format.setTopMargin(6);
                if (index % 4 == 0)
                    format.setBottomMargin(4.5);
                if (index % 5 == 0)
                    format.setIndent(1);
                if (index % 2 == 0)
                    format.setTextIndent(12.3);
                if (index % 7 == 0)
                    format.setAlignment(Qt::AlignHCenter);
                if (index % 7 == 1)
                    format.setAlignment(Qt::AlignRight);
                if (index % 6 == 0)
                    format.setLineHeight(80, QTextBlockFormat::ProportionalHeight);
                if (index % 6 == 3)
                    format.setLineHeight(30, QTextBlockFormat::MinimumHeight);
                if (index % 8 == 5)
                    format.setRightMargin(25);
                QTextCursor(block).mergeBlockFormat(format);
            }
        }
        const QString difference = layoutDifference(&qt, &ours, true);
        QVERIFY2(difference.isEmpty(), qPrintable(difference));
    }

    // Each edit lays out only the lines it touched, and every line still ends
    // up where Qt's layout, which lays out from the edit down, puts it.
    void plainTextLayoutFollowsEditsLikeQt() {
        QFont font;
        font.setPixelSize(17);
        QTextDocument qt;
        QTextDocument ours;
        ours.setDocumentLayout(new PlainTextLayout(&ours));
        QString text;
        for (int copy = 0; copy < 3; ++copy)
            text += layoutSample() + QLatin1Char('\n');
        for (QTextDocument *document : {&qt, &ours}) {
            setUpLayoutDocument(document, font, 480, 0, false);
            new MarkdownHighlighter(document);
            fillLayoutDocument(document, text);
        }
        QVERIFY(layoutDifference(&qt, &ours, false).isEmpty());

        const QStringList insertions = {
            QStringLiteral("a"), QStringLiteral("word "), QStringLiteral("\n"),
            QStringLiteral("two\nlines"), QStringLiteral("\n\n"), QStringLiteral("```"),
            QStringLiteral("# "), QStringLiteral("مرحبا "), QStringLiteral("\t"), QStringLiteral("**"),
            QStringLiteral("averyveryveryveryveryveryveryveryveryveryveryveryverylongword")};
        QRandomGenerator random(20261017);
        for (int step = 0; step < 400; ++step) {
            const int length = qt.characterCount() - 1;
            const int position = int(random.bounded(length + 1));
            const int end = qMin(length, position + int(random.bounded(60)));
            const QString insertion = insertions.at(random.bounded(int(insertions.size())));
            const int choice = int(random.bounded(100));
            const auto select = [position, end](QTextDocument *document) {
                QTextCursor cursor(document);
                cursor.setPosition(position);
                cursor.setPosition(end, QTextCursor::KeepAnchor);
                return cursor;
            };
            std::function<void(QTextDocument *)> edit;
            QString action;
            if (choice < 40) {
                action = QStringLiteral("insert %1 at %2").arg(insertion).arg(position);
                edit = [&](QTextDocument *document) {
                    QTextCursor cursor(document);
                    cursor.setPosition(position);
                    cursor.insertText(insertion);
                };
            } else if (choice < 62) {
                action = QStringLiteral("remove %1 to %2").arg(position).arg(end);
                edit = [&](QTextDocument *document) { select(document).removeSelectedText(); };
            } else if (choice < 72) {
                action = QStringLiteral("replace %1 to %2 with %3").arg(position).arg(end).arg(insertion);
                edit = [&](QTextDocument *document) { select(document).insertText(insertion); };
            } else if (choice < 80) {
                QTextBlockFormat format;
                format.setLineHeight(100 + 20 * random.bounded(3), QTextBlockFormat::ProportionalHeight);
                if (random.bounded(2))
                    format.setBottomMargin(random.bounded(8.0));
                // Lines that do not wrap can be wider than the page.
                if (random.bounded(3) == 0)
                    format.setNonBreakableLines(random.bounded(2));
                action = QStringLiteral("format blocks %1 to %2").arg(position).arg(end);
                edit = [&, format](QTextDocument *document) { select(document).mergeBlockFormat(format); };
            } else if (choice < 86) {
                QTextCharFormat format;
                format.setFontPointSize(10 + random.bounded(20));
                action = QStringLiteral("resize text %1 to %2").arg(position).arg(end);
                edit = [&, format](QTextDocument *document) { select(document).mergeCharFormat(format); };
            } else if (choice < 93) {
                action = QStringLiteral("undo");
                edit = [](QTextDocument *document) { document->undo(); };
            } else if (choice < 97) {
                const qreal width = 200 + random.bounded(700.0);
                action = QStringLiteral("width %1").arg(width);
                edit = [width](QTextDocument *document) { document->setTextWidth(width); };
            } else {
                const qreal margin = random.bounded(8);
                action = QStringLiteral("margin %1").arg(margin);
                // TextEdit sets its margin before there is anything to undo,
                // and Qt's layout does not follow an undo that takes it back.
                edit = [margin](QTextDocument *document) {
                    document->setDocumentMargin(margin);
                    document->clearUndoRedoStacks();
                };
            }
            edit(&qt);
            edit(&ours);
            const QString difference = layoutDifference(&qt, &ours, step % 50 == 49);
            QVERIFY2(difference.isEmpty(),
                     qPrintable(QStringLiteral("step %1, %2: %3").arg(step).arg(action, difference)));
        }
    }

    // An editor leaves the painting to its document's layout, so ours has to
    // paint what Qt's would: the text, the selection, highlights and the caret.
    void plainTextLayoutDrawsLikeQt() {
        QFont font;
        font.setPixelSize(17);
        QTextDocument qt;
        QTextDocument ours;
        ours.setDocumentLayout(new PlainTextLayout(&ours));
        for (QTextDocument *document : {&qt, &ours}) {
            setUpLayoutDocument(document, font, 480, 4, false);
            new MarkdownHighlighter(document);
            fillLayoutDocument(document, layoutSample());
        }

        const QString text = qt.toPlainText();
        const int arabic = int(text.indexOf(QStringLiteral("هذا")));
        const int hebrew = int(text.indexOf(QStringLiteral("עולם")));
        QVERIFY(arabic > 0 && hebrew > 0);
        const int from = qt.findBlockByNumber(2).position() + 5;
        const int to = qt.findBlockByNumber(5).position() + 3;
        const QSizeF size = qt.documentLayout()->documentSize();
        const QRectF middleThird(0, size.height() / 3, size.width(), size.height() / 3);

        struct Case {
            const char *name;
            int cursor;
            QList<std::tuple<int, int, QColor>> selections;
            QRectF clip;
        };
        const QList<Case> cases = {
            {"no caret", -1, {}, {}},
            {"caret at the start", 0, {}, {}},
            {"caret in Arabic", arabic + 2, {}, {}},
            {"caret in Hebrew", hebrew + 1, {}, {}},
            {"caret at the end", qt.characterCount() - 1, {}, {}},
            {"selection", to, {{from, to, QColor(QStringLiteral("#264f78"))}}, {}},
            {"highlights", -1,
             {{10, 14, QColor(QStringLiteral("#725b18"))}, {40, 47, QColor(QStringLiteral("#b36b20"))}},
             {}},
            {"middle third", from, {{from, to, QColor(QStringLiteral("#264f78"))}}, middleThird},
        };

        for (const Case &test : cases) {
            QList<QImage> images;
            for (QTextDocument *document : {&qt, &ours}) {
                QAbstractTextDocumentLayout::PaintContext context;
                context.cursorPosition = test.cursor;
                context.palette.setColor(QPalette::Text, QColor(QStringLiteral("#d6d6d6")));
                context.palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#264f78")));
                context.clip = test.clip;
                for (const auto &[start, end, color] : test.selections) {
                    QAbstractTextDocumentLayout::Selection selection;
                    selection.cursor = QTextCursor(document);
                    selection.cursor.setPosition(start);
                    selection.cursor.setPosition(end, QTextCursor::KeepAnchor);
                    selection.format.setBackground(color);
                    context.selections.append(selection);
                }
                QImage image(size.toSize() + QSize(1, 1), QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::white);
                QPainter painter(&image);
                document->documentLayout()->draw(&painter, context);
                painter.end();
                images.append(image);
            }
            QVERIFY2(images.at(0) == images.at(1), test.name);
        }
    }

    // The editor centres its column and leaves room above and below it with
    // padding, which moves the lines without breaking them again.
    void movesBlocksWithoutLayingThemOutWhenPaddingChanges() {
        QFont font;
        font.setPixelSize(16);
        QTextDocument qt;
        QTextDocument ours;
        auto *layout = new PlainTextLayout(&ours);
        ours.setDocumentLayout(layout);
        QString text;
        for (int copy = 0; copy < 5; ++copy)
            text += layoutSample() + QLatin1Char('\n');
        for (QTextDocument *document : {&qt, &ours}) {
            setUpLayoutDocument(document, font, 600, 0, false);
            fillLayoutDocument(document, text);
        }

        // Qt's layout gets the same space as root frame margins, around a page
        // wider by the horizontal part of it, so the lines break alike.
        const QList<QMarginsF> paddings = {QMarginsF(40, 0, 0, 0), QMarginsF(0, 0, 40, 0),
                                           QMarginsF(0, 80, 40, 220), QMarginsF(17, 42, 23, 220)};
        for (const QMarginsF &padding : paddings) {
            QTextFrameFormat format = qt.rootFrame()->frameFormat();
            format.setLeftMargin(padding.left());
            format.setTopMargin(padding.top());
            format.setRightMargin(padding.right());
            format.setBottomMargin(padding.bottom());
            qt.rootFrame()->setFrameFormat(format);
            qt.setTextWidth(600 + padding.left() + padding.right());

            layout->setPadding(padding);
            QCOMPARE(layout->lastLayoutBlockCount(), 0);
            const QString difference = layoutDifference(&qt, &ours, true);
            QVERIFY2(difference.isEmpty(), qPrintable(difference));
        }
    }


    // The editor asks Backend in plain C++ types.
    void answersEditorsInPlainTypes() {
        Backend backend;
        QTextDocument document;
        backend.attachDocument(&document);
        QTextCursor(&document).insertText(QStringLiteral("a **b** c"));

        const QList<TextRange> ranges = backend.hiddenRangesAt(3);
        QCOMPARE(ranges.size(), 2);
        QCOMPARE(ranges.at(0).start, 2);
        QCOMPARE(ranges.at(0).end, 4);
        QCOMPARE(ranges.at(1).start, 5);
        QCOMPARE(ranges.at(1).end, 7);

        const std::optional<EmphasisSpan> bold = backend.emphasisSpanAt(4, QStringLiteral("**"));
        QVERIFY(bold);
        QCOMPARE(bold->start, 2);
        QCOMPARE(bold->end, 7);
        QCOMPARE(bold->contentStart, 4);
        QCOMPARE(bold->contentEnd, 5);
        QCOMPARE(bold->textEnd, 5);
        QVERIFY(!backend.emphasisSpanAt(0, QStringLiteral("*")));

        QSettings().remove(QStringLiteral("window"));
        const WindowGeometry geometry = backend.windowGeometry();
        QCOMPARE(geometry.x, -1);
        QCOMPARE(geometry.y, -1);
        QCOMPARE(geometry.width, 1280);
        QCOMPARE(geometry.height, 820);
        QCOMPARE(geometry.maximized, false);
    }

    void markdownEditsWrapAndUnwrap() {
        EditCase edit;
        edit.load(QStringLiteral("a b c"), 2, 3);
        MarkdownEdits::toggleWrap(edit.cursor, QStringLiteral("**"), std::nullopt);
        QCOMPARE(edit.text(), QStringLiteral("a **b** c"));
        QCOMPARE(edit.cursor.selectedText(), QStringLiteral("b"));
        MarkdownEdits::toggleWrap(edit.cursor, QStringLiteral("**"), std::nullopt);
        QCOMPARE(edit.text(), QStringLiteral("a b c"));
        QCOMPARE(edit.cursor.selectedText(), QStringLiteral("b"));

        // The "*" of "**" is not an italic marker, so italic wraps instead.
        edit.load(QStringLiteral("**bold**"), 2, 6);
        MarkdownEdits::toggleWrap(edit.cursor, QStringLiteral("*"), std::nullopt);
        QCOMPARE(edit.text(), QStringLiteral("***bold***"));

        // With nothing selected the span around the caret is what counts: at
        // its end the caret steps over the closing marker and back, and inside
        // it the span comes off with the caret on the same letter.
        const EmphasisSpan bold{2, 7, 4, 5, 5};
        edit.load(QStringLiteral("a **b** c"), 7);
        MarkdownEdits::toggleWrap(edit.cursor, QStringLiteral("**"), bold);
        QCOMPARE(edit.caret(), 5);
        MarkdownEdits::toggleWrap(edit.cursor, QStringLiteral("**"), bold);
        QCOMPARE(edit.caret(), 7);
        edit.load(QStringLiteral("a **b** c"), 4);
        MarkdownEdits::toggleWrap(edit.cursor, QStringLiteral("**"), bold);
        QCOMPARE(edit.text(), QStringLiteral("a b c"));
        QCOMPARE(edit.caret(), 2);
    }

    void markdownEditsInsertLinks() {
        EditCase edit;
        edit.load(QString(), 0);
        MarkdownEdits::insertLink(edit.cursor, QString());
        QCOMPARE(edit.text(), QStringLiteral("[link text](https://)"));
        QCOMPARE(edit.cursor.selectedText(), QStringLiteral("link text"));

        edit.load(QStringLiteral("word"), 0, 4);
        MarkdownEdits::insertLink(edit.cursor, QString());
        QCOMPARE(edit.text(), QStringLiteral("[word](https://)"));
        QCOMPARE(edit.cursor.selectedText(), QStringLiteral("https://"));

        edit.load(QStringLiteral("word"), 0, 4);
        MarkdownEdits::insertLink(edit.cursor, QStringLiteral("https://a.b/(x)"));
        QCOMPARE(edit.text(), QStringLiteral("[word](https://a.b/\\(x\\))"));
        QVERIFY(!edit.cursor.hasSelection());

        QCOMPARE(MarkdownEdits::escapeLinkText(QStringLiteral("a[b]\\")),
                 QStringLiteral("a\\[b\\]\\\\"));
    }

    void markdownEditsPaste() {
        EditCase edit;
        edit.load(QStringLiteral(" word "), 0, 6);
        QVERIFY(MarkdownEdits::pasteUrlAsLink(edit.cursor, QStringLiteral("https://x.y")));
        QCOMPARE(edit.text(), QStringLiteral(" [word](https://x.y) "));

        edit.load(QStringLiteral("word"), 2);
        QVERIFY(!MarkdownEdits::pasteUrlAsLink(edit.cursor, QStringLiteral("https://x.y")));
        edit.load(QStringLiteral("word"), 0, 4);
        QVERIFY(!MarkdownEdits::pasteUrlAsLink(edit.cursor, QString()));
        edit.load(QStringLiteral("   "), 0, 3);
        QVERIFY(!MarkdownEdits::pasteUrlAsLink(edit.cursor, QStringLiteral("https://x.y")));
        QCOMPARE(edit.text(), QStringLiteral("   "));

        edit.load(QString(), 0);
        MarkdownEdits::pastePlainText(edit.cursor, QStringLiteral("a\r\nb\rc"));
        QCOMPARE(edit.text(), QStringLiteral("a\nb\nc"));
    }

    void markdownEditsReturn() {
        EditCase edit;
        // Ending a paragraph leaves the blank line that separates it from the next.
        edit.load(QStringLiteral("one\n\ntwo"), 8);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\ntwo\n\n"));

        // On a blank line Return is worth one line, not two.
        edit.load(QStringLiteral("one\n\ntwo"), 4);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\n\ntwo"));
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\n\n\ntwo"));

        edit.load(QString(), 0);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("\n"));

        edit.load(QStringLiteral("one\n  \ntwo"), 5);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n \n \ntwo"));

        // A selection dragged right to left breaks the line it leaves behind.
        edit.load(QStringLiteral("one\n\ntwo"), 7, 4);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\n\no"));

        // Lists, numbers and quotes carry their marker down; an empty item ends them.
        edit.load(QStringLiteral("- item"), 6);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("- item\n- "));
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("- item\n\n"));
        edit.load(QStringLiteral("9. item"), 7);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("9. item\n10. "));
        edit.load(QStringLiteral("1) a"), 4);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("1) a\n2) "));
        edit.load(QStringLiteral("> quote"), 7);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("> quote\n> "));
        edit.load(QStringLiteral("  - x"), 5);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("  - x\n  - "));

        // Shift+Return is a single break.
        edit.load(QStringLiteral("one"), 3);
        edit.returnKey(true);
        QCOMPARE(edit.text(), QStringLiteral("one\n"));

        // Inside a code fence every line is its own, blank ones included.
        edit.load(QStringLiteral("```\ncode\n```"), 8);
        MarkdownEdits::smartReturn(edit.cursor, false, true);
        QCOMPARE(edit.text(), QStringLiteral("```\ncode\n\n```"));
        MarkdownEdits::smartReturn(edit.cursor, false, true);
        QCOMPARE(edit.text(), QStringLiteral("```\ncode\n\n\n```"));
    }

    void markdownEditsBackspace() {
        EditCase edit;
        // At the head of a paragraph Backspace closes the whole break above it.
        edit.load(QStringLiteral("one\n\ntwo"), 5);
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("onetwo"));
        QCOMPARE(edit.caret(), 3);

        // Ending a paragraph writes both breaks, and one Backspace takes both.
        edit.load(QStringLiteral("one\n\ntwo"), 3);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\n\n\ntwo"));
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\ntwo"));
        QCOMPARE(edit.caret(), 3);

        edit.load(QStringLiteral("one\n\n\n\ntwo"), 4);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\n\n\n\ntwo"));
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\n\ntwo"));
        QCOMPARE(edit.caret(), 3);

        // Return on a blank line writes one break, and Backspace takes one.
        edit.load(QStringLiteral("one\n\ntwo"), 4);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\n\ntwo"));
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\ntwo"));

        edit.load(QStringLiteral("one\n\ntwo"), 4);
        edit.returnKey();
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\n\n\ntwo"));
        edit.backspaceKey();
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\ntwo"));

        edit.load(QStringLiteral("one\n  \ntwo"), 4);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\n  \ntwo"));
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n  \ntwo"));

        // On the blank line a document ends with, Backspace takes one break.
        edit.load(QStringLiteral("one\n"), 4);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\n"));
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n"));

        edit.load(QStringLiteral("one\n\ntwo"), 8);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\ntwo\n\n"));
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\ntwo\n"));
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n\ntwo"));

        edit.load(QString(), 0);
        edit.returnKey();
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("\n\n"));
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("\n"));
        edit.backspaceKey();
        QCOMPARE(edit.text(), QString());

        edit.load(QStringLiteral("   "), 3);
        edit.returnKey();
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("   \n\n"));
        edit.backspaceKey();
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("   "));

        edit.load(QStringLiteral("two"), 0);
        edit.returnKey();
        QCOMPARE(edit.text(), QStringLiteral("\n\ntwo"));
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("two"));
        QCOMPARE(edit.caret(), 0);

        edit.load(QStringLiteral("one\n  \n\ntwo"), 8);
        edit.backspaceKey();
        QCOMPARE(edit.text(), QStringLiteral("one\n  two"));
        QCOMPARE(edit.caret(), 6);
    }

    // Every rule is one edit, so one undo takes it back whole.
    void markdownEditsUndoInOneStep() {
        const auto undoes = [](const QString &text, int anchor, int position,
                               const std::function<void(QTextCursor &)> &rule) {
            EditCase edit;
            edit.load(text, anchor, position);
            rule(edit.cursor);
            if (edit.text() == text)
                return false;
            edit.document.undo();
            return edit.text() == text;
        };
        QVERIFY(undoes(QStringLiteral("a b c"), 2, 3, [](QTextCursor &cursor) {
            MarkdownEdits::toggleWrap(cursor, QStringLiteral("**"), std::nullopt);
        }));
        QVERIFY(undoes(QStringLiteral("word"), 0, 4, [](QTextCursor &cursor) {
            MarkdownEdits::insertLink(cursor, QStringLiteral("https://x.y"));
        }));
        QVERIFY(undoes(QStringLiteral(" word "), 0, 6, [](QTextCursor &cursor) {
            MarkdownEdits::pasteUrlAsLink(cursor, QStringLiteral("https://x.y"));
        }));
        QVERIFY(undoes(QStringLiteral("- item"), 6, -1, [](QTextCursor &cursor) {
            MarkdownEdits::smartReturn(cursor, false, false);
        }));
        QVERIFY(undoes(QStringLiteral("one\n\ntwo"), 5, -1, [](QTextCursor &cursor) {
            MarkdownEdits::deleteParagraphBreakBehind(cursor);
        }));
        QVERIFY(undoes(QStringLiteral("abc"), 1, -1, [](QTextCursor &cursor) {
            MarkdownEdits::replaceRange(cursor, 0, 3, QStringLiteral("x\r\ny"), std::pair{0, 1});
        }));
    }

    void searchMatchesStepAndFollow() {
        SearchMatches matches;
        matches.update({2, 10, 30}, 3, true);
        QCOMPARE(matches.currentIndex(), 0);
        QCOMPARE(matches.countText(), QStringLiteral("1 of 3"));
        matches.step(-1);
        QCOMPARE(matches.currentIndex(), 2);
        matches.step(1);
        QCOMPARE(matches.currentIndex(), 0);

        // An edit moves the matches; the current one is still the one at or
        // after where it was, and the caret is no business of theirs.
        matches.step(1);
        QCOMPARE(matches.currentStart(), 10);
        matches.update({4, 12, 32}, 3, false);
        QCOMPARE(matches.currentStart(), 12);

        matches.setCurrentAtOrAfter(31);
        QCOMPARE(matches.currentStart(), 32);
        matches.setCurrentAtOrAfter(40);
        QCOMPARE(matches.currentStart(), 4);

        matches.update({}, 3, true);
        QCOMPARE(matches.countText(), QStringLiteral("0 of 0"));
        QCOMPARE(matches.currentStart(), -1);
        matches.update({}, 0, true);
        QCOMPARE(matches.countText(), QString());
    }

    // A one-letter query can match hundreds of thousands of times in a long
    // document; only the matches in view are drawn, and finding them is a search.
    void findsMatchesInViewAmongManyQuickly() {
        QList<int> starts;
        for (int match = 0; match < 350000; ++match)
            starts.append(match * 15);
        SearchMatches matches;
        matches.update(starts, 1, true);

        QElapsedTimer timer;
        timer.start();
        const QList<SearchHighlight> inView = matches.inRange(1'000'000, 1'003'000);
        const QList<SearchHighlight> everything = matches.inRange(0, 6'000'000);
        QVERIFY2(timer.elapsed() < 5, qPrintable(QString::number(timer.elapsed())));

        QCOMPARE(inView.size(), 200);
        QCOMPARE(inView.first().start, 1'000'005);
        QCOMPARE(inView.first().end, 1'000'006);
        QVERIFY(std::none_of(inView.cbegin(), inView.cend(),
                             [](const SearchHighlight &match) { return match.current; }));
        QCOMPARE(everything.size(), 2000);
        QVERIFY(everything.first().current);
    }

    void centresTheColumnAtTheTextSize() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        MarkdownEditor *editor = fixture.editor;
        const auto column = [editor]() {
            return MarkdownEditor::columnWidth(QFontMetricsF(editor->font()).averageCharWidth(),
                                               editor->width());
        };
        const auto checkCentred = [&]() {
            const QMarginsF padding = fixture.layout()->padding();
            const int width = editor->viewport()->width();
            QCOMPARE(editor->lineWrapColumnOrWidth(), column());
            QCOMPARE(int(padding.left()), 24 + qRound((width - 48 - column()) / 2.0));
            QVERIFY(qAbs(padding.left() - padding.right()) <= 1);
            QCOMPARE(padding.top(), 42.0);
            QCOMPARE(padding.bottom(), 220.0);
        };
        QCOMPARE(editor->font().pixelSize(), 20);
        checkCentred();

        fixture.backend.increaseTextSize();
        QCOMPARE(editor->fontPixelSize(), 22);
        QCOMPARE(editor->font().pixelSize(), 22);
        checkCentred();
        fixture.backend.decreaseTextSize();
        fixture.backend.decreaseTextSize();
        QCOMPARE(editor->font().pixelSize(), 18);
        checkCentred();
        fixture.backend.resetTextSize();
    }

    void keepsTextColumnInsideNarrowWindows() {
        EditorFixture fixture;
        fixture.window.resize(394, 600);
        QVERIFY(fixture.show());
        fixture.load(QStringLiteral("A line long enough to need wrapping in a window this narrow, "
                                    "and then some more words to be sure of it."), 0);
        MarkdownEditor *editor = fixture.editor;
        QCOMPARE(editor->lineWrapColumnOrWidth(), 394 - 48);
        QVERIFY(fixture.layout()->padding().left() >= 0);
        const QRectF block = editor->document()->documentLayout()->blockBoundingRect(
            editor->document()->firstBlock());
        QVERIFY(block.right() <= editor->viewport()->width());
    }

    // A window wider than the column only moves it, however long the document.
    void resizesAWideWindowWithoutLayingOut() {
        EditorFixture fixture;
        fixture.window.resize(2000, 800);
        QVERIFY(fixture.show());
        QString text;
        for (int line = 0; line < 20000; ++line)
            text += QStringLiteral("Line %1 of a long document.\n").arg(line);
        fixture.load(text, 0);
        const qreal left = fixture.layout()->padding().left();
        fixture.window.resize(2100, 800);
        QTRY_VERIFY(fixture.layout()->padding().left() > left);
        QCOMPARE(fixture.layout()->lastLayoutBlockCount(), 0);
    }

    void showsThePlaceholderOnlyWhileEmptyAndUnfocused() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        fixture.other->setFocus();
        QTRY_VERIFY(fixture.other->hasFocus());
        const QColor muted(fixture.backend.darkMode() ? QStringLiteral("#909191")
                                                     : QStringLiteral("#aeb1b5"));
        const auto shown = [&]() { return holdsColour(fixture.editor->viewport()->grab().toImage(), muted); };
        QVERIFY(shown());

        fixture.editor->setFocus();
        QTRY_VERIFY(fixture.editor->hasFocus());
        QVERIFY(!shown());

        QTest::keyClick(fixture.editor, Qt::Key_A);
        fixture.other->setFocus();
        QTRY_VERIFY(fixture.other->hasFocus());
        QVERIFY(!shown());
    }

    void keepsTheCaretTwoLinesInsideTheView() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        QString text;
        for (int line = 0; line < 300; ++line)
            text += QStringLiteral("line %1\n").arg(line);
        fixture.load(text, 0);
        QTextCursor cursor = fixture.editor->textCursor();
        cursor.movePosition(QTextCursor::End);
        fixture.editor->setTextCursor(cursor);
        const int margin = 2 * fixture.editor->fontPixelSize();
        QVERIFY(fixture.editor->cursorRect().bottom() <= fixture.editor->viewport()->height() - margin);
        QVERIFY(fixture.editor->cursorRect().top() >= 0);
    }

    void selectsWithTheMouseAndShiftArrows() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        fixture.editor->setFocus();
        fixture.load(QStringLiteral("one two three"), 0);
        MarkdownEditor *editor = fixture.editor;
        const auto pointAt = [editor](int position) {
            QTextCursor cursor(editor->document());
            cursor.setPosition(position);
            return editor->cursorRect(cursor).center();
        };
        QTest::mousePress(editor->viewport(), Qt::LeftButton, {}, pointAt(4));
        QTest::mouseMove(editor->viewport(), pointAt(7));
        QTest::mouseRelease(editor->viewport(), Qt::LeftButton, {}, pointAt(7));
        QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("two"));

        fixture.load(QStringLiteral("one two three"), 0);
        QTest::keyClick(editor, Qt::Key_Right, Qt::ShiftModifier);
        QTest::keyClick(editor, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("on"));
    }

    void breaksLinesOnReturn() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        fixture.editor->setFocus();
        const auto returnKey = [&] { QTest::keyClick(fixture.editor, Qt::Key_Return); };

        // Ending a paragraph leaves the blank line that separates it from the
        // next one.
        fixture.load(QStringLiteral("one\n\ntwo"), 8);
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n\ntwo\n\n"));

        // On a line that is already blank there is nothing to separate from,
        // so Return is worth one line, not two.
        fixture.load(QStringLiteral("one\n\ntwo"), 4);
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n\n\ntwo"));
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n\n\n\ntwo"));

        // An empty document is a blank line too.
        fixture.load(QString(), 0);
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("\n"));

        // Whitespace left behind on a line still reads as blank.
        fixture.load(QStringLiteral("one\n  \ntwo"), 5);
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n \n \ntwo"));

        // A selection dragged right to left leaves the caret on the blank line
        // it began on, but the break lands on the line it leaves behind.
        fixture.load(QStringLiteral("one\n\ntwo"), 7);
        QTextCursor cursor = fixture.editor->textCursor();
        cursor.setPosition(4, QTextCursor::KeepAnchor);
        fixture.editor->setTextCursor(cursor);
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n\n\no"));

        // A list carries its marker down, and an item left empty drops the
        // marker to end the list.
        fixture.load(QStringLiteral("- item"), 6);
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("- item\n- "));
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("- item\n\n"));

        // Inside a code fence every line is its own, blank ones included.
        fixture.load(QStringLiteral("```\ncode\n```"), 8);
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("```\ncode\n\n```"));
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("```\ncode\n\n\n```"));

        // Shift+Return is a single break.
        fixture.load(QStringLiteral("one"), 3);
        QTest::keyClick(fixture.editor, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(fixture.text(), QStringLiteral("one\n"));
    }

    void breaksLinesAfterInlineBackticks() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        fixture.editor->setFocus();

        // Backticks that open no fence leave the paragraphs below as prose.
        const QString text = QStringLiteral("```x``` inline\n\npara");
        fixture.load(text, int(text.length()));
        QTest::keyClick(fixture.editor, Qt::Key_Return);
        QCOMPARE(fixture.text(), text + QStringLiteral("\n\n"));

        // Before a closing fence the caret is still in the code; before an
        // opening one it is not yet.
        fixture.load(QStringLiteral("```\ncode\n```"), 9);
        QTest::keyClick(fixture.editor, Qt::Key_Return);
        QCOMPARE(fixture.text(), QStringLiteral("```\ncode\n\n```"));
        fixture.load(QStringLiteral("para\n```\ncode\n```"), 5);
        QTest::keyClick(fixture.editor, Qt::Key_Return);
        QCOMPARE(fixture.text(), QStringLiteral("para\n\n\n```\ncode\n```"));

        // Tilde fences hold code the same way.
        fixture.load(QStringLiteral("~~~\ncode"), 8);
        QTest::keyClick(fixture.editor, Qt::Key_Return);
        QCOMPARE(fixture.text(), QStringLiteral("~~~\ncode\n"));
    }

    void closesParagraphBreaksOnBackspace() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        fixture.editor->setFocus();
        const auto caret = [&] { return fixture.editor->textCursor().position(); };
        const auto returnKey = [&] { QTest::keyClick(fixture.editor, Qt::Key_Return); };
        const auto backspaceKey = [&] { QTest::keyClick(fixture.editor, Qt::Key_Backspace); };

        // Backspace at the head of a paragraph closes the whole break above
        // it, so the two paragraphs join in one press rather than two, and
        // the caret lands where they meet.
        fixture.load(QStringLiteral("one\n\ntwo"), 5);
        backspaceKey();
        QCOMPARE(fixture.text(), QStringLiteral("onetwo"));
        QCOMPARE(caret(), 3);

        // Ending a paragraph writes both breaks at once, and one Backspace
        // takes both back.
        fixture.load(QStringLiteral("one\n\ntwo"), 3);
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n\n\n\ntwo"));
        backspaceKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n\ntwo"));
        QCOMPARE(caret(), 3);

        // Return on a blank line writes a single break, so Backspace must
        // take back a single break too.
        fixture.load(QStringLiteral("one\n\ntwo"), 4);
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n\n\ntwo"));
        backspaceKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n\ntwo"));

        // On the blank line a document ends with, Backspace takes one break.
        fixture.load(QStringLiteral("one\n"), 4);
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n\n"));
        backspaceKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n"));

        // An empty document is a blank line, so it takes two Returns to open
        // a gap and two Backspaces to close it again.
        fixture.load(QString(), 0);
        returnKey();
        returnKey();
        QCOMPARE(fixture.text(), QStringLiteral("\n\n"));
        backspaceKey();
        QCOMPARE(fixture.text(), QStringLiteral("\n"));
        backspaceKey();
        QCOMPARE(fixture.text(), QString());

        // Spaces left on the line above a break do not make it any less of one.
        fixture.load(QStringLiteral("one\n  \n\ntwo"), 8);
        backspaceKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n  two"));
        QCOMPARE(caret(), 6);

        // With a selection, Backspace deletes it as anywhere else.
        fixture.load(QStringLiteral("one\n\ntwo"), 5);
        QTextCursor cursor = fixture.editor->textCursor();
        cursor.setPosition(8, QTextCursor::KeepAnchor);
        fixture.editor->setTextCursor(cursor);
        backspaceKey();
        QCOMPARE(fixture.text(), QStringLiteral("one\n\n"));
    }

    void togglesEmphasisAtTheCaret() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        MarkdownEditor *editor = fixture.editor;
        editor->setFocus();
        const auto type = [editor](const QString &keys) { QTest::keyClicks(editor, keys); };
        const auto bold = [editor] { editor->toggleWrap(QStringLiteral("**")); };
        const auto caret = [editor] { return editor->textCursor().position(); };

        // Bold, type, bold ends the bold instead of stacking markers.
        fixture.load(QStringLiteral("a "), 2);
        bold();
        type(QStringLiteral("hi"));
        bold();
        type(QStringLiteral(" more"));
        QCOMPARE(fixture.text(), QStringLiteral("a **hi** more"));

        // Just past the closing marker, bold steps back into the bold.
        fixture.load(QStringLiteral("a **hi**"), 8);
        bold();
        type(QStringLiteral("!"));
        QCOMPARE(fixture.text(), QStringLiteral("a **hi!**"));

        // Inside the bold, bold takes it off and keeps the caret in place.
        fixture.load(QStringLiteral("a **hello** b"), 6);
        bold();
        QCOMPARE(fixture.text(), QStringLiteral("a hello b"));
        QCOMPARE(caret(), 4);

        // At the end of bold italic text, bold steps out rather than unbolding.
        fixture.load(QStringLiteral("***hello***"), 8);
        bold();
        QCOMPARE(fixture.text(), QStringLiteral("***hello***"));
        QCOMPARE(caret(), 11);

        // Through several layers of nesting too.
        fixture.load(QStringLiteral("**~~*hello*~~**"), 10);
        bold();
        QCOMPARE(fixture.text(), QStringLiteral("**~~*hello*~~**"));
        QCOMPARE(caret(), 15);

        // A literal underscore at the end is text, not a closing marker.
        fixture.load(QStringLiteral("**value_**"), 7);
        bold();
        QCOMPARE(fixture.text(), QStringLiteral("value_"));

        // Italic works the same way.
        fixture.load(QStringLiteral("a "), 2);
        editor->toggleWrap(QStringLiteral("*"));
        type(QStringLiteral("hi"));
        editor->toggleWrap(QStringLiteral("*"));
        type(QStringLiteral(" x"));
        QCOMPARE(fixture.text(), QStringLiteral("a *hi* x"));
    }

    void togglesEmphasisMarkers() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        // Sets the text, selects [from, to), toggles, and returns the text.
        const auto toggle = [&fixture](const QString &text, int from, int to, const QString &marker) {
            fixture.load(text, from);
            QTextCursor cursor = fixture.editor->textCursor();
            cursor.setPosition(to, QTextCursor::KeepAnchor);
            fixture.editor->setTextCursor(cursor);
            fixture.editor->toggleWrap(marker);
            return fixture.text();
        };
        const QString bold = QStringLiteral("**");
        const QString italic = QStringLiteral("*");

        QCOMPARE(toggle(QStringLiteral("a word b"), 2, 6, bold), QStringLiteral("a **word** b"));
        // Off again, whether the markers sit outside the selection or in it.
        QCOMPARE(toggle(QStringLiteral("a **word** b"), 4, 8, bold), QStringLiteral("a word b"));
        QCOMPARE(toggle(QStringLiteral("a **word** b"), 2, 10, bold), QStringLiteral("a word b"));
        QCOMPARE(toggle(QStringLiteral("a ~~word~~ b"), 4, 8, QStringLiteral("~~")),
                 QStringLiteral("a word b"));
        // An empty pair goes away too.
        QCOMPARE(toggle(QStringLiteral("Hello****."), 7, 7, bold), QStringLiteral("Hello."));

        // The "*" of bold is not italic, and "***" holds both.
        QCOMPARE(toggle(QStringLiteral("**word**"), 2, 6, italic), QStringLiteral("***word***"));
        QCOMPARE(toggle(QStringLiteral("***word***"), 3, 7, italic), QStringLiteral("**word**"));
        QCOMPARE(toggle(QStringLiteral("***word***"), 3, 7, bold), QStringLiteral("*word*"));
        // Selecting part of bold's markers adds italic rather than removing bold.
        QCOMPARE(toggle(QStringLiteral("**word**"), 1, 7, italic), QStringLiteral("***word***"));

        // Separate spans are wrapped, not broken apart.
        QCOMPARE(toggle(QStringLiteral("**a** and **b**"), 0, 15, bold),
                 QStringLiteral("****a** and **b****"));
        // Other emphasis nested inside does not stop italic from coming off.
        QCOMPARE(toggle(QStringLiteral("*a **b** c*"), 1, 10, italic),
                 QStringLiteral("a **b** c"));
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        EditCase edit;
        edit.load(QStringLiteral("alpha omega"), 5);
        MarkdownEdits::replaceRange(edit.cursor, 5, 5, QStringLiteral("one\r\ntwo"));
        QCOMPARE(edit.text(), QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(edit.caret(), 12);

        edit.load(QStringLiteral("alpha beta omega"), 6, 10);
        MarkdownEdits::replaceRange(edit.cursor, 6, 10, QStringLiteral("**beta**"), std::pair{2, 6});
        QCOMPARE(edit.text(), QStringLiteral("alpha **beta** omega"));
        QCOMPARE(edit.cursor.selectionStart(), 8);
        QCOMPARE(edit.cursor.selectionEnd(), 12);
    }

    void stepsOverWholeCharactersWhenTypingAndSaving() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        MarkdownEditor *editor = fixture.editor;
        editor->setFocus();
        const auto move = [editor](int direction) {
            QTest::keyClick(editor, direction > 0 ? Qt::Key_Right : Qt::Key_Left);
            return editor->textCursor().position();
        };

        // Right steps over the whole emoji, so typing keeps it in the file.
        fixture.load(QStringLiteral("a😀b"), 1);
        QCOMPARE(move(1), 3);
        QTest::keyClick(editor, Qt::Key_X);
        QCOMPARE(fixture.text(), QStringLiteral("a😀xb"));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString savedPath = directory.filePath(QStringLiteral("emoji.md"));
        fixture.backend.saveAs(QUrl::fromLocalFile(savedPath));
        QFile saved(savedPath);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(saved.readAll()), QStringLiteral("a😀xb"));

        // Left steps back over the same cluster.
        fixture.load(QStringLiteral("a😀b"), 3);
        QCOMPARE(move(-1), 1);

        // Hidden Markdown markers are still skipped on both sides.
        fixture.load(QStringLiteral("**bold**"), 0);
        QCOMPARE(move(1), 2);
        fixture.load(QStringLiteral("**bold**"), 8);
        QCOMPARE(move(-1), 6);

        // Hidden markers next to an emoji combine correctly.
        fixture.load(QStringLiteral("*😀*x"), 0);
        QCOMPARE(move(1), 1);
        QCOMPARE(move(1), 4);

        // Paragraph separators still move one position at a time.
        fixture.load(QStringLiteral("ab\ncd"), 2);
        QCOMPARE(move(1), 3);
        QCOMPARE(move(-1), 2);

        // A selection collapses to the edge the arrow points at.
        fixture.load(QStringLiteral("abcdef"), 1);
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(4, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        QCOMPARE(move(-1), 1);
        cursor.setPosition(1);
        cursor.setPosition(4, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        QCOMPARE(move(1), 4);
    }

    void stepsCursorByWholeGraphemeClusters() {
        EditorFixture fixture;
        const auto step = [&fixture](const QString &text, int from, int direction) {
            fixture.load(text, 0);
            return fixture.backend.cursorPositionStep(from, direction);
        };

        // Qt 6 treats surrogate pairs, skin tone modifiers, ZWJ sequences,
        // regional indicators and combining marks as single cursor steps.
        QCOMPARE(step(QStringLiteral("a😀b"), 1, 1), 3);
        QCOMPARE(step(QStringLiteral("a😀b"), 3, -1), 1);
        QCOMPARE(step(QStringLiteral("a👍🏽b"), 1, 1), 5);
        QCOMPARE(step(QStringLiteral("a👍🏽b"), 5, -1), 1);
        QCOMPARE(step(QStringLiteral("a👨‍👩‍👧b"), 1, 1), 9);
        QCOMPARE(step(QStringLiteral("a👨‍👩‍👧b"), 9, -1), 1);
        QCOMPARE(step(QStringLiteral("a🇺🇸b"), 1, 1), 5);
        QCOMPARE(step(QStringLiteral("a1⃣b"), 1, 1), 3);
        QCOMPARE(step(QStringLiteral("aéb"), 1, 1), 3);

        // Document limits and paragraph separators are handled explicitly.
        QCOMPARE(step(QStringLiteral("a😀b"), 0, -1), 0);
        QCOMPARE(step(QStringLiteral("a😀b"), 4, 1), 4);
        QCOMPARE(step(QStringLiteral("ab\ncd"), 2, 1), 3);
        QCOMPARE(step(QStringLiteral("ab\ncd"), 3, -1), 2);
        QCOMPARE(step(QStringLiteral("ab\ncd"), 5, 1), 5);
        QCOMPARE(step(QStringLiteral("😀\nb"), 2, 1), 3);
        QCOMPARE(step(QStringLiteral("😀\nb"), 3, -1), 2);
    }

    void undoesAnEditInOneStep() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        fixture.load(QStringLiteral("hello world"), 0);
        QTextCursor cursor = fixture.editor->textCursor();
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        fixture.editor->setTextCursor(cursor);
        fixture.editor->toggleWrap(QStringLiteral("**"));
        QCOMPARE(fixture.text(), QStringLiteral("**hello** world"));
        fixture.editor->undo();
        QCOMPARE(fixture.text(), QStringLiteral("hello world"));
    }

    void keepsLineHeightAcrossUndoAndPaste() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        MarkdownEditor *editor = fixture.editor;
        editor->setFocus();
        QTextDocument *document = editor->document();
        const auto allLinesSpaced = [document] {
            for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
                if (block.blockFormat().lineHeight() != 140)
                    return false;
            }
            return true;
        };
        const auto select = [editor](int from, int to) {
            QTextCursor cursor = editor->textCursor();
            cursor.setPosition(from);
            cursor.setPosition(to, QTextCursor::KeepAnchor);
            editor->setTextCursor(cursor);
        };

        // Undoing a deleted line leaves redo working.
        fixture.load(QStringLiteral("a\nb\nc"), 0);
        select(2, 4);
        QTest::keyClick(editor, Qt::Key_Delete);
        QCOMPARE(fixture.text(), QStringLiteral("a\nc"));
        editor->undo();
        QCOMPARE(fixture.text(), QStringLiteral("a\nb\nc"));
        QVERIFY(document->isRedoAvailable());

        // Lines written over others keep the line height, however many there are.
        fixture.load(QStringLiteral("aa\nbb\ncc"), 0);
        QGuiApplication::clipboard()->setText(QStringLiteral("X\nY"));
        select(0, 5);
        editor->paste();
        QCOMPARE(fixture.text(), QStringLiteral("X\nY\ncc"));
        QVERIFY(allLinesSpaced());
    }

    // A URL pasted over selected text links that text; anything else goes in
    // as plain text, however it arrives.
    void pastesALinkOverSelectedText() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        MarkdownEditor *editor = fixture.editor;
        editor->setFocus();
        fixture.load(QStringLiteral("see word here"), 4);
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(8, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        QGuiApplication::clipboard()->setText(QStringLiteral("https://x.y"));
        editor->paste();
        QCOMPARE(fixture.text(), QStringLiteral("see [word](https://x.y) here"));

        fixture.load(QStringLiteral("ab"), 1);
        editor->paste();
        QCOMPARE(fixture.text(), QStringLiteral("ahttps://x.yb"));

        auto *html = new QMimeData;
        html->setHtml(QStringLiteral("<b>bold</b>"));
        html->setText(QStringLiteral("bold\r\nline"));
        QGuiApplication::clipboard()->setMimeData(html);
        fixture.load(QString(), 0);
        editor->paste();
        QCOMPARE(fixture.text(), QStringLiteral("bold\nline"));
    }

    void pagesByTheViewLessTwoLines() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        MarkdownEditor *editor = fixture.editor;
        editor->setFocus();
        QString text;
        for (int line = 0; line < 500; ++line)
            text += QStringLiteral("line %1\n").arg(line);
        fixture.load(text, 0);
        const int step = qMax(editor->fontPixelSize(),
                              editor->viewport()->height() - 2 * editor->fontPixelSize());
        const auto caretY = [editor] {
            return editor->cursorRect().center().y() + editor->verticalScrollBar()->value();
        };
        const int lineHeight = editor->cursorRect().height() * 2;

        const int before = caretY();
        QTest::keyClick(editor, Qt::Key_PageDown);
        QVERIFY2(qAbs(caretY() - before - step) <= lineHeight,
                 qPrintable(QStringLiteral("%1 %2").arg(caretY() - before).arg(step)));
        QVERIFY(!editor->textCursor().hasSelection());

        const int anchor = editor->textCursor().position();
        const int anchorY = caretY();
        QTest::keyClick(editor, Qt::Key_PageDown, Qt::ShiftModifier);
        QCOMPARE(editor->textCursor().anchor(), anchor);
        QVERIFY(editor->textCursor().position() > anchor);

        // A page back up from there lands about where the selection began.
        QTest::keyClick(editor, Qt::Key_PageUp);
        QVERIFY(!editor->textCursor().hasSelection());
        QVERIFY(qAbs(caretY() - anchorY) <= lineHeight);
    }

    void showsLinkUrlOnlyOverTheLinkText() {
        // Probing a character past the end of the text warns from QTextCursor.
        QTest::failOnWarning(QRegularExpression(QStringLiteral("out of range")));

        EditorFixture fixture;
        QVERIFY(fixture.show());
        MarkdownEditor *editor = fixture.editor;
        const auto urlUnder = [editor](int x, int y) { return editor->linkAt(QPoint(x, y)); };
        const auto boxAt = [editor](int position) {
            QTextCursor cursor(editor->document());
            cursor.setPosition(position);
            return editor->cursorRect(cursor);
        };
        fixture.load(QStringLiteral("see [label](https://example.com)\nplain"), 0);

        // Over the label (the hidden "[" is index 4), in the right half of a
        // character too.
        const QRect first = boxAt(5);
        const QRect second = boxAt(6);
        const int middleY = first.y() + first.height() / 2;
        QCOMPARE(urlUnder(first.x() + 1, middleY), QStringLiteral("https://example.com"));
        QCOMPARE(urlUnder(second.x() - 1, middleY), QStringLiteral("https://example.com"));
        // Over the plain word before it.
        QCOMPARE(urlUnder(boxAt(0).x() + 1, middleY), QString());
        // Past the end of the line the link ends.
        QCOMPARE(urlUnder(editor->viewport()->width() - 2, middleY), QString());
        // The right half of the label's last character still counts, though
        // the nearest caret position is past the label.
        QCOMPARE(urlUnder(boxAt(10).x() - 1, middleY), QStringLiteral("https://example.com"));

        // Below a last line that is a link, the nearest caret snaps up onto it.
        fixture.load(QStringLiteral("[label](https://example.com)"), 0);
        const QRect only = boxAt(1);
        QCOMPARE(urlUnder(only.x() + 1, only.y() + only.height() / 2),
                 QStringLiteral("https://example.com"));
        QCOMPARE(urlUnder(only.x() + 1, only.y() + only.height() * 3), QString());

        // A wide character counts across its whole width.
        fixture.load(QStringLiteral("[宽](https://example.com)"), 0);
        const QRect wide = boxAt(1);
        const int wideRight = boxAt(2).x();
        QVERIFY(wideRight - wide.x() > editor->font().pixelSize() * 0.7);
        QCOMPARE(urlUnder(wideRight - 1, wide.y() + wide.height() / 2),
                 QStringLiteral("https://example.com"));

        // A right-to-left label runs the other way across the screen.
        fixture.load(QStringLiteral("[שלום](https://example.org)"), 0);
        const QRect rtlFirst = boxAt(1);
        const QRect rtlSecond = boxAt(2);
        QVERIFY(rtlSecond.x() < rtlFirst.x());
        const int rtlY = rtlFirst.y() + rtlFirst.height() / 2;
        QCOMPARE(urlUnder((rtlFirst.x() + rtlSecond.x()) / 2, rtlY),
                 QStringLiteral("https://example.org"));
        QCOMPARE(urlUnder(rtlFirst.x() - 1, rtlY), QStringLiteral("https://example.org"));

        // Link-like text inside fenced code is not a link.
        fixture.load(QStringLiteral("```\n[label](https://example.com)\n```"), 0);
        QCOMPARE(fixture.backend.linkUrlAt(5), QString());

        // Moving the pointer over a link is what shows it. The move goes to the
        // view itself, as QTest moving the real pointer does not reach a
        // window on macOS.
        fixture.load(QStringLiteral("see [label](https://example.com)"), 0);
        const auto moveTo = [editor](const QPoint &point) {
            QMouseEvent move(QEvent::MouseMove, point, editor->viewport()->mapToGlobal(point),
                             Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(editor->viewport(), &move);
        };
        const QPoint over(boxAt(6).x(), boxAt(6).center().y());
        moveTo(over);
        QCOMPARE(editor->hoveredLink(), QStringLiteral("https://example.com"));
        moveTo(QPoint(boxAt(1).x(), over.y()));
        QCOMPARE(editor->hoveredLink(), QString());
    }

    void showsLinkTooltipUrlAsPlainText() {
        // A destination Qt would mistake for HTML must still read as itself.
        const QString destination = QStringLiteral("<img src=\"https://example.com/pixel\">");
        QVERIFY(Qt::mightBeRichText(destination));
        QTextDocument shown;
        shown.setHtml(MarkdownEditor::linkTooltip(destination, 2000));
        QCOMPARE(shown.toPlainText(), destination);

        // A long one is cut in the middle to fit.
        const QString longUrl = QStringLiteral("https://example.com/") + QString(400, QLatin1Char('a'))
            + QStringLiteral("/end");
        shown.setHtml(MarkdownEditor::linkTooltip(longUrl, 300));
        QVERIFY(shown.toPlainText().size() < longUrl.size());
        QVERIFY(shown.toPlainText().startsWith(QStringLiteral("https://")));
        QVERIFY(shown.toPlainText().endsWith(QStringLiteral("/end")));
    }

    void banksTouchpadScrollBeforeChangingTextSize() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        QWidget *viewport = fixture.editor->viewport();
        const auto scrollBy = [viewport](int angle, int pixels) {
            const QPointF at(10, 10);
            QWheelEvent wheel(at, viewport->mapToGlobal(at), QPoint(0, pixels), QPoint(0, angle),
                              Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(viewport, &wheel);
        };

        // A touchpad trickles in pixels: a nudge is not a whole step yet. Its
        // events carry a synthesized angleDelta as well.
        scrollBy(240, 20);
        scrollBy(240, 20);
        QCOMPARE(fixture.backend.textScale(), 1.0);
        scrollBy(240, 20);
        QCOMPARE(fixture.backend.textScale(), 1.1);

        // A wheel notch is one step, and reversing drops the banked remainder.
        scrollBy(-120, 0);
        QCOMPARE(fixture.backend.textScale(), 1.0);

        // Without ⌘ the wheel scrolls, as anywhere.
        fixture.load(QString(500, QLatin1Char('\n')), 0);
        const QPointF at(10, 10);
        QWheelEvent scroll(at, viewport->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton,
                           Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(viewport, &scroll);
        QVERIFY(fixture.editor->verticalScrollBar()->value() > 0);
        QCOMPARE(fixture.backend.textScale(), 1.0);
        fixture.backend.resetTextSize();
    }

    void highlightsSearchMatchesInTheirColours() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        fixture.load(QStringLiteral("ab xyz cd xyz ef"), 0);
        const auto backgrounds = [&fixture]() {
            QStringList colours;
            for (const QTextEdit::ExtraSelection &selection : fixture.editor->extraSelections())
                colours.append(selection.format.background().color().name());
            return colours;
        };

        fixture.backend.setDarkMode(true);
        fixture.editor->setSearchHighlights({{3, 6, false}, {10, 13, true}});
        QCOMPARE(fixture.editor->extraSelections().size(), 2);
        QCOMPARE(fixture.editor->extraSelections().at(1).cursor.selectedText(), QStringLiteral("xyz"));
        QCOMPARE(backgrounds(), QStringList({QStringLiteral("#725b18"), QStringLiteral("#b36b20")}));

        fixture.backend.setDarkMode(false);
        QCOMPARE(backgrounds(), QStringList({QStringLiteral("#ffe58a"), QStringLiteral("#ffad42")}));

        fixture.editor->setSearchHighlights({});
        QVERIFY(fixture.editor->extraSelections().isEmpty());
    }

    void searchFollowsEditsWithoutTakingTheCaret() {
        FindFixture fixture;
        QVERIFY(fixture.show());
        MarkdownEditor *editor = fixture.editor;
        FindBar *findBar = fixture.findBar;
        fixture.load(QStringLiteral("foo bar foo"));
        findBar->open(false);
        findBar->searchField()->setText(QStringLiteral("foo"));
        QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("foo"));

        // Back in the text, typing goes where the caret is.
        editor->setFocus();
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(7);
        editor->setTextCursor(cursor);
        QTest::keyClicks(editor, QStringLiteral("xy"));
        QCOMPARE(fixture.text(), QStringLiteral("foo barxy foo"));
        QCOMPARE(findBar->matches().count(), 2);

        // The current match is drawn apart from the others, and the mark
        // follows it when text above it is deleted.
        fixture.load(QStringLiteral("long text\nfoo\nfoo"));
        findBar->searchField()->setText(QStringLiteral("x"));
        findBar->searchField()->setText(QStringLiteral("foo"));
        findBar->step(1);
        QCOMPARE(fixture.drawnAs(14), QStringLiteral("current"));
        QCOMPARE(fixture.drawnAs(10), QStringLiteral("other"));
        QTextCursor edit(editor->document());
        edit.setPosition(0);
        edit.setPosition(10, QTextCursor::KeepAnchor);
        edit.removeSelectedText();
        QCOMPARE(findBar->matches().count(), 2);
        QCOMPARE(findBar->matches().currentIndex(), 0);
        QCOMPARE(fixture.drawnAs(0), QStringLiteral("current"));
        QCOMPARE(fixture.drawnAs(4), QStringLiteral("other"));

        // Nor does it stay behind when another match moves into its offset.
        fixture.load(QStringLiteral("foo\nab\nfoo"));
        findBar->step(1);
        QCOMPARE(fixture.drawnAs(7), QStringLiteral("current"));
        edit.setPosition(0);
        edit.insertText(QStringLiteral("abcdefg"));
        QCOMPARE(findBar->matches().currentStart(), 7);
        QCOMPARE(fixture.drawnAs(7), QStringLiteral("current"));
        QCOMPARE(fixture.drawnAs(14), QStringLiteral("other"));

        // Matches are found at the document's own offsets, even past a
        // character whose lowercase is longer.
        fixture.load(QString::fromUtf8("\xC4\xB0 ab"));
        findBar->searchField()->setText(QStringLiteral("ab"));
        QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("ab"));
    }

    void replacesOneMatchAfterAnother() {
        FindFixture fixture;
        QVERIFY(fixture.show());
        FindBar *findBar = fixture.findBar;
        auto *replaceButton = findBar->findChild<QPushButton *>(QStringLiteral("replaceButton"));
        auto *replaceAllButton = findBar->findChild<QPushButton *>(QStringLiteral("replaceAllButton"));
        QVERIFY(replaceButton && replaceAllButton);

        fixture.load(QStringLiteral("foo foo foo"));
        findBar->open(true);
        QVERIFY(replaceButton->isVisible());
        findBar->searchField()->setText(QStringLiteral("foo"));
        findBar->replaceField()->setText(QStringLiteral("Foo"));

        // A replacement the query still matches is not found again.
        replaceButton->click();
        QCOMPARE(fixture.text(), QStringLiteral("Foo foo foo"));
        replaceButton->click();
        QCOMPARE(fixture.text(), QStringLiteral("Foo Foo foo"));
        replaceButton->click();
        QCOMPARE(fixture.text(), QStringLiteral("Foo Foo Foo"));

        // Replace All is one edit, so one undo takes it back.
        fixture.load(QStringLiteral("a foo b foo c"));
        findBar->replaceField()->setText(QStringLiteral("x"));
        replaceAllButton->click();
        QCOMPARE(fixture.text(), QStringLiteral("a x b x c"));
        fixture.editor->undo();
        QCOMPARE(fixture.text(), QStringLiteral("a foo b foo c"));
    }

    void replacesEveryMatchInOneUndo() {
        FindFixture fixture;
        QVERIFY(fixture.show());
        fixture.load(QStringLiteral("x x x"));
        fixture.findBar->open(true);
        fixture.findBar->searchField()->setText(QStringLiteral("x"));
        fixture.findBar->replaceField()->setText(QStringLiteral("yy"));
        fixture.findBar->findChild<QPushButton *>(QStringLiteral("replaceAllButton"))->click();
        QCOMPARE(fixture.text(), QStringLiteral("yy yy yy"));
        fixture.editor->undo();
        QCOMPARE(fixture.text(), QStringLiteral("x x x"));
    }

    void stepsThroughMatchesFromTheKeyboard() {
        FindFixture fixture;
        QVERIFY(fixture.show());
        FindBar *findBar = fixture.findBar;
        auto *count = findBar->findChild<QLabel *>(QStringLiteral("matchCount"));
        QVERIFY(count);
        fixture.load(QStringLiteral("foo foo foo"));
        findBar->open(true);
        QTest::keyClicks(findBar->searchField(), QStringLiteral("foo"));
        QCOMPARE(count->text(), QStringLiteral("1 of 3"));
        QTest::keyClick(findBar->searchField(), Qt::Key_Return);
        QCOMPARE(count->text(), QStringLiteral("2 of 3"));
        QCOMPARE(fixture.editor->textCursor().selectionStart(), 4);
        QTest::keyClick(findBar->searchField(), Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(count->text(), QStringLiteral("1 of 3"));

        // Return in the replace field replaces the current match.
        findBar->replaceField()->setText(QStringLiteral("bar"));
        QTest::keyClick(findBar->replaceField(), Qt::Key_Return);
        QCOMPARE(fixture.text(), QStringLiteral("bar foo foo"));
        QCOMPARE(count->text(), QStringLiteral("1 of 2"));
    }

    void reachesAndLeavesTheReplaceFieldFromTheKeyboard() {
        FindFixture fixture;
        QVERIFY(fixture.show());
        FindBar *findBar = fixture.findBar;

        findBar->open(true);
        QTRY_VERIFY(findBar->searchField()->hasFocus());
        QTest::keyClick(findBar->searchField(), Qt::Key_Tab);
        QTRY_VERIFY(findBar->replaceField()->hasFocus());
        QTest::keyClick(findBar->replaceField(), Qt::Key_Escape);
        QVERIFY(!findBar->isOpen());
        QTRY_VERIFY(fixture.editor->hasFocus());

        // Escape closes the bar from the search field too.
        findBar->open(false);
        QTest::keyClick(findBar->searchField(), Qt::Key_Escape);
        QVERIFY(!findBar->isOpen());

        // A link inserted from the find bar puts the writer back in the text.
        findBar->open(false);
        QTRY_VERIFY(findBar->searchField()->hasFocus());
        fixture.editor->insertLink();
        QTRY_VERIFY(fixture.editor->hasFocus());
    }

    void putsEveryCommandInTheMenuBar() {
        WindowFixture fixture;
        // Each menu as its title followed by its items, written out as macOS
        // shows them, keys and all, and a separator as "-".
        QStringList shown;
        for (QAction *menuAction : fixture.window.menuBar()->actions()) {
            QMenu *menu = menuAction->menu();
            QVERIFY(menu);
            QStringList entries;
            for (QAction *action : menu->actions()) {
                if (action->isSeparator()) {
                    entries += QStringLiteral("-");
                    continue;
                }
                QVERIFY2(action->menuRole() == QAction::NoRole, qPrintable(action->text()));
                const QString keys = action->shortcut().toString(QKeySequence::NativeText);
                entries += keys.isEmpty() ? action->text() : action->text() + u' ' + keys;
            }
            shown += menu->title() + QStringLiteral(": ") + entries.join(QStringLiteral(" | "));
        }

        QCOMPARE(shown, QStringList({
            QStringLiteral("File: New Window ⌘N | Open… ⌘O | - | Save ⌘S | Save As… ⇧⌘S | - | "
                           "Print… ⌘P | - | Close Window ⌘W"),
            QStringLiteral("Edit: Undo ⌘Z | Redo ⇧⌘Z | - | Cut ⌘X | Copy ⌘C | Paste ⌘V | "
                           "Select All ⌘A | Copy as HTML ⇧⌘C | - | Find ⌘F | "
                           "Find and Replace ⌥⌘F | - | Bold ⌘B | Italic ⌘I | "
                           "Strikethrough ⇧⌘X | Link ⌘K"),
            QStringLiteral("View: Bigger ⌘= | Smaller ⌘- | Actual Size ⌘0 | Font… | - | "
                           "Enter Full Screen ⌃⌘F"),
            QStringLiteral("Window: Minimize ⌘M | Zoom"),
        }));
    }

    void givesEveryKeyOneCommand() {
        WindowFixture fixture;
        // Qt finds a sequence that two commands claim ambiguous and runs
        // neither, so no key may belong to a menu item and a shortcut both.
        QStringList sequences;
        for (QAction *action : fixture.window.findChildren<QAction *>()) {
            for (const QKeySequence &sequence : action->shortcuts())
                sequences += sequence.toString();
        }
        for (QShortcut *shortcut : fixture.window.findChildren<QShortcut *>()) {
            for (const QKeySequence &sequence : shortcut->keys())
                sequences += sequence.toString();
        }
        QVERIFY(sequences.size() > 25);
        QStringList unique = sequences;
        unique.removeDuplicates();
        for (const QString &sequence : std::as_const(unique))
            QVERIFY2(sequences.count(sequence) == 1, qPrintable(sequence + " is taken twice"));
    }

    void runsEditCommandsFromTheMenu() {
        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.editor()->setFocus();

        const QList<std::pair<QString, QString>> wraps = {
            { QStringLiteral("Bold"), QStringLiteral("**word** here") },
            { QStringLiteral("Italic"), QStringLiteral("*word* here") },
            { QStringLiteral("Strikethrough"), QStringLiteral("~~word~~ here") },
        };
        for (const auto &[command, wrapped] : wraps) {
            fixture.load(QStringLiteral("word here"));
            fixture.select(0, 4);
            QVERIFY(fixture.choose(command));
            QCOMPARE(fixture.text(), wrapped);
        }

        QGuiApplication::clipboard()->setText(QStringLiteral("https://example.com"));
        fixture.load(QStringLiteral("word here"));
        fixture.select(0, 4);
        QVERIFY(fixture.choose(QStringLiteral("Link")));
        QCOMPARE(fixture.text(), QStringLiteral("[word](https://example.com) here"));

        // Paste links a URL over selected text, from the menu as from the key,
        // and pastes anything else as plain text.
        fixture.load(QStringLiteral("word here"));
        fixture.select(0, 4);
        QTest::keyClick(fixture.editor(), Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(fixture.text(), QStringLiteral("[word](https://example.com) here"));
        fixture.load(QStringLiteral("word here"));
        fixture.select(0, 4);
        QVERIFY(fixture.choose(QStringLiteral("Paste")));
        QCOMPARE(fixture.text(), QStringLiteral("[word](https://example.com) here"));
        auto *rich = new QMimeData;
        rich->setHtml(QStringLiteral("<b>bold</b>"));
        rich->setText(QStringLiteral("bold"));
        QGuiApplication::clipboard()->setMimeData(rich);
        fixture.load(QStringLiteral("word here"));
        fixture.select(5, 9);
        QVERIFY(fixture.choose(QStringLiteral("Paste")));
        QCOMPARE(fixture.text(), QStringLiteral("word bold"));

        fixture.select(0, 4);
        QVERIFY(fixture.choose(QStringLiteral("Copy")));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("word"));
        QVERIFY(fixture.choose(QStringLiteral("Cut")));
        QCOMPARE(fixture.text(), QStringLiteral(" bold"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("word"));
        QVERIFY(fixture.choose(QStringLiteral("Undo")));
        QCOMPARE(fixture.text(), QStringLiteral("word bold"));
        QVERIFY(fixture.choose(QStringLiteral("Redo")));
        QCOMPARE(fixture.text(), QStringLiteral(" bold"));
        QVERIFY(fixture.choose(QStringLiteral("Select All")));
        QCOMPARE(fixture.editor()->textCursor().selectedText(), QStringLiteral(" bold"));

        QVERIFY(fixture.choose(QStringLiteral("Copy as HTML")));
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasHtml());

        // In the find field, the Edit menu edits the find field.
        QVERIFY(fixture.choose(QStringLiteral("Find")));
        QLineEdit *searchField = fixture.window.findBar()->searchField();
        QTRY_VERIFY(searchField->hasFocus());
        QGuiApplication::clipboard()->setText(QStringLiteral("bold"));
        QVERIFY(fixture.choose(QStringLiteral("Paste")));
        QCOMPARE(searchField->text(), QStringLiteral("bold"));
        QCOMPARE(fixture.text(), QStringLiteral(" bold"));
        QVERIFY(fixture.choose(QStringLiteral("Select All")));
        QCOMPARE(searchField->selectedText(), QStringLiteral("bold"));
        fixture.backend.discardRecovery();
    }

    void runsDocumentAndWindowCommandsFromTheMenu() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("menu.md"));
        writeFile(path, "before\n");

        WindowFixture fixture;
        QVERIFY(fixture.show());
        Backend &backend = fixture.backend;
        backend.open(QUrl::fromLocalFile(path));
        fixture.load(QStringLiteral("after\n"));
        QVERIFY(fixture.choose(QStringLiteral("Save")));
        QCOMPARE(readFile(path), QByteArray("after\n"));

        QVERIFY(fixture.choose(QStringLiteral("Find and Replace")));
        QVERIFY(fixture.window.findBar()->isOpen());
        QVERIFY(fixture.window.findBar()->replaceField()->isVisible());
        fixture.window.findBar()->dismiss();

        const qreal textScale = backend.textScale();
        QVERIFY(fixture.choose(QStringLiteral("Bigger")));
        QVERIFY(backend.textScale() > textScale);
        QVERIFY(fixture.choose(QStringLiteral("Actual Size")));
        QCOMPARE(backend.textScale(), 1.0);
        QVERIFY(fixture.choose(QStringLiteral("Smaller")));
        QVERIFY(backend.textScale() < 1.0);
        QVERIFY(fixture.choose(QStringLiteral("Actual Size")));

        // The way out of full screen keeps its own text and its place in View,
        // where macOS would take an "Exit" item for Quit by its words alone.
        QVERIFY(fixture.choose(QStringLiteral("Enter Full Screen")));
        QTRY_VERIFY(fixture.window.isFullScreen());
        QAction *exitFullScreen = fixture.command(QStringLiteral("Exit Full Screen"));
        QVERIFY(exitFullScreen);
        QCOMPARE(exitFullScreen->menuRole(), QAction::NoRole);
        exitFullScreen->trigger();
        QTRY_VERIFY(!fixture.window.isFullScreen());

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(fixture.choose(QStringLiteral("Save As…")));
        QCOMPARE(saveDialogSpy.count(), 1);
        // The Save As sheet holds the commands until it is dismissed.
        auto *savePanel = fixture.window.findChild<QFileDialog *>(QStringLiteral("savePanel"));
        QVERIFY(savePanel);
        QVERIFY(!fixture.command(QStringLiteral("Open…"))->isEnabled());
        static_cast<QDialog *>(savePanel)->reject();
        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(fixture.choose(QStringLiteral("Open…")));
        QCOMPARE(openDialogSpy.count(), 1);
        backend.discardRecovery();
    }

    void leavesMacSystemShortcutsAlone() {
        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.editor()->setFocus();
        // ⌘H hides the app and ⌃F moves the caret, so neither may be taken.
        QTest::keyClick(&fixture.window, Qt::Key_H, Qt::ControlModifier);
        QVERIFY(!fixture.window.findBar()->isOpen());
        QTest::keyClick(&fixture.window, Qt::Key_F, Qt::MetaModifier);
        QVERIFY(!fixture.window.isFullScreen());
        QTest::keyClick(&fixture.window, Qt::Key_F, Qt::ControlModifier | Qt::MetaModifier);
        QTRY_VERIFY(fixture.window.isFullScreen());
        fixture.window.showNormal();
    }

    void growsTextFromEitherPlusKey() {
        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.editor()->setFocus();
        // Qt resolves a shifted = to Ctrl++ rather than to Ctrl+=, and a layout
        // that puts + on its own key never produces Ctrl+= at all.
        QTest::keyClick(fixture.editor(), Qt::Key_Plus, Qt::ControlModifier);
        QCOMPARE(fixture.backend.textScale(), 1.1);
        QTest::keyClick(fixture.editor(), Qt::Key_Equal, Qt::ControlModifier);
        QCOMPARE(fixture.backend.textScale(), 1.25);
        QTest::keyClick(fixture.editor(), Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(fixture.backend.textScale(), 1.1);
        fixture.backend.resetTextSize();
    }

    void titlesTheWindowAfterItsFile() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WindowFixture fixture;
        QVERIFY(fixture.show());
        QCOMPARE(fixture.window.windowTitle(), QStringLiteral("Untitled.md[*]"));
        QVERIFY(!fixture.window.isWindowModified());

        fixture.editor()->setFocus();
        QTest::keyClick(fixture.editor(), Qt::Key_A);
        QTRY_VERIFY(fixture.window.isWindowModified());

        const QString path = directory.filePath(QStringLiteral("x.md"));
        fixture.backend.saveAs(QUrl::fromLocalFile(path));
        QCOMPARE(fixture.window.windowTitle(), QStringLiteral("x.md[*]"));
        QVERIFY(!fixture.window.isWindowModified());
        QCOMPARE(fixture.window.windowFilePath(), path);
    }

    void showsStatusAndWordCount() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WindowFixture fixture;
        QVERIFY(fixture.show());
        auto *words = fixture.window.findChild<QLabel *>(QStringLiteral("wordCountLabel"));
        auto *status = fixture.window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(words && status);

        fixture.editor()->setFocus();
        QTest::keyClicks(fixture.editor(), QStringLiteral("one"));
        QTRY_COMPARE(words->text(), QStringLiteral("1 Word"));
        QTest::keyClicks(fixture.editor(), QStringLiteral(" two"));
        QTRY_COMPARE(words->text(), QStringLiteral("2 Words"));

        fixture.backend.saveAs(QUrl::fromLocalFile(directory.filePath(QStringLiteral("s.md"))));
        QVERIFY(!fixture.backend.status().isEmpty());
        QCOMPARE(status->text(), fixture.backend.status());
    }

    void asksBeforeClosingWithUnsavedChanges() {
        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.load(QStringLiteral("unsaved"));
        QVERIFY(fixture.backend.modified());
        QSignalSpy cancelled(&fixture.window, &MainWindow::closeCancelled);

        QVERIFY(fixture.choose(QStringLiteral("Close Window")));
        QTRY_VERIFY(sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")));
        QMessageBox *asked = sheet(&fixture.window, QStringLiteral("unsavedChangesSheet"));
        QCOMPARE(asked->text(), QStringLiteral("Do you want to save the changes you made to “Untitled.md”?"));
        QCOMPARE(asked->informativeText(), QStringLiteral("Your changes will be lost if you don't save them."));
        QVERIFY(fixture.window.isVisible());
        asked->button(QMessageBox::Cancel)->click();
        QTRY_VERIFY(!sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")));
        QVERIFY(fixture.window.isVisible());
        QCOMPARE(cancelled.count(), 1);

        // Cmd+W asks the same, and Don't Save closes without saving.
        QTest::keyClick(&fixture.window, Qt::Key_W, Qt::ControlModifier);
        QTRY_VERIFY(sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")));
        sheet(&fixture.window, QStringLiteral("unsavedChangesSheet"))->button(QMessageBox::Discard)->click();
        QTRY_VERIFY(!fixture.window.isVisible());
        QCOMPARE(cancelled.count(), 1);
    }

    void unsavedChangesDialogAnswersToItsKeys() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("keys.md"));
        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.backend.saveAs(QUrl::fromLocalFile(path));
        fixture.load(QStringLiteral("typed"));
        const auto ask = [&fixture]() {
            fixture.window.close();
            return QTest::qWaitFor([&fixture]() {
                return sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")) != nullptr;
            });
        };

        // Esc cancels: the window stays and the file is untouched. (The
        // native alert adds Cmd+. for Cancel and Cmd+D for Don't Save.)
        QVERIFY(ask());
        QTest::keyClick(sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")), Qt::Key_Escape);
        QTRY_VERIFY(!sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")));
        QVERIFY(fixture.window.isVisible());
        QCOMPARE(readFile(path), QByteArray());

        // Return takes Save, and the window closes once the save is done.
        QVERIFY(ask());
        QTest::keyClick(sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")), Qt::Key_Return);
        QTRY_VERIFY(!fixture.window.isVisible());
        QCOMPARE(readFile(path), QByteArray("typed"));
    }

    void dropsAPendingCloseWhenItsSaveFails() {
        auto directory = std::make_unique<QTemporaryDir>();
        QVERIFY(directory->isValid());
        const QString path = directory->filePath(QStringLiteral("gone.md"));
        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.backend.saveAs(QUrl::fromLocalFile(path));
        fixture.load(QStringLiteral("typed"));
        QSignalSpy cancelled(&fixture.window, &MainWindow::closeCancelled);

        // The writer chose Save in the close prompt, and the save failed.
        directory.reset();
        fixture.window.close();
        QTRY_VERIFY(sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")));
        QSignalSpy failed(&fixture.backend, &Backend::saveFailed);
        sheet(&fixture.window, QStringLiteral("unsavedChangesSheet"))->button(QMessageBox::Save)->click();
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(fixture.window.isVisible());
        QCOMPARE(cancelled.count(), 1);

        // A later save, such as an auto-save, must not close the window.
        QTemporaryDir elsewhere;
        fixture.backend.saveAs(QUrl::fromLocalFile(elsewhere.filePath(QStringLiteral("x.md"))));
        QCoreApplication::processEvents();
        QVERIFY(fixture.window.isVisible());
    }

    void holdsAutoSaveWhileTheWriterDecides() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("deciding.md"));
        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.backend.saveAs(QUrl::fromLocalFile(path));
        QSignalSpy savedSpy(&fixture.backend, &Backend::saveSucceeded);

        // Closing right after typing opens the unsaved-changes sheet, and
        // nothing is saved behind it while the writer decides.
        fixture.load(QStringLiteral("undecided"));
        fixture.window.close();
        QTRY_VERIFY(sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")));
        QTest::qWait(1200);
        QCOMPARE(savedSpy.count(), 0);
        QCOMPARE(readFile(path), QByteArray());

        // Cancel lets auto-save carry on.
        sheet(&fixture.window, QStringLiteral("unsavedChangesSheet"))->button(QMessageBox::Cancel)->click();
        QTRY_COMPARE(readFile(path), QByteArray("undecided"));
    }

    void keepsTheWritersTextWhenTheChangeDialogIsDismissed() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("changed.md"));
        writeFile(path, "theirs\n");
        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.backend.open(QUrl::fromLocalFile(path));
        fixture.load(QStringLiteral("mine\n"));

        // Esc closes the sheet without a choice, and the writer's text stays.
        emit fixture.backend.externalChangeDetected(false, true);
        QTRY_VERIFY(sheet(&fixture.window, QStringLiteral("externalChangeSheet")));
        QMessageBox *asked = sheet(&fixture.window, QStringLiteral("externalChangeSheet"));
        QCOMPARE(asked->text(), QStringLiteral("File changed"));
        QCOMPARE(asked->informativeText(),
                 QStringLiteral("This file changed outside Write. Reloading will discard your changes."));
        QTest::keyClick(asked, Qt::Key_Escape);
        QTRY_VERIFY(!sheet(&fixture.window, QStringLiteral("externalChangeSheet")));
        QCOMPARE(fixture.text(), QStringLiteral("mine\n"));
        QVERIFY(fixture.backend.modified());

        // Reload takes the file's text.
        emit fixture.backend.externalChangeDetected(false, true);
        QTRY_VERIFY(sheet(&fixture.window, QStringLiteral("externalChangeSheet")));
        sheet(&fixture.window, QStringLiteral("externalChangeSheet"))->findChild<QPushButton *>(
            QStringLiteral("reloadButton"))->click();
        QTRY_COMPARE(fixture.text(), QStringLiteral("theirs\n"));

        // A removed file is kept by default, as the writer's text is all there is.
        emit fixture.backend.externalChangeDetected(true, false);
        QTRY_VERIFY(sheet(&fixture.window, QStringLiteral("externalChangeSheet")));
        asked = sheet(&fixture.window, QStringLiteral("externalChangeSheet"));
        QCOMPARE(asked->text(), QStringLiteral("File removed"));
        QCOMPARE(asked->defaultButton()->objectName(), QStringLiteral("keepButton"));
        asked->defaultButton()->click();
        fixture.backend.discardRecovery();
    }

    void keepsShortcutsBehindDialogs() {
        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.load(QStringLiteral("unsaved"));
        fixture.window.close();
        QTRY_VERIFY(sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")));

        QTest::keyClick(&fixture.window, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(!fixture.window.findBar()->isOpen());
        // The native menu bar takes its keys before Qt sees them, so its
        // commands have to be off while the sheet is up.
        QVERIFY(!fixture.choose(QStringLiteral("Find")));
        QVERIFY(!fixture.window.findBar()->isOpen());
        sheet(&fixture.window, QStringLiteral("unsavedChangesSheet"))->button(QMessageBox::Cancel)->click();
        QTRY_VERIFY(!sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")));
        QVERIFY(fixture.choose(QStringLiteral("Find")));
        QVERIFY(fixture.window.findBar()->isOpen());
        fixture.backend.discardRecovery();
    }

    void holdsCommandsWhileASheetIsUp() {
        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.load(QStringLiteral("unsaved"));
        fixture.window.close();
        QTRY_VERIFY(sheet(&fixture.window, QStringLiteral("unsavedChangesSheet")));
        QVERIFY(!fixture.window.commandsEnabled());
        QVERIFY(!fixture.command(QStringLiteral("Close Window"))->isEnabled());
        // A second close asks nothing more.
        fixture.window.close();
        QCoreApplication::processEvents();
        int shown = 0;
        for (QMessageBox *box : fixture.window.findChildren<QMessageBox *>())
            shown += box->isVisible();
        QCOMPARE(shown, 1);
        // Minimizing is the window's own business, sheet or no sheet.
        QVERIFY(fixture.command(QStringLiteral("Minimize"))->isEnabled());
        sheet(&fixture.window, QStringLiteral("unsavedChangesSheet"))->button(QMessageBox::Cancel)->click();
        QTRY_VERIFY(fixture.window.commandsEnabled());

        // So it is with the Fonts panel up.
        QVERIFY(fixture.choose(QStringLiteral("Font…")));
        auto *panel = fixture.window.findChild<QFontDialog *>(QStringLiteral("fontsPanel"));
        QVERIFY(panel);
        QTRY_VERIFY(panel->isVisible());
        QVERIFY(!fixture.window.commandsEnabled());
        panel->reject();
        QTRY_VERIFY(fixture.window.commandsEnabled());
        fixture.backend.discardRecovery();
    }

    void choosesTheEditorFontFromTheFontsPanel() {
        WindowFixture fixture;
        QVERIFY(fixture.show());
        // The bundled font is the app's to register, so the test starts from
        // one the system has.
        const QString before = fixture.backend.editorFont();
        fixture.backend.setEditorFont(QStringLiteral("Menlo"));
        QVERIFY(fixture.choose(QStringLiteral("Font…")));
        auto *panel = fixture.window.findChild<QFontDialog *>(QStringLiteral("fontsPanel"));
        QVERIFY(panel);
        QTRY_VERIFY(panel->isVisible());
        QCOMPARE(panel->currentFont().family(), QStringLiteral("Menlo"));
        panel->setCurrentFont(QFont(QStringLiteral("Monaco")));
        panel->accept();
        QCOMPARE(fixture.backend.editorFont(), QStringLiteral("Monaco"));
        QCOMPARE(fixture.editor()->font().family(), QStringLiteral("Monaco"));
        QTRY_VERIFY(fixture.editor()->hasFocus());
        fixture.backend.setEditorFont(before);
    }

    // A file opened from the Open panel lands with the writer back in the
    // text, the caret in view.
    void showsTheCaretAfterOpeningFromTheOpenDialog() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("long.md"));
        QByteArray contents;
        for (int line = 0; line < 3000; ++line)
            contents += "A line of text, number " + QByteArray::number(line) + "\n";
        writeFile(path, contents);

        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.window.findBar()->open(false);
        QVERIFY(fixture.choose(QStringLiteral("Open…")));
        QFileDialog *panel = nullptr;
        QTRY_VERIFY((panel = fixture.window.findChild<QFileDialog *>(QStringLiteral("openPanel")))
                    && panel->isVisible());
        // The test plays the panel's part: QFileDialog cannot be driven
        // offscreen, so it says what the writer chose, as the panel would.
        QCOMPARE(panel->fileMode(), QFileDialog::ExistingFile);
        emit panel->urlSelected(QUrl::fromLocalFile(path));
        static_cast<QDialog *>(panel)->done(QDialog::Accepted);
        QTRY_COMPARE(fixture.backend.fileUrl(), QUrl::fromLocalFile(path));

        QTRY_VERIFY(fixture.editor()->hasFocus());
        QVERIFY(fixture.editor()->viewport()->rect().intersects(fixture.editor()->cursorRect()));
        fixture.backend.discardRecovery();
    }

    void savesAsFromASheet() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("named.md"));
        WindowFixture fixture;
        QVERIFY(fixture.show());
        fixture.load(QStringLiteral("text"));
        QVERIFY(fixture.choose(QStringLiteral("Save As…")));
        QFileDialog *panel = nullptr;
        QTRY_VERIFY((panel = fixture.window.findChild<QFileDialog *>(QStringLiteral("savePanel")))
                    && panel->isVisible());
        QCOMPARE(panel->acceptMode(), QFileDialog::AcceptSave);
        QCOMPARE(panel->windowModality(), Qt::WindowModal);
        QVERIFY(!fixture.window.commandsEnabled());
        emit panel->urlSelected(QUrl::fromLocalFile(path));
        static_cast<QDialog *>(panel)->done(QDialog::Accepted);
        QTRY_COMPARE(readFile(path), QByteArray("text"));
        QTRY_VERIFY(fixture.window.commandsEnabled());
    }

    void runsEveryWindowInOneProcess() {
        WindowManager windows;
        MainWindow *first = windows.newWindow();
        QVERIFY(first);
        QVERIFY(QTest::qWaitForWindowExposed(first));
        QSignalSpy countSpy(&windows, &WindowManager::windowCountChanged);

        // Cmd+N opens the next window here rather than in a process of its own.
        QTest::keyClick(first, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(windows.windows().size(), 2);
        QCOMPARE(countSpy.count(), 1);
        MainWindow *second = windows.windows().constFirst();
        QVERIFY(second != first);
        QCOMPARE(windows.activeWindow(), second);

        // Each window has a document, file watcher, auto-save, and draft of
        // its own.
        Backend *firstBackend = windows.backendFor(first);
        Backend *secondBackend = windows.backendFor(second);
        QVERIFY(firstBackend && secondBackend && firstBackend != secondBackend);
        QVERIFY(!firstBackend->m_recoveryPath.isEmpty());
        QVERIFY(!secondBackend->m_recoveryPath.isEmpty());
        QVERIFY(firstBackend->m_recoveryPath != secondBackend->m_recoveryPath);
        first->editor()->setPlainText(QStringLiteral("only here"));
        QVERIFY(firstBackend->modified());
        QVERIFY(!secondBackend->modified());
        QCOMPARE(second->editor()->toPlainText(), QString());

        // A closed window takes its backend with it, and with it the lock
        // on its draft.
        const QString secondDraftLock = secondBackend->m_recoveryLock->fileName();
        QPointer<Backend> closedBackend(secondBackend);
        QVERIFY(second->close());
        QCOMPARE(windows.windows(), QList<MainWindow *>{first});
        QCOMPARE(countSpy.count(), 2);
        QTRY_VERIFY(!closedBackend);
        QVERIFY(!QFileInfo::exists(secondDraftLock));
        firstBackend->discardRecovery();
    }

    void opensAWindowFromTheMenuBarWithNoWindowOpen() {
        WindowManager windows;
        QAction *newWindow = nullptr;
        for (QAction *action : windows.m_noWindowMenuBar->findChildren<QAction *>()) {
            if (action->text() == QStringLiteral("New Window"))
                newWindow = action;
        }
        QVERIFY(newWindow);
        QCOMPARE(newWindow->shortcut(), QKeySequence(QStringLiteral("Ctrl+N")));

        // With every window closed, Cmd+N reaches the menu bar macOS shows then.
        QVERIFY(newWindow->isEnabled());
        newWindow->trigger();
        QCOMPARE(windows.windows().size(), 1);
        MainWindow *window = windows.windows().constFirst();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        // While a window is open its own menu has the key, so this one stands
        // aside rather than make Cmd+N ambiguous.
        QVERIFY(!newWindow->isEnabled());
        QTest::keyClick(window, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(windows.windows().size(), 2);

        for (MainWindow *open : windows.windows())
            QVERIFY(open->close());
        QVERIFY(windows.windows().isEmpty());
        QVERIFY(newWindow->isEnabled());
        newWindow->trigger();
        QCOMPARE(windows.windows().size(), 1);
    }

    void cascadesNewWindowsAndRemembersTheLastToClose() {
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        WindowManager windows;
        MainWindow *first = windows.newWindow();
        QVERIFY(first);
        QVERIFY(QTest::qWaitForWindowExposed(first));
        const QRect firstPlace(available.topLeft() + QPoint(10, 40), first->minimumSize());
        first->setGeometry(firstPlace);

        // A new window steps down and to the right of the front one, at its
        // size, so it never lands squarely on top of it.
        MainWindow *second = windows.newWindow();
        QVERIFY(second);
        QCOMPARE(second->geometry(), firstPlace.translated(22, 22));

        // Stepping from the front window again passes over the one already
        // there.
        first->activateWindow();
        QTRY_COMPARE(windows.activeWindow(), first);
        MainWindow *stepped = windows.newWindow();
        QVERIFY(stepped);
        QCOMPARE(stepped->geometry(), firstPlace.translated(44, 44));

        // One that would run off the screen starts again from its top left.
        stepped->activateWindow();
        QTRY_COMPARE(windows.activeWindow(), stepped);
        stepped->setGeometry(QRect(available.bottomRight() - QPoint(first->minimumWidth(),
                                                                     first->minimumHeight()),
                                   first->minimumSize()));
        MainWindow *third = windows.newWindow();
        QVERIFY(third);
        const QMargins frame = stepped->windowHandle()->frameMargins();
        QCOMPARE(third->geometry().topLeft(), available.topLeft() + QPoint(frame.left(), frame.top()));

        // Only the last window to close says where the next launch opens.
        QSettings().remove(QStringLiteral("window"));
        QVERIFY(third->close());
        QVERIFY(stepped->close());
        QVERIFY(second->close());
        QVERIFY(!QSettings().contains(QStringLiteral("window/x")));
        first->setGeometry(firstPlace.translated(5, 7));
        QVERIFY(first->close());
        QCOMPARE(QSettings().value(QStringLiteral("window/x")).toInt(), firstPlace.x() + 5);
        QCOMPARE(QSettings().value(QStringLiteral("window/y")).toInt(), firstPlace.y() + 7);
        QCOMPARE(QSettings().value(QStringLiteral("window/width")).toInt(), firstPlace.width());

        MainWindow *reopened = windows.newWindow();
        QVERIFY(reopened);
        QCOMPARE(reopened->geometry(), firstPlace.translated(5, 7));
        QSettings().remove(QStringLiteral("window"));
    }

    void sharesTextSizeAndFontAcrossWindows() {
        WindowManager windows;
        MainWindow *first = windows.newWindow();
        MainWindow *second = windows.newWindow();
        QVERIFY(first && second);
        Backend *firstBackend = windows.backendFor(first);
        Backend *secondBackend = windows.backendFor(second);
        QSignalSpy scaleSpy(&windows, &WindowManager::textScaleChanged);

        // The text size is one size for every window. Only the window it was
        // changed in says so.
        firstBackend->increaseTextSize();
        QCOMPARE(firstBackend->textScale(), 1.1);
        QCOMPARE(secondBackend->textScale(), 1.1);
        QCOMPARE(windows.textScale(), 1.1);
        QCOMPARE(scaleSpy.count(), 1);
        QCOMPARE(scaleSpy.constFirst().constFirst().toReal(), 1.1);
        QCOMPARE(firstBackend->status(), QStringLiteral("Text size 110%"));
        QCOMPARE(secondBackend->status(), QString());
        QCOMPARE(second->editor()->fontPixelSize(), 22);
        QCOMPARE(windows.backendFor(windows.newWindow())->textScale(), 1.1);

        secondBackend->setEditorFont(QStringLiteral("Menlo"));
        QCOMPARE(firstBackend->editorFont(), QStringLiteral("Menlo"));
        QCOMPARE(first->editor()->font().family(), QStringLiteral("Menlo"));

        secondBackend->resetTextSize();
        QCOMPARE(firstBackend->textScale(), 1.0);
        secondBackend->setEditorFont(Backend::defaultEditorFont());
        QSettings().remove(QStringLiteral("editor/font"));
        QSettings().remove(QStringLiteral("text/zoom"));
    }

    void routesFilesOpenedFromFinder() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString first = directory.filePath(QStringLiteral("first.md"));
        const QString second = directory.filePath(QStringLiteral("second.md"));
        const QString third = directory.filePath(QStringLiteral("third.md"));
        writeFile(first, "first");
        writeFile(second, "second");
        writeFile(third, "third");

        WindowManager windows;
        AppEventFilter appEvents(&windows);
        QCoreApplication::instance()->installEventFilter(&appEvents);
        const auto openFromFinder = [](const QString &path) {
            QFileOpenEvent event(QUrl::fromLocalFile(path));
            QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
        };

        // The blank window Write opened with takes the first file.
        MainWindow *blank = windows.newWindow();
        QVERIFY(blank);
        openFromFinder(first);
        QCOMPARE(windows.windows().size(), 1);
        QCOMPARE(windows.backendFor(blank)->fileUrl(), QUrl::fromLocalFile(first));

        // A window with a document keeps it, and the next file gets a window
        // of its own.
        openFromFinder(second);
        QCOMPARE(windows.windows().size(), 2);
        MainWindow *secondWindow = windows.windows().constFirst();
        QCOMPARE(windows.backendFor(secondWindow)->fileUrl(), QUrl::fromLocalFile(second));
        QCOMPARE(windows.backendFor(blank)->fileUrl(), QUrl::fromLocalFile(first));

        // A file that is already open comes forward rather than opening twice,
        // under whatever name it was opened by.
        openFromFinder(directory.filePath(QStringLiteral("./first.md")));
        QCOMPARE(windows.windows().size(), 2);
        QTRY_COMPARE(windows.activeWindow(), blank);

        // An untitled window with writing in it is not blank, but an untouched
        // window behind the front one still is.
        MainWindow *untouched = windows.newWindow();
        MainWindow *written = windows.newWindow();
        QVERIFY(untouched && written);
        written->editor()->setPlainText(QStringLiteral("draft"));
        QVERIFY(windows.backendFor(written)->modified());
        openFromFinder(third);
        QCOMPARE(windows.windows().size(), 4);
        QCOMPARE(windows.backendFor(untouched)->fileUrl(), QUrl::fromLocalFile(third));
        QVERIFY(windows.backendFor(written)->fileUrl().isEmpty());
        windows.backendFor(written)->discardRecovery();
    }

    void reopensAWindowFromTheDock() {
        WindowManager windows;
        AppEventFilter appEvents(&windows);
        QCoreApplication::instance()->installEventFilter(&appEvents);
        const auto send = [](QEvent::Type type) {
            QEvent event(type);
            QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
        };
        // macOS activates Write, if it was not already, and then Qt
        // reports the click as one more activation.
        const auto clickDockIcon = [&send] {
            send(QEvent::ApplicationActivate);
            send(QEvent::ApplicationActivate);
        };
        const auto switchFromAnotherApp = [&send] {
            send(QEvent::ApplicationDeactivate);
            send(QEvent::ApplicationActivate);
        };

        MainWindow *window = windows.newWindow();
        QVERIFY(window);
        clickDockIcon();
        QCOMPARE(windows.windows().size(), 1);

        // Closing the last window leaves Write running. Switching to it
        // shows no window, as in other Mac apps, but the Dock icon brings
        // one back, whether Write was in front or not.
        QVERIFY(window->close());
        QVERIFY(windows.windows().isEmpty());
        switchFromAnotherApp();
        QVERIFY(windows.windows().isEmpty());
        send(QEvent::ApplicationActivate);
        QCOMPARE(windows.windows().size(), 1);

        QVERIFY(windows.activeWindow()->close());
        send(QEvent::ApplicationDeactivate);
        clickDockIcon();
        QCOMPARE(windows.windows().size(), 1);
    }

    void quitsOnlyOnceEveryWindowAgrees() {
        WindowManager windows;
        QuitRecorder quits;
        QCoreApplication::instance()->installEventFilter(&quits);
        AppEventFilter appEvents(&windows);
        QCoreApplication::instance()->installEventFilter(&appEvents);
        const auto quit = [] {
            QEvent event(QEvent::Quit);
            QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
            return event.isAccepted();
        };
        const auto asking = [](MainWindow *window) {
            return sheet(window, QStringLiteral("unsavedChangesSheet"));
        };

        MainWindow *back = windows.newWindow();
        MainWindow *clean = windows.newWindow();
        MainWindow *front = windows.newWindow();
        QVERIFY(back && clean && front);
        back->editor()->setPlainText(QStringLiteral("unsaved"));
        front->editor()->setPlainText(QStringLiteral("unsaved"));

        // The front window asks first, and no window closes while one waits.
        QVERIFY(!quit());
        QTRY_VERIFY(asking(front));
        QVERIFY(!asking(back));
        QCOMPARE(windows.windows().size(), 3);

        // Cancelling that question cancels the quit, and every window stays.
        asking(front)->button(QMessageBox::Cancel)->click();
        QTRY_VERIFY(!asking(front));
        QTest::qWait(50);
        QCOMPARE(windows.windows().size(), 3);
        QVERIFY(!asking(back));
        QCOMPARE(quits.acceptedQuits, 0);

        // Answering it carries the quit on to the next window with changes,
        // before the clean window closes.
        QVERIFY(!quit());
        QTRY_VERIFY(asking(front));
        asking(front)->button(QMessageBox::Discard)->click();
        QTRY_VERIFY(asking(back));
        QCOMPARE(windows.windows(), (QList<MainWindow *>{back, clean}));
        QCOMPARE(quits.acceptedQuits, 0);

        // Once the last question is answered, every window closes and the
        // quit goes through.
        asking(back)->button(QMessageBox::Discard)->click();
        QTRY_COMPARE(quits.acceptedQuits, 1);
        QVERIFY(windows.windows().isEmpty());
    }

    void restoresEveryDraftACrashLeftBehind() {
        // Drafts of its own, apart from any another test or test run left.
        const QString applicationName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(
            QStringLiteral("tst_write-drafts-%1").arg(QCoreApplication::applicationPid()));
        const QString stateDirectory =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const auto restore = qScopeGuard([&] {
            QDir(stateDirectory).removeRecursively();
            QCoreApplication::setApplicationName(applicationName);
        });
        QDir().mkpath(stateDirectory);
        QCOMPARE(Backend::unclaimedRecoveryDrafts(), 0);
        writeFile(QDir(stateDirectory).filePath(QStringLiteral("recovery-7.json")),
                  R"({"fileUrl":"","text":"first draft"})");
        writeFile(QDir(stateDirectory).filePath(QStringLiteral("recovery-8.json")),
                  R"({"fileUrl":"","text":"second draft"})");
        QCOMPARE(Backend::unclaimedRecoveryDrafts(), 2);

        WindowManager windows;
        windows.openStartupWindows();
        QCOMPARE(windows.windows().size(), 2);
        QStringList texts;
        for (MainWindow *window : windows.windows()) {
            texts.append(window->editor()->toPlainText());
            QVERIFY(windows.backendFor(window)->modified());
            windows.backendFor(window)->discardRecovery();
        }
        texts.sort();
        QCOMPARE(texts, QStringList({QStringLiteral("first draft"),
                                     QStringLiteral("second draft")}));
        QCOMPARE(Backend::unclaimedRecoveryDrafts(), 0);

        // Without drafts, Write starts with one blank window.
        WindowManager fresh;
        fresh.openStartupWindows();
        QCOMPARE(fresh.windows().size(), 1);
        QVERIFY(fresh.backendFor(fresh.activeWindow())->isUntouched());
    }

    void followsTheSystemAppearance() {
        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#eeeeee"));

        // Code sits on a shade a little off the page, nowhere near the text.
        const QColor darkPanel(backend.themeCodeBackground());
        QVERIFY(darkPanel != QColor(backend.themeBackground()));
        QVERIFY(darkPanel.lightness() < 60);

        backend.setDarkMode(false);
        QCOMPARE(backend.themeBackground(), QStringLiteral("#ffffff"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#222324"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#2077b2"));
        const QColor lightPanel(backend.themeCodeBackground());
        QVERIFY(lightPanel != QColor(Qt::white));
        QVERIFY(lightPanel.lightness() > 195);

        MainWindow window(&backend);
        const QPalette colours = window.editor()->palette();
        QCOMPARE(colours.color(QPalette::Base), QColor(backend.themeBackground()));
        QCOMPARE(colours.color(QPalette::Text), QColor(backend.themeForeground()));
        QCOMPARE(colours.color(QPalette::Highlight), QColor(backend.themeSelection()));
        QCOMPARE(colours.color(QPalette::HighlightedText), QColor(backend.themeSelectionForeground()));

        // A switch of the system's appearance repaints the open window too.
        backend.setDarkMode(true);
        QCOMPARE(window.editor()->palette().color(QPalette::Base), QColor(QStringLiteral("#101010")));
    }

    void asksAboutUnsavedChangesBeforeClosingOrQuitting() {
        WindowManager windows;
        MainWindow *window = windows.newWindow();
        Backend *backend = windows.backendFor(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->editor()->setPlainText(QStringLiteral("unsaved"));
        QVERIFY(backend->modified());

        QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier);
        QTRY_VERIFY(sheet(window, QStringLiteral("unsavedChangesSheet")));
        QVERIFY(window->isVisible());
        sheet(window, QStringLiteral("unsavedChangesSheet"))->button(QMessageBox::Cancel)->click();
        QTRY_VERIFY(!sheet(window, QStringLiteral("unsavedChangesSheet")));

        // Quitting from the macOS app menu, as Cmd+Q does, sends the
        // application a Quit event; the window must get to ask first.
        AppEventFilter appEvents(&windows);
        QCoreApplication::instance()->installEventFilter(&appEvents);
        QEvent quit(QEvent::Quit);
        QCoreApplication::sendEvent(QCoreApplication::instance(), &quit);
        QVERIFY(!quit.isAccepted());
        QVERIFY(window->isVisible());
        QTRY_VERIFY(sheet(window, QStringLiteral("unsavedChangesSheet")));
        sheet(window, QStringLiteral("unsavedChangesSheet"))->button(QMessageBox::Cancel)->click();
        backend->discardRecovery();
    }

    // Write's interface is widgets: the app carries no Qt Quick or QML, and
    // with them no JavaScript engine.
    void linksNoQtQuick() {
        const QString binary = QFINDTESTDATA("../build/Write.app/Contents/MacOS/Write");
        if (binary.isEmpty())
            QSKIP("Build the app with bin/build first.");
        QProcess otool;
        otool.start(QStringLiteral("otool"), {QStringLiteral("-L"), binary});
        QVERIFY(otool.waitForFinished());
        const QString libraries = QString::fromUtf8(otool.readAllStandardOutput());
        QVERIFY(libraries.contains(QStringLiteral("QtWidgets")));
        QVERIFY2(!libraries.contains(QStringLiteral("QtQuick")), qPrintable(libraries));
        QVERIFY2(!libraries.contains(QStringLiteral("QtQml")), qPrintable(libraries));
    }

    void composesTextWithAnInputMethod() {
        EditorFixture fixture;
        QVERIFY(fixture.show());
        fixture.editor->setFocus();
        fixture.load(QStringLiteral("ab"), 1);

        QInputMethodEvent preedit(QStringLiteral("か"), {});
        QApplication::sendEvent(fixture.editor, &preedit);
        const QRect caret = fixture.editor->cursorRect();
        QVERIFY(caret.isValid());
        QVERIFY(fixture.editor->viewport()->rect().intersects(caret));
        QVERIFY(!fixture.editor->viewport()->grab().isNull());

        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("漢"));
        QApplication::sendEvent(fixture.editor, &commit);
        QCOMPARE(fixture.text(), QStringLiteral("a漢b"));
        fixture.editor->undo();
        QCOMPARE(fixture.text(), QStringLiteral("ab"));
    }

    // Typing near the top of a long document lays out the line it changed,
    // not every line below it.
    void typingInALongDocumentLaysOutOnlyTheEditedLine() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("long.md"));
        QByteArray contents;
        for (int line = 0; line < 50000; ++line)
            contents += "Line " + QByteArray::number(line) + " has words enough to wrap once at this width, or so.\n";
        writeFile(path, contents);

        EditorFixture fixture;
        fixture.window.resize(500, 700);
        QVERIFY(fixture.show());
        fixture.backend.open(QUrl::fromLocalFile(path));
        MarkdownEditor *editor = fixture.editor;
        editor->setFocus();
        PlainTextLayout *layout = fixture.layout();
        QVERIFY(layout);
        const qreal height = layout->documentSize().height();

        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(4);
        editor->setTextCursor(cursor);
        QElapsedTimer timer;
        timer.start();
        for (int key = 0; key < 200; ++key) {
            QTest::keyClick(editor, Qt::Key_X);
            QCOMPARE(layout->lastLayoutBlockCount(), 1);
        }
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(layout->documentSize().height() > height);

        // The last line of text is still where the editor draws it and finds it.
        QTextDocument *document = editor->document();
        const QTextBlock lastLine = document->lastBlock().previous();
        QTextCursor last(document);
        last.setPosition(lastLine.position() + lastLine.length() - 1);
        editor->setTextCursor(last);
        const QRect rect = editor->cursorRect();
        const qreal bottom = layout->blockBoundingRect(lastLine).bottom()
            - editor->verticalScrollBar()->value();
        QVERIFY2(qAbs(rect.bottom() + 1 - bottom) <= 1,
                 qPrintable(QStringLiteral("%1 %2").arg(rect.bottom()).arg(bottom)));
        QCOMPARE(editor->cursorForPosition(QPoint(rect.left() + 1, rect.center().y())).position(),
                 last.position());
    }

    void findsLinesWithoutCopyingTheText() {
        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        const QString text = QStringLiteral("\nfirst\n\n  - item\nlast");
        editor->setProperty("plainText", text);

        // The same answers the editor's text gave, at every position.
        for (int position = 0; position <= text.size(); ++position) {
            QCOMPARE(backend.lineStart(position),
                     position == 0 ? 0 : int(text.lastIndexOf(QLatin1Char('\n'), position - 1)) + 1);
            QCOMPARE(backend.lineEnd(position), int(text.indexOf(QLatin1Char('\n'), position)));
        }
    }

    void remembersEditorFont() {
        // A font macOS always has, beside the bundled default.
        const QString installed = QStringLiteral("Menlo");

        {
            Backend backend;
            QCOMPARE(backend.editorFont(), Backend::defaultEditorFont());
            QSignalSpy fontSpy(&backend, &Backend::editorFontChanged);
            backend.setEditorFont(installed);
            QCOMPARE(fontSpy.count(), 1);
        }

        QCOMPARE(Backend().editorFont(), installed);

        // A remembered font that has since been uninstalled falls back.
        QSettings().setValue(QStringLiteral("editor/font"), QStringLiteral("No Such Font"));
        QCOMPARE(Backend().editorFont(), Backend::defaultEditorFont());
        QSettings().remove(QStringLiteral("editor/font"));
    }

    void findsStrikethroughOutsideCode() {
        const auto markup = MarkdownHighlighter::inlineMarkup(QStringLiteral("a ~~gone~~ b"));
        QCOMPARE(markup.size(), 1);
        QCOMPARE(markup.at(0).kind, MarkdownHighlighter::InlineKind::Strikethrough);
        QCOMPARE(markup.at(0).content.start, 4);
        QCOMPARE(markup.at(0).content.length, 4);

        // Tildes in code, beside space, or in longer runs stay literal.
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("`~~x~~`")).isEmpty());
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("a ~~ b ~~ c")).isEmpty());
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("~~~x~~~")).isEmpty());
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void findsLinkUrlAtPosition() {
        const QString text = QStringLiteral("Read [site](https://example.com/docs) now");
        const int linkStart = text.indexOf(QStringLiteral("site"));

        QCOMPARE(MarkdownHighlighter::linkUrlAt(text, linkStart),
                 QStringLiteral("https://example.com/docs"));
        QCOMPARE(MarkdownHighlighter::linkUrlAt(text, linkStart + 3),
                 QStringLiteral("https://example.com/docs"));
        QVERIFY(MarkdownHighlighter::linkUrlAt(text, linkStart - 1).isEmpty());
        QVERIFY(MarkdownHighlighter::linkUrlAt(text, text.indexOf(QStringLiteral("https"))).isEmpty());

        // Escaped punctuation, as the editor writes for parentheses, reads as itself.
        const QString escaped = QStringLiteral(R"([wiki](https://example.com/a\(b\)))");
        QCOMPARE(MarkdownHighlighter::linkUrlAt(escaped, 1),
                 QStringLiteral("https://example.com/a(b)"));
    }

    void leavesInlineCodeLiteral() {
        QVERIFY(MarkdownHighlighter::inlineMarkup(
                    QStringLiteral("see `default_line_height` and `file_name`")).isEmpty());
        QVERIFY(MarkdownHighlighter::inlineMarkup(
                    QStringLiteral("a_b `c_d`")).isEmpty());
        // Word-boundary underscores inside a code span are still literal.
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("`_private_`")).isEmpty());
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("`a * b * c`")).isEmpty());
        QVERIFY(MarkdownHighlighter::inlineMarkup(
                    QStringLiteral("`[not](a link)`")).isEmpty());

        // Emphasis that merely wraps a code span still applies.
        const auto wrapping = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**`file_name` only**"));
        QCOMPARE(wrapping.size(), 1);
        QCOMPARE(wrapping.at(0).kind, MarkdownHighlighter::InlineKind::Bold);
        QCOMPARE(wrapping.at(0).content.start, 2);
        QCOMPARE(wrapping.at(0).content.length, 16);
        QCOMPARE(wrapping.at(0).markers[0].length, 2);
        QCOMPARE(wrapping.at(0).markers[1].start, 18);
    }

    void keepsIntrawordUnderscoresLiteral() {
        QVERIFY(MarkdownHighlighter::inlineMarkup(
                    QStringLiteral("snake_case_name and default_line_height")).isEmpty());
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("a__b__c")).isEmpty());

        // Emphasis still opens at a word boundary.
        const auto underscoreBold =
            MarkdownHighlighter::inlineMarkup(QStringLiteral("say __bold__ here"));
        QCOMPARE(underscoreBold.size(), 1);
        QCOMPARE(underscoreBold.at(0).content.start, 6);
        QCOMPARE(underscoreBold.at(0).content.length, 4);

        const auto emphasis = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("_italic_, **bold** and mid*word*"));
        QCOMPARE(emphasis.size(), 3);
        QCOMPARE(emphasis.at(0).kind, MarkdownHighlighter::InlineKind::Bold);
        QCOMPARE(emphasis.at(0).content.length, 4);
        QCOMPARE(emphasis.at(1).content.start, 1);
        QCOMPARE(emphasis.at(1).content.length, 6);
        QCOMPARE(emphasis.at(2).content.length, 4);
    }

    void respectsEscapedOpeningBackticks() {
        const auto escaped = MarkdownHighlighter::inlineMarkup(
            QStringLiteral(R"(\` *italic* \`)"));
        QCOMPARE(escaped.size(), 1);
        QCOMPARE(escaped.constFirst().kind, MarkdownHighlighter::InlineKind::Italic);
        QVERIFY(MarkdownHighlighter::codeSpans(QStringLiteral(R"(\` *italic* \`)")).isEmpty());

        const QString mixed = QStringLiteral(R"(\` *italic* `*literal*`)");
        QCOMPARE(MarkdownHighlighter::inlineMarkup(mixed).size(), 1);
        const auto code = MarkdownHighlighter::codeSpans(mixed);
        QCOMPARE(code.size(), 1);
        QCOMPARE(mixed.mid(code.constFirst().start, code.constFirst().length),
                 QStringLiteral("`*literal*`"));

        // An escaped backslash leaves the opening backtick active.
        QVERIFY(MarkdownHighlighter::inlineMarkup(
            QStringLiteral(R"(\\`*literal*`)")).isEmpty());
        QCOMPARE(MarkdownHighlighter::codeSpans(
            QStringLiteral(R"(\\`*literal*`)")).size(), 1);

        // Backslashes inside code do not escape its closing delimiter.
        const auto afterCode = MarkdownHighlighter::inlineMarkup(
            QStringLiteral(R"(`code\` *italic*)"));
        QCOMPARE(afterCode.size(), 1);
        QCOMPARE(afterCode.constFirst().kind, MarkdownHighlighter::InlineKind::Italic);
    }

    void keepsEnclosedCodeStyling_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("kind");
        QTest::newRow("italic") << QStringLiteral("*a `code` b*")
                                << int(MarkdownHighlighter::InlineKind::Italic);
        QTest::newRow("bold") << QStringLiteral("**a `code` b**")
                              << int(MarkdownHighlighter::InlineKind::Bold);
        QTest::newRow("link") << QStringLiteral("[a `code` b](https://example.com)")
                              << int(MarkdownHighlighter::InlineKind::Link);
    }

    void keepsEnclosedCodeStyling() {
        QFETCH(QString, text);
        QFETCH(int, kind);
        QTextDocument document;
        document.setPlainText(text);
        MarkdownHighlighter highlighter(&document);
        highlighter.setTheme(true, "#101010", "#eeeeee", "#5584aa", "#23372b");
        highlighter.rehighlight();

        const auto formatAt = [&document](int position) {
            for (const auto &range : document.firstBlock().layout()->formats()) {
                if (position >= range.start && position < range.start + range.length)
                    return range.format;
            }
            return QTextCharFormat();
        };
        const auto markup = MarkdownHighlighter::inlineMarkup(text);
        QCOMPARE(markup.size(), 1);
        QCOMPARE(int(markup.constFirst().kind), kind);
        const auto prose = formatAt(text.indexOf(QLatin1Char('a')));
        QCOMPARE(prose.fontItalic(), kind == int(MarkdownHighlighter::InlineKind::Italic));
        QCOMPARE(prose.fontWeight() == QFont::Bold,
                 kind == int(MarkdownHighlighter::InlineKind::Bold));
        QCOMPARE(prose.fontUnderline(), kind == int(MarkdownHighlighter::InlineKind::Link));
        QCOMPARE(prose.background().style(), Qt::NoBrush);

        const int codeStart = text.indexOf(QLatin1Char('`'));
        for (int i = codeStart; i < codeStart + 6; ++i) {
            const auto code = formatAt(i);
            QCOMPARE(code.background().color(), QColor("#23372b"));
            QVERIFY(!code.fontItalic());
            QVERIFY(code.fontWeight() != QFont::Bold);
            QVERIFY(!code.fontUnderline());
        }
        for (const auto &marker : markup.constFirst().markers)
            QCOMPARE(formatAt(marker.start).fontPointSize(), 1.0);
    }

    void leavesFencedCodeLiteral() {
        QTextDocument document;
        document.setPlainText(QStringLiteral("prose _italic_\n"
                                            "```ruby\n"
                                            "snake_case_name = *value*\n"
                                            "```\n"
                                            "after _italic_\n"));
        MarkdownHighlighter highlighter(&document);
        highlighter.setTheme(true, "#101010", "#eeeeee", "#5584aa", "#23372b");
        highlighter.rehighlight();

        const auto stateOf = [&document](int blockNumber) {
            return document.findBlockByNumber(blockNumber).userState();
        };
        QCOMPARE(stateOf(0), int(MarkdownHighlighter::Prose));
        QCOMPARE(stateOf(1), int(MarkdownHighlighter::InsideFence));
        QCOMPARE(stateOf(2), int(MarkdownHighlighter::InsideFence));
        QCOMPARE(stateOf(3), int(MarkdownHighlighter::Prose));
        QCOMPARE(stateOf(4), int(MarkdownHighlighter::Prose));

        // The fenced line sits on the code panel, with nothing italic and no
        // marker hidden.
        const QList<QTextLayout::FormatRange> fenced =
            document.findBlockByNumber(2).layout()->formats();
        QCOMPARE(fenced.size(), 1);
        QCOMPARE(fenced.constFirst().length, document.findBlockByNumber(2).text().length());
        QVERIFY(fenced.constFirst().format.background().style() != Qt::NoBrush);
        for (const QTextLayout::FormatRange &range : fenced) {
            QVERIFY(!range.format.fontItalic());
            QVERIFY(range.format.foreground().color() != range.format.background().color());
        }

        const QList<QTextLayout::FormatRange> prose =
            document.findBlockByNumber(4).layout()->formats();
        QVERIFY(std::any_of(prose.cbegin(), prose.cend(),
                            [](const QTextLayout::FormatRange &range) {
                                return range.format.fontItalic();
                            }));
    }

    void opensFencesOnlyWhereMarkdownDoes() {
        QTextDocument document;
        document.setPlainText(QStringLiteral("```inline``` and _italic_\n"
                                            "````md\n"
                                            "```\n"
                                            "still code\n"
                                            "````\n"
                                            "after _italic_\n"));
        MarkdownHighlighter highlighter(&document);
        highlighter.setTheme(true, "#101010", "#eeeeee", "#5584aa", "#23372b");
        highlighter.rehighlight();

        const auto stateOf = [&document](int blockNumber) {
            return document.findBlockByNumber(blockNumber).userState();
        };
        // Backticks that close on the same line are inline code, not a fence.
        QCOMPARE(stateOf(0), int(MarkdownHighlighter::Prose));
        // A four-backtick fence is not closed by three.
        QVERIFY(stateOf(1) >= int(MarkdownHighlighter::InsideFence));
        QVERIFY(stateOf(2) >= int(MarkdownHighlighter::InsideFence));
        QVERIFY(stateOf(3) >= int(MarkdownHighlighter::InsideFence));
        QCOMPARE(stateOf(4), int(MarkdownHighlighter::Prose));
        QCOMPARE(stateOf(5), int(MarkdownHighlighter::Prose));

        QCOMPARE(MarkdownHighlighter::openingFenceLength(QStringLiteral("```ruby")), 3);
        QCOMPARE(MarkdownHighlighter::openingFenceLength(QStringLiteral("    ```")), 0);
        QVERIFY(!MarkdownHighlighter::closesFence(QStringLiteral("``` text"), 3));
    }

    void keepsCaretRangesConsistentWithCode() {
        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        auto *document = qobject_cast<QTextEdit *>(editor.data())->document();
        auto *highlighter = document->findChild<MarkdownHighlighter *>();
        QVERIFY(highlighter);
        editor->setProperty("plainText", QStringLiteral("``` **opening**\n"
                                                   "# _literal_ [link](url)\n"
                                                   "```  \n"
                                                   "*prose*\n"
                                                   "`*literal*`\n"
                                                   "```\n"
                                                   "_unterminated_"));
        highlighter->rehighlight();
        for (int blockNumber : {0, 1, 2, 4, 5, 6}) {
            const auto block = document->findBlockByNumber(blockNumber);
            QVERIFY(backend.hiddenRangesAt(block.position()).isEmpty());
        }
        QCOMPARE(backend.hiddenRangesAt(document->findBlockByNumber(3).position()).size(), 2);

        // Removing a fence rehighlights the following blocks as prose.
        QTextCursor cursor(document);
        cursor.setPosition(document->findBlockByNumber(5).position());
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        QTRY_COMPARE(document->findBlockByNumber(6).userState(), int(MarkdownHighlighter::Prose));
        QCOMPARE(backend.hiddenRangesAt(document->findBlockByNumber(6).position()).size(), 2);
    }

    void rendersBoldItalicRuns() {
        const QString text = QStringLiteral("a ***word*** b");
        const auto markup = MarkdownHighlighter::inlineMarkup(text);
        QCOMPARE(markup.size(), 2);
        QCOMPARE(markup.at(0).kind, MarkdownHighlighter::InlineKind::Bold);
        QCOMPARE(markup.at(1).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(markup.at(1).content.start, 5);
        QCOMPARE(markup.at(1).content.length, 4);

        const auto formats = highlightedFormats(text);
        QVERIFY(formats.at(5).fontItalic());
        QCOMPARE(formats.at(5).fontWeight(), int(QFont::Bold));
        for (int i : {2, 3, 4, 9, 10, 11})
            QCOMPARE(formats.at(i).fontPointSize(), 1.0);
    }

    // Markers nested inside other markup stay hidden, as the caret skips them.
    void hidesMarkersInsideNestedMarkup() {
        const QString linked = QStringLiteral("[**a**](u)");
        const auto linkFormats = highlightedFormats(linked);
        for (int i : {0, 1, 2, 4, 5, 6})
            QCOMPARE(linkFormats.at(i).fontPointSize(), 1.0);
        QCOMPARE(linkFormats.at(3).fontWeight(), int(QFont::Bold));
        QVERIFY(linkFormats.at(3).fontUnderline());

        const QString nested = QStringLiteral("*a __b__ c*");
        const auto nestedFormats = highlightedFormats(nested);
        for (int i : {0, 3, 4, 6, 7, 10})
            QCOMPARE(nestedFormats.at(i).fontPointSize(), 1.0);
        QVERIFY(nestedFormats.at(5).fontItalic());
        QCOMPARE(nestedFormats.at(5).fontWeight(), int(QFont::Bold));
    }

    void honoursBackslashEscapes() {
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("\\*not\\* here")).isEmpty());
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("\\[x](y)")).isEmpty());
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("\\~~not~~")).isEmpty());
        // An escaped backslash leaves the marker alone.
        QCOMPARE(MarkdownHighlighter::inlineMarkup(QStringLiteral("\\\\*yes*")).size(), 1);
    }

    void leavesThematicBreaksAlone() {
        for (const QString &rule : {QStringLiteral("*****"), QStringLiteral("___"),
                                    QStringLiteral("* * *")}) {
            QVERIFY2(MarkdownHighlighter::inlineMarkup(rule).isEmpty(), qPrintable(rule));
            const auto formats = highlightedFormats(rule);
            for (const QTextCharFormat &format : formats) {
                QVERIFY(format.fontWeight() != QFont::Bold);
                QVERIFY(format.fontPointSize() != 1.0);
            }
        }
    }

    void mergesInlineStylesWithTheBlock() {
        // A heading stays bold under italic, a link, or code.
        const auto heading = highlightedFormats(QStringLiteral("# head *it* [l](u) `c`"));
        QVERIFY(heading.at(8).fontItalic());
        QCOMPARE(heading.at(8).fontWeight(), int(QFont::Bold));
        QCOMPARE(heading.at(13).fontWeight(), int(QFont::Bold));
        QCOMPARE(heading.at(20).fontWeight(), int(QFont::Bold));

        // A quote keeps its colour under bold.
        const auto quote = highlightedFormats(QStringLiteral("> quote **b**"));
        QCOMPARE(quote.at(10).fontWeight(), int(QFont::Bold));
        QCOMPARE(quote.at(10).foreground().color(), quote.at(3).foreground().color());
    }

    void recognisesTildeFences() {
        QTextDocument document;
        document.setPlainText(QStringLiteral("~~~js\n"
                                            "*code*\n"
                                            "```\n"
                                            "~~~\n"
                                            "after *it*\n"));
        MarkdownHighlighter highlighter(&document);
        highlighter.rehighlight();
        const auto stateOf = [&document](int blockNumber) {
            return document.findBlockByNumber(blockNumber).userState();
        };
        QVERIFY(stateOf(0) >= int(MarkdownHighlighter::InsideFence));
        QVERIFY(stateOf(1) >= int(MarkdownHighlighter::InsideFence));
        // Backticks do not close a tilde fence.
        QVERIFY(stateOf(2) >= int(MarkdownHighlighter::InsideFence));
        QCOMPARE(stateOf(3), int(MarkdownHighlighter::Prose));
        QCOMPARE(stateOf(4), int(MarkdownHighlighter::Prose));
    }

    void readsLinkDestinationsWithParentheses() {
        QCOMPARE(MarkdownHighlighter::linkUrlAt(QStringLiteral("[a](b(c))"), 1),
                 QStringLiteral("b(c)"));
        QCOMPARE(MarkdownHighlighter::linkUrlAt(QStringLiteral("[a](b) and (c)"), 1),
                 QStringLiteral("b"));
    }

    void reportsThemeChangesOnlyWhenTheyHappen() {
        Backend backend;
        QVERIFY(backend.darkMode());
        QSignalSpy colorsSpy(&backend, &Backend::themeColorsChanged);
        QSignalSpy modeSpy(&backend, &Backend::darkModeChanged);

        backend.setDarkMode(true);
        QCOMPARE(colorsSpy.count(), 0);
        QCOMPARE(modeSpy.count(), 0);

        backend.setDarkMode(false);
        QCOMPARE(colorsSpy.count(), 1);
        QCOMPARE(modeSpy.count(), 1);
    }

    void ignoresInlineMarkdownInsideCodeSpans() {
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("`The_brown_fox` jumps")).size(), 0);
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("`a **b** [c](d)` and `e`")).size(), 0);

        const auto mixed = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("`code_span` then *real* emphasis"));
        QCOMPARE(mixed.size(), 1);
        QCOMPARE(mixed.at(0).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(mixed.at(0).content.start, 18);
    }

    // A code span makes its own contents literal; it does not stop markup that
    // merely encloses one. Only a delimiter landing inside the code span does.
    void keepsMarkupThatEnclosesACodeSpan() {
        const auto spanning = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("_a `b` c_"));
        QCOMPARE(spanning.size(), 1);
        QCOMPARE(spanning.at(0).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(spanning.at(0).content.start, 1);
        QCOMPARE(spanning.at(0).content.length, 7);

        const auto linked = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("[see `code`](url)"));
        QCOMPARE(linked.size(), 1);
        QCOMPARE(linked.at(0).kind, MarkdownHighlighter::InlineKind::Link);
        QCOMPARE(linked.at(0).content.start, 1);
        QCOMPARE(linked.at(0).content.length, 10);

        const auto wrapped = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("*`code`*"));
        QCOMPARE(wrapped.size(), 1);
        QCOMPARE(wrapped.at(0).kind, MarkdownHighlighter::InlineKind::Italic);

        const auto bold = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**a `b` c**"));
        QCOMPARE(bold.size(), 1);
        QCOMPARE(bold.at(0).kind, MarkdownHighlighter::InlineKind::Bold);
        QCOMPARE(bold.at(0).content.start, 2);
        QCOMPARE(bold.at(0).content.length, 7);

        // The closing underscore is inside the code span, so it is not a marker.
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("_a `b_ c` d_")).size(), 0);
    }

    // Emphasis may enclose a code span, so the code pass has to run last for the
    // code span to keep its own styling.
    void stylesAnEnclosedCodeSpanAsCode() {
        QTextDocument document;
        MarkdownHighlighter highlighter(&document);
        document.setPlainText(QStringLiteral("_a `b` c_"));
        // The code panel comes from the theme, as Backend passes it in.
        highlighter.setTheme(true, "#101010", "#eeeeee", "#5584aa", "#23372b");
        // The constructor's own rehighlight is queued, so ask for one directly.
        highlighter.rehighlight();

        const QTextBlock block = document.firstBlock();
        QVERIFY(block.isValid());
        const auto formatAt = [&block](int index) {
            QTextCharFormat found;
            for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
                if (index >= range.start && index < range.start + range.length)
                    found = range.format;
            }
            return found;
        };

        // "a" is italic and carries no code background.
        QVERIFY(formatAt(1).fontItalic());
        QCOMPARE(formatAt(1).background().style(), Qt::NoBrush);

        // The code span keeps the code background and is not italicised.
        QVERIFY(formatAt(4).background().style() != Qt::NoBrush);
        QVERIFY(!formatAt(4).fontItalic());
    }

    // A stray asterisk next to whitespace is not an emphasis delimiter, so it
    // must not swallow a bullet or the operands of arithmetic.
    void keepsWhitespaceDelimitedAsterisksLiteral() {
        // Only `*important*` is markup; the bullet stays a bullet.
        const auto bullet = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("* first *important* point"));
        QCOMPARE(bullet.size(), 1);
        QCOMPARE(bullet.at(0).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(bullet.at(0).content.start, 9);
        QCOMPARE(bullet.at(0).content.length, 9);
        QCOMPARE(bullet.at(0).markers[0].start, 8);
        QCOMPARE(bullet.at(0).markers[1].start, 18);

        // Multiplication stays exactly as typed.
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("2 * 3 * 4")).size(), 0);

        // Real emphasis is unaffected.
        const auto shortWord = MarkdownHighlighter::inlineMarkup(QStringLiteral("*a*"));
        QCOMPARE(shortWord.size(), 1);
        QCOMPARE(shortWord.at(0).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(shortWord.at(0).content.start, 1);
        QCOMPARE(shortWord.at(0).content.length, 1);

        const auto longPhrase =
            MarkdownHighlighter::inlineMarkup(QStringLiteral("*longer text*"));
        QCOMPARE(longPhrase.size(), 1);
        QCOMPARE(longPhrase.at(0).content.length, 11);

        const auto mixed = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic*"));
        QCOMPARE(mixed.size(), 2);
        QCOMPARE(mixed.at(0).kind, MarkdownHighlighter::InlineKind::Bold);
        QCOMPARE(mixed.at(1).kind, MarkdownHighlighter::InlineKind::Italic);
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void copiesRenderedHtml() {
        WindowFixture fixture;
        fixture.load(QStringLiteral("# Title\n\nFirst **bold**.\n\nSecond."));
        QVERIFY(fixture.choose(QStringLiteral("Copy as HTML")));

        const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
        QVERIFY(mimeData);
        QVERIFY(mimeData->hasHtml());
        const QString html = mimeData->html();
        QVERIFY(html.contains(QStringLiteral("<h1>")));
        QVERIFY(html.contains(QStringLiteral("<p>First ")));
        QVERIFY(html.contains(QStringLiteral("<body>")));
        QVERIFY(html.contains(QStringLiteral("font-weight:700;\">bold</span>")));
        QVERIFY(!html.contains(QStringLiteral("font-family")));
        QVERIFY(!html.contains(QStringLiteral("margin")));
        QCOMPARE(mimeData->text(), QStringLiteral("Title\nFirst bold.\nSecond."));
    }


    void stepsTextSize() {
        Backend backend;
        QSignalSpy textScaleSpy(&backend, &Backend::textScaleChanged);
        QCOMPARE(backend.textScale(), 1.0);

        backend.increaseTextSize();
        QCOMPARE(backend.textScale(), 1.1);
        backend.decreaseTextSize();
        backend.decreaseTextSize();
        QCOMPARE(backend.textScale(), 0.9);
        QCOMPARE(backend.status(), QStringLiteral("Text size 90%"));

        // The ends of the ladder hold instead of running away.
        for (int step = 0; step < 8; ++step)
            backend.decreaseTextSize();
        QCOMPARE(backend.textScale(), 0.67);
        const int settledCount = textScaleSpy.count();
        backend.decreaseTextSize();
        QCOMPARE(textScaleSpy.count(), settledCount);

        // A new window picks the size back up.
        Backend reopened;
        QCOMPARE(reopened.textScale(), 0.67);

        backend.resetTextSize();
        QCOMPARE(backend.textScale(), 1.0);
        QCOMPARE(Backend().textScale(), 1.0);
    }


    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

    void autoSavesTitledDocumentsAfterEdits() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("draft.md"));

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);

        // Give the document a home on disk, then edit it without saving again.
        backend.saveAs(QUrl::fromLocalFile(path));
        QSignalSpy savedSpy(&backend, &Backend::saveSucceeded);
        editor->setProperty("plainText", QStringLiteral("auto-saved body"));

        // The debounced timer should flush to disk with no manual save.
        QTRY_VERIFY(savedSpy.count() >= 1);
        QVERIFY(!backend.modified());

        QFile written(path);
        QVERIFY(written.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(QString::fromUtf8(written.readAll()),
                 QStringLiteral("auto-saved body"));
    }

    void doesNotAutoSaveUntitledDocuments() {
        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);

        QSignalSpy savedSpy(&backend, &Backend::saveSucceeded);
        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        editor->setProperty("plainText", QStringLiteral("nameless draft"));

        // Wait past the auto-save debounce: an untitled draft must never auto-save
        // or silently raise the save picker. Crash recovery still covers it.
        QTest::qWait(1800);
        QCOMPARE(savedSpy.count(), 0);
        QCOMPARE(saveDialogSpy.count(), 0);
        QVERIFY(backend.modified());
        // Leave no draft behind for the next test's backend to recover.
        backend.discardRecovery();
    }

    void holdsAutoSaveWhenTheFileChangesElsewhere() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("shared.md"));

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        backend.saveAs(QUrl::fromLocalFile(path));
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        // Another program writes while our auto-save is pending.
        editor->setProperty("plainText", QStringLiteral("mine"));
        writeFile(path, "theirs");

        // Their version survives the debounce, and further typing.
        QTRY_VERIFY(externalChangeSpy.count() >= 1);
        editor->setProperty("plainText", QStringLiteral("mine, still typing"));
        QTest::qWait(1200);
        QCOMPARE(readFile(path), QByteArray("theirs"));
        QVERIFY(backend.modified());

        // Keeping our version is a choice to overwrite, so auto-save resumes.
        backend.keepExternalVersion();
        QTRY_COMPARE(readFile(path), QByteArray("mine, still typing"));
        QVERIFY(!backend.modified());
    }

    void checksTheFileBeforeEachAutoSave() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("unwatched.md"));

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        backend.saveAs(QUrl::fromLocalFile(path));
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        // Change the file without the watcher seeing it, then let auto-save run.
        writeFileUnwatched(backend, path, "theirs");
        editor->setProperty("plainText", QStringLiteral("mine"));

        QTRY_VERIFY(externalChangeSpy.count() >= 1);
        QTest::qWait(700);
        QCOMPARE(readFile(path), QByteArray("theirs"));
        QVERIFY(backend.modified());
        backend.discardRecovery();
    }

    void resumesAutoSaveOnlyAfterAnExplicitSave() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("dismissed.md"));

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        backend.saveAs(QUrl::fromLocalFile(path));
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        writeFile(path, "theirs");
        QTRY_VERIFY(externalChangeSpy.count() >= 1);

        // The prompt is dismissed (Escape): neither reload nor keep is chosen.
        editor->setProperty("plainText", QStringLiteral("mine"));
        QTest::qWait(1200);
        QCOMPARE(readFile(path), QByteArray("theirs"));

        backend.save();
        QCOMPARE(readFile(path), QByteArray("mine"));

        editor->setProperty("plainText", QStringLiteral("mine, auto-saved again"));
        QTRY_COMPARE(readFile(path), QByteArray("mine, auto-saved again"));
    }

    void resumesAutoSaveAfterReloading() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("reloaded.md"));

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        backend.saveAs(QUrl::fromLocalFile(path));
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        writeFile(path, "theirs");
        QTRY_VERIFY(externalChangeSpy.count() >= 1);
        backend.reloadFromDisk();
        QCOMPARE(editor->property("plainText").toString(), QStringLiteral("theirs"));

        editor->setProperty("plainText", QStringLiteral("theirs, edited"));
        QTRY_COMPARE(readFile(path), QByteArray("theirs, edited"));
    }

    // Saving writes UTF-8 with plain line endings, so a file holding anything
    // else is only rewritten by an explicit save, never by typing.
    void keepsOtherEncodingsAndLineEndingsOutOfAutoSave() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QList<QByteArray> originals{QByteArray("one\r\ntwo\r\n"),
                                          QByteArray("caf\xe9\n")};
        for (const QByteArray &original : originals) {
            const QString path = directory.filePath(QStringLiteral("kept.md"));
            writeFile(path, original);

            Backend backend;
            QScopedPointer<QObject> editor(makeEditor(backend));
            QVERIFY(editor);
            backend.open(QUrl::fromLocalFile(path));
            QVERIFY(backend.status().contains(QStringLiteral("Auto-save is off")));

            editor->setProperty("plainText", QStringLiteral("changed\n"));
            QVERIFY(!backend.m_autoSaveTimer.isActive());
            QTest::qWait(700);
            QCOMPARE(readFile(path), original);

            // Keeping our version over an outside edit does not lift it either.
            backend.keepExternalVersion();
            QVERIFY(!backend.m_autoSaveTimer.isActive());

            // An explicit save still writes.
            backend.save();
            QCOMPARE(readFile(path), QByteArray("changed\n"));
        }
    }

    void startsANamedFileThatIsNotThereYet() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("new.md"));

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        QSignalSpy externalSpy(&backend, &Backend::externalChangeDetected);

        backend.openOrStartFile(QUrl::fromLocalFile(path));
        QCOMPARE(backend.fileName(), QStringLiteral("new.md"));
        QCOMPARE(backend.status(), QStringLiteral("New file new.md"));
        QVERIFY(!backend.modified());
        QVERIFY(!QFileInfo::exists(path));

        // Typing does not auto-save the file into existence.
        editor->setProperty("plainText", QStringLiteral("first words"));
        QVERIFY(backend.modified());
        QTest::qWait(800);
        QVERIFY(!QFileInfo::exists(path));

        // The first save lands on the name given, without asking for one.
        QSignalSpy saveAsSpy(&backend, &Backend::saveDialogRequested);
        backend.save();
        QCOMPARE(saveAsSpy.count(), 0);
        QCOMPARE(readFile(path), QByteArray("first words"));
        QVERIFY(!backend.modified());

        // From then on it is an ordinary file, auto-saved like any other.
        editor->setProperty("plainText", QStringLiteral("more words"));
        QTRY_COMPARE(readFile(path), QByteArray("more words"));
        QCOMPARE(externalSpy.count(), 0);
    }

    void refusesANewFileInAMissingFolder() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("missing/new.md"));

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        backend.openOrStartFile(QUrl::fromLocalFile(path));
        QCOMPARE(backend.status(), QStringLiteral("Could not open new.md."));
        QVERIFY(backend.fileName() != QStringLiteral("new.md"));
    }

    void namesARelativeFileByItsFullPath() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString previous = QDir::currentPath();
        QVERIFY(QDir::setCurrent(directory.path()));

        Backend backend;
        backend.openOrStartFile(QUrl::fromLocalFile(QStringLiteral("relative.md")));
        QDir::setCurrent(previous);

        // A recovery draft written later must not depend on the directory.
        // The working directory comes back with symlinks resolved, as macOS
        // keeps temporary files under /var, a link to /private/var.
        QCOMPARE(backend.fileUrl().toLocalFile(),
                 QDir(QFileInfo(directory.path()).canonicalFilePath())
                     .absoluteFilePath(QStringLiteral("relative.md")));
    }

    void saysWhenARecoveredDraftKeepsAFileFromOpening() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("asked.md"));
        writeFile(path, "on disk");

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        // As after restoring a draft: the window holds unsaved text.
        editor->setProperty("plainText", QStringLiteral("recovered"));

        backend.openOrStartFile(QUrl::fromLocalFile(path));
        QCOMPARE(backend.status(),
                 QStringLiteral("Recovered unsaved changes; asked.md was not opened."));
        QCOMPARE(editor->property("plainText").toString(), QStringLiteral("recovered"));
        backend.discardRecovery();
    }

    void savesInAFolderItCannotWriteNewFilesIn() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("shared.md"));
        writeFile(path, "before");
        QFile::setPermissions(directory.path(), QFile::ReadOwner | QFile::ExeOwner);

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("plainText", QStringLiteral("after"));
        backend.save();
        QFile::setPermissions(directory.path(),
                              QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        QVERIFY(!backend.modified());
        QCOMPARE(readFile(path), QByteArray("after"));
    }

    void removesARecoveryDraftItCannotRead() {
        Backend backend;
        const QString draft = backend.m_recoveryPath;
        QVERIFY(!draft.isEmpty());
        writeFile(draft, "not json");

        // Attaching the document is when a draft is restored.
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        QVERIFY(!QFileInfo::exists(draft));
        QVERIFY(!backend.modified());
    }

    void keepsWatchingAFileReplacedFromOutside() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("watched.md"));
        writeFile(path, "first");

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        backend.open(QUrl::fromLocalFile(path));
        QSignalSpy externalSpy(&backend, &Backend::externalChangeDetected);

        // Editors and sync tools often replace a file rather than write into it.
        const QString replacement = directory.filePath(QStringLiteral("replacement"));
        writeFile(replacement, "second");
        QVERIFY(QFile::remove(path));
        QVERIFY(QFile::rename(replacement, path));
        QTRY_VERIFY(externalSpy.count() >= 1);
        QTRY_VERIFY(backend.m_fileWatcher.files().contains(path));
    }

    void opensAnExistingFileFromTheCommandLine() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("notes.md"));
        writeFile(path, "already here");

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        backend.openOrStartFile(QUrl::fromLocalFile(path));
        QCOMPARE(editor->property("plainText").toString(), QStringLiteral("already here"));
        QCOMPARE(backend.status(), QStringLiteral("Opened notes.md"));
    }

    void keepsDiscardedChangesOutOfTheFile() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("draft.md"));

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        editor->setProperty("plainText", QStringLiteral("original"));
        backend.saveAs(QUrl::fromLocalFile(path));

        // The writer edits, then picks Discard to open a file that fails.
        backend.holdAutoSave(true);
        editor->setProperty("plainText", QStringLiteral("changes to throw away"));
        backend.discardRecovery();
        backend.open(QUrl::fromLocalFile(directory.filePath(QStringLiteral("missing.md"))));
        backend.holdAutoSave(false);

        QTest::qWait(800);
        QCOMPARE(readFile(path), QByteArray("original"));
    }

    void forgetsTheSavedTextWhenTheFileChangedUnseen() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("draft.md"));

        Backend backend;
        QScopedPointer<QObject> editor(makeEditor(backend));
        QVERIFY(editor);
        editor->setProperty("plainText", QStringLiteral("original"));
        backend.saveAs(QUrl::fromLocalFile(path));

        // The file changes without the watcher noticing; the pre-save check
        // catches it.
        QSignalSpy externalSpy(&backend, &Backend::externalChangeDetected);
        writeFileUnwatched(backend, path, "theirs");
        editor->setProperty("plainText", QStringLiteral("mine"));
        QTRY_VERIFY(externalSpy.count() >= 1);

        // Undoing back to the old text is not a return to what is on disk.
        editor->setProperty("plainText", QStringLiteral("original"));
        QVERIFY(backend.modified());
        backend.discardRecovery();
    }

    void doesNotAutoSaveARecoveredDraftOverItsFile() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("recovered.md"));
        const QUrl url = QUrl::fromLocalFile(path);

        {
            // A session that ends without saving leaves a recovery draft.
            Backend crashed;
            QScopedPointer<QObject> editor(makeEditor(crashed));
            QVERIFY(editor);
            crashed.saveAs(url);
            editor->setProperty("plainText", QStringLiteral("draft"));
            crashed.m_autoSaveSuspended = true; // stand in for a crash before auto-save
            QTest::qWait(1000);
        }
        QCOMPARE(readFile(path), QByteArray());

        // The file moves on before Write comes back.
        writeFile(path, "newer on disk");

        // Earlier tests may have left drafts of their own; claim until ours.
        for (int attempt = 0; attempt < 100; ++attempt) {
            Backend restored;
            QScopedPointer<QObject> editor(makeEditor(restored));
            QVERIFY(editor);
            if (restored.fileUrl() != url) {
                restored.discardRecovery();
                continue;
            }

            QCOMPARE(editor->property("plainText").toString(), QStringLiteral("draft"));
            editor->setProperty("plainText", QStringLiteral("draft, edited"));
            QTest::qWait(1200);
            QCOMPARE(readFile(path), QByteArray("newer on disk"));
            QVERIFY(restored.modified());

            restored.save();
            QCOMPARE(readFile(path), QByteArray("draft, edited"));
            return;
        }
        QFAIL("The recovery draft was not restored.");
    }

    void revertsEmptyDocumentToCleanState() {
        Backend backend;
        QTextDocument *document = attachTextEdit(&backend);
        QVERIFY(document);

        QVERIFY(!backend.modified());

        document->setPlainText(QStringLiteral("hello"));
        QVERIFY(backend.modified());

        document->setPlainText(QString());
        QVERIFY(!backend.modified());
    }

    void revertsSavedDocumentToCleanState() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("doc.md"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QCOMPARE(file.write("hello"), qint64(5));
        file.close();

        Backend backend;
        QTextDocument *document = attachTextEdit(&backend);
        QVERIFY(document);

        backend.open(QUrl::fromLocalFile(path));
        QVERIFY(!backend.modified());

        document->setPlainText(QStringLiteral("hello world"));
        QVERIFY(backend.modified());

        document->setPlainText(QStringLiteral("hello"));
        QVERIFY(!backend.modified());
    }

    void keepsADocumentUnsavedAfterItsFileDisappears() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("gone.md"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QCOMPARE(file.write("hello"), qint64(5));
        file.close();

        Backend backend;
        QTextDocument *document = attachTextEdit(&backend);
        QVERIFY(document);

        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);
        backend.open(QUrl::fromLocalFile(path));
        QVERIFY(!backend.modified());

        QVERIFY(QFile::remove(path));
        QTRY_COMPARE(externalChangeSpy.count(), 1);

        // The writer can dismiss the dialog without picking a version, so the
        // text the deleted file used to hold is not a saved state to return to.
        document->setPlainText(QStringLiteral("hello world"));
        document->setPlainText(QStringLiteral("hello"));
        QVERIFY(backend.modified());

        // Nor is emptying it: an empty document only reads as clean while there
        // is no file behind it.
        document->setPlainText(QString());
        QVERIFY(backend.modified());
    }

    // The character formats the highlighter gives a single line of text.
private:
    static QList<QTextCharFormat> highlightedFormats(const QString &text) {
        QTextDocument document;
        document.setPlainText(text);
        MarkdownHighlighter highlighter(&document);
        highlighter.setTheme(true, "#101010", "#eeeeee", "#5584aa", "#23372b");
        highlighter.rehighlight();
        QList<QTextCharFormat> formats(text.length());
        for (const auto &range : document.firstBlock().layout()->formats()) {
            for (int i = range.start; i < range.start + range.length; ++i)
                formats[i] = range.format;
        }
        return formats;
    }

    // An editor on the backend's document, its text as the plainText property.
    // It tells the backend of each edit, as Write's own does.
    static MarkdownEditor *makeEditor(Backend &backend) {
        return new MarkdownEditor(&backend);
    }

    static void writeFile(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(contents), qint64(contents.size()));
    }

    // Writes while the backend is not watching, as when an event is missed.
    static void writeFileUnwatched(Backend &backend, const QString &path,
                                   const QByteArray &contents) {
        backend.m_fileWatcher.removePath(path);
        writeFile(path, contents);
    }

    static QByteArray readFile(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }

    // The document of an editor that goes with the backend.
    static QTextDocument *attachTextEdit(Backend *backend) {
        auto *editor = new MarkdownEditor(backend);
        QObject::connect(backend, &QObject::destroyed, editor, &QObject::deleteLater);
        return editor->document();
    }

    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(WriteTest)
#include "tst_write.moc"
