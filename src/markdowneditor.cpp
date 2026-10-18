#include "markdowneditor.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QToolTip>
#include <QWheelEvent>

#include "backend.h"
#include "markdownedits.h"
#include "markdownhighlighter.h"
#include "plaintextlayout.h"

namespace {

const QString startWritingHint = QStringLiteral("# Start writing");
constexpr qreal spaceBelowText = 220;

}

MarkdownEditor::MarkdownEditor(Backend *backend, QWidget *parent)
    : QTextEdit(parent), m_backend(backend) {
    setObjectName(QStringLiteral("sourceEditor"));
    setFrameShape(QFrame::NoFrame);
    setAcceptRichText(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    // Wrapping at a fixed width lays the document out again only when the
    // column changes; a wider window just moves it.
    setLineWrapMode(QTextEdit::FixedPixelWidth);

    // The space around the column is the layout's padding, not the
    // document's margin, which an undo could take back.
    const int caretWidth = cursorWidth();
    document()->setDocumentMargin(0);
    m_layout = new PlainTextLayout(document());
    // A wrapped list item, quote or heading continues under its text.
    m_layout->setHangingIndentRule(&MarkdownHighlighter::hangingIndentLength);
    // Fenced code sits on one panel, not a box behind each line's text.
    m_layout->setBlockBackgroundRule([this](const QTextBlock &block) {
        return MarkdownHighlighter::isFencedCode(block) ? QColor(m_backend->themeCodeBackground())
                                                        : QColor();
    });
    document()->setDocumentLayout(m_layout);
    setCursorWidth(caretWidth);
    document()->clearUndoRedoStacks();
    m_backend->attachDocument(document());

    connect(m_backend, &Backend::editorFontChanged, this, &MarkdownEditor::applyFont);
    connect(m_backend, &Backend::textScaleChanged, this, &MarkdownEditor::applyFont);
    connect(m_backend, &Backend::themeColorsChanged, this, &MarkdownEditor::applyTheme);
    connect(m_backend, &Backend::darkModeChanged, this, &MarkdownEditor::applyTheme);
    connect(m_backend, &Backend::darkModeChanged, this, &MarkdownEditor::applySearchHighlights);
    connect(this, &QTextEdit::cursorPositionChanged, this, &MarkdownEditor::ensureCaretVisible);
    // Backend keeps the line height on what was typed, and tells an edit
    // from a pass of formatting.
    connect(this, &QTextEdit::textChanged, this, [this]() {
        if (m_backend->editorTextChanged())
            emit edited();
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &MarkdownEditor::viewChanged);
    // A link that scrolls or is edited away from the pointer is no longer under it.
    connect(this, &MarkdownEditor::viewChanged, this, &MarkdownEditor::forgetHoveredLink);
    connect(this, &QTextEdit::textChanged, this, &MarkdownEditor::forgetHoveredLink);
    viewport()->setMouseTracking(true);
    connect(m_layout, &QAbstractTextDocumentLayout::documentSizeChanged, this,
            &MarkdownEditor::viewChanged);
    applyTheme();
    applyFont();
}

int MarkdownEditor::columnWidth(qreal averageCharWidth, int windowWidth) {
    return qMin(qRound(averageCharWidth * 65),
                qMin(qMax(360, windowWidth - qRound(averageCharWidth * 20)),
                     qMax(0, windowWidth - 48)));
}

int MarkdownEditor::fontPixelSize() const {
    return qMax(1, qRound(20 * m_backend->textScale()));
}

void MarkdownEditor::applyFont() {
    QFont writing(Backend::fontFamily(m_backend->editorFont()));
    writing.setPixelSize(fontPixelSize());
    writing.setWeight(QFont::Normal);
    // Shaping Latin text only kerns and ligates it, which a monospaced font
    // has no use for, and it is most of the cost of laying out a long
    // document. Other scripts still shape.
    if (m_backend->editorFontFixedPitch())
        writing.setStyleStrategy(QFont::PreferNoShaping);
    setFont(writing);
    // The hidden markers are measured from the document's font.
    m_backend->documentFontChanged();
    placeColumn();
}

void MarkdownEditor::applyTheme() {
    QPalette colours = palette();
    colours.setColor(QPalette::Base, QColor(m_backend->themeBackground()));
    colours.setColor(QPalette::Text, QColor(m_backend->themeForeground()));
    colours.setColor(QPalette::Highlight, QColor(m_backend->themeSelection()));
    colours.setColor(QPalette::HighlightedText, QColor(m_backend->themeSelectionForeground()));
    setPalette(colours);
    viewport()->update();
}

QColor MarkdownEditor::mutedColour() const {
    return QColor(m_backend->darkMode() ? QStringLiteral("#909191") : QStringLiteral("#aeb1b5"));
}

void MarkdownEditor::placeColumn() {
    const int column = columnWidth(QFontMetricsF(font()).averageCharWidth(), width());
    if (lineWrapColumnOrWidth() != column)
        setLineWrapColumnOrWidth(column);
    const int viewWidth = viewport()->width();
    const qreal left = qMax(0, 24 + qRound((viewWidth - 48 - column) / 2.0));
    const qreal right = qMax(0.0, viewWidth - left - column);
    const qreal top = qMax(42, qRound(0.05 * window()->height()));
    m_layout->setPadding(QMarginsF(left, top, right, spaceBelowText));
}

void MarkdownEditor::ensureCaretVisible() {
    const int margin = 2 * fontPixelSize();
    const QRect caret = cursorRect();
    const int height = viewport()->height();
    QScrollBar *bar = verticalScrollBar();
    if (caret.bottom() + margin > height)
        bar->setValue(bar->value() + caret.bottom() + margin - height);
    else if (caret.top() - margin < 0)
        bar->setValue(bar->value() + caret.top() - margin);
}

std::pair<int, int> MarkdownEditor::visiblePositions() const {
    const QRect view = viewport()->rect();
    return {cursorForPosition(view.topLeft()).position(),
            cursorForPosition(view.bottomRight()).position()};
}

void MarkdownEditor::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    placeColumn();
    emit viewChanged();
}

void MarkdownEditor::paintEvent(QPaintEvent *event) {
    QTextEdit::paintEvent(event);
    if (!document()->isEmpty() || hasFocus())
        return;

    // Where the first line would be, in the writing font.
    const QRectF line = m_layout->blockBoundingRect(document()->firstBlock())
        .translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    QPainter painter(viewport());
    painter.setFont(font());
    painter.setPen(mutedColour());
    painter.drawText(QRectF(line.left(), line.top(), lineWrapColumnOrWidth(), line.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, startWritingHint);
}

void MarkdownEditor::focusInEvent(QFocusEvent *event) {
    QTextEdit::focusInEvent(event);
    viewport()->update();
}

void MarkdownEditor::focusOutEvent(QFocusEvent *event) {
    QTextEdit::focusOutEvent(event);
    viewport()->update();
}

void MarkdownEditor::toggleWrap(const QString &marker) {
    setFocus();
    QTextCursor cursor = textCursor();
    MarkdownEdits::toggleWrap(cursor, marker, m_backend->emphasisSpanAt(cursor.position(), marker));
    setTextCursor(cursor);
}

void MarkdownEditor::insertLink() {
    setFocus();
    QTextCursor cursor = textCursor();
    MarkdownEdits::insertLink(cursor, m_backend->clipboardUrl());
    setTextCursor(cursor);
}

void MarkdownEditor::keyPressEvent(QKeyEvent *event) {
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool command = modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool shift = modifiers & Qt::ShiftModifier;
    QTextCursor cursor = textCursor();
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (command)
            break;
        MarkdownEdits::smartReturn(cursor, shift, m_backend->insideFence(cursor.position()));
        setTextCursor(cursor);
        return;
    case Qt::Key_Backspace:
        if (command || !MarkdownEdits::deleteParagraphBreakBehind(cursor))
            break;
        setTextCursor(cursor);
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
        if (command || shift)
            break;
        moveCaretVisibly(event->key() == Qt::Key_Right ? 1 : -1);
        return;
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        if (command)
            break;
        movePage(event->key() == Qt::Key_PageDown ? 1 : -1, shift);
        return;
    default:
        break;
    }
    QTextEdit::keyPressEvent(event);
}

// A URL pasted or dropped over selected text links that text; anything else
// goes in as plain text.
void MarkdownEditor::insertFromMimeData(const QMimeData *source) {
    QTextCursor cursor = textCursor();
    if (!MarkdownEdits::pasteUrlAsLink(cursor, Backend::linkUrlFrom(source)))
        MarkdownEdits::pastePlainText(cursor, source->text());
    setTextCursor(cursor);
}

// Left and Right step over whole characters, as an emoji is several code
// units, and over the Markdown markers the highlighter hides.
void MarkdownEditor::moveCaretVisibly(int direction) {
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        cursor.setPosition(direction > 0 ? cursor.selectionEnd() : cursor.selectionStart());
        setTextCursor(cursor);
        return;
    }
    const int last = document()->characterCount() - 1;
    const int position = qBound(0, m_backend->cursorPositionStep(cursor.position(), direction), last);
    cursor.setPosition(skipHidden(position, direction));
    setTextCursor(cursor);
}

int MarkdownEditor::skipHidden(int position, int direction) const {
    const QList<TextRange> ranges = m_backend->hiddenRangesAt(position);
    // One marker can end where the next begins, so look again after a jump.
    for (bool moved = true; moved;) {
        moved = false;
        for (const TextRange &range : ranges) {
            if (direction > 0 && position >= range.start && position < range.end) {
                position = range.end;
                moved = true;
            } else if (direction < 0 && position > range.start && position <= range.end) {
                position = range.start;
                moved = true;
            }
        }
    }
    return position;
}

void MarkdownEditor::movePage(int direction, bool extendSelection) {
    const int lineSize = fontPixelSize();
    const int step = qMax(lineSize, viewport()->height() - 2 * lineSize);
    const QRect caret = cursorRect();
    const int scrolled = verticalScrollBar()->value();
    const int target = qMax(0, scrolled + caret.y() + caret.height() / 2 + direction * step);
    const int position = cursorForPosition(QPoint(caret.x(), target - scrolled)).position();
    QTextCursor cursor = textCursor();
    cursor.setPosition(position, extendSelection ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
    setTextCursor(cursor);
}

// The nearest caret can be past the end of a line, so only a character the
// point is inside counts: the one on either side of that caret, whichever
// way the text runs.
QString MarkdownEditor::linkAt(const QPoint &point) const {
    const int length = document()->characterCount() - 1;
    const int caret = cursorForPosition(point).position();
    QTextCursor probe(document());
    for (int position = qMax(0, caret - 1); position <= caret && position < length; ++position) {
        probe.setPosition(position);
        const QRect box = cursorRect(probe);
        probe.setPosition(position + 1);
        const QRect next = cursorRect(probe);
        // A hidden marker has no width; only a line's last character needs an
        // estimate.
        const int edge = next.y() == box.y()
            ? next.x() : box.x() + qRound(QFontMetricsF(font()).averageCharWidth());
        if (point.y() >= box.y() && point.y() < box.y() + box.height()
                && point.x() >= qMin(box.x(), edge) && point.x() < qMax(box.x(), edge))
            return m_backend->linkUrlAt(position);
    }
    return {};
}

QString MarkdownEditor::linkTooltip(const QString &url, int maximumWidth) {
    const QString elided =
        QFontMetrics(QToolTip::font()).elidedText(url, Qt::ElideMiddle, maximumWidth);
    // A tooltip takes text that looks like HTML for HTML, and a destination is
    // the writer's text, so it goes in escaped.
    return Qt::convertFromPlainText(elided, Qt::WhiteSpaceNormal);
}

void MarkdownEditor::mouseMoveEvent(QMouseEvent *event) {
    QTextEdit::mouseMoveEvent(event);
    const QString url = linkAt(event->position().toPoint());
    if (url == m_hoveredLink)
        return;
    m_hoveredLink = url;
    if (url.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(event->globalPosition().toPoint() + QPoint(12, 20),
                           linkTooltip(url, window()->width() - 32), viewport());
}

void MarkdownEditor::forgetHoveredLink() {
    if (m_hoveredLink.isEmpty())
        return;
    m_hoveredLink.clear();
    QToolTip::hideText();
}

// Cmd+scroll changes the text size, as in a browser.
void MarkdownEditor::wheelEvent(QWheelEvent *event) {
    if (!(event->modifiers() & Qt::ControlModifier)) {
        QTextEdit::wheelEvent(event);
        return;
    }
    // Finger scrolling synthesizes an angle too, so its pixels come first.
    const QPoint pixels = event->pixelDelta();
    zoomByWheel(pixels.y() != 0 ? pixels.y() / 50.0 : event->angleDelta().y() / 120.0);
    event->accept();
}

// A wheel notch is a whole step, but a touchpad trickles in pixels, so bank
// the fractions and step once a whole one is scrolled.
void MarkdownEditor::zoomByWheel(qreal notches) {
    if (m_zoomNotches * notches < 0)
        m_zoomNotches = 0;
    m_zoomNotches += notches;
    while (m_zoomNotches >= 1) {
        m_backend->increaseTextSize();
        m_zoomNotches -= 1;
    }
    while (m_zoomNotches <= -1) {
        m_backend->decreaseTextSize();
        m_zoomNotches += 1;
    }
}

void MarkdownEditor::setSearchHighlights(const QList<SearchHighlight> &highlights) {
    m_searchHighlights = highlights;
    applySearchHighlights();
}

void MarkdownEditor::applySearchHighlights() {
    const bool dark = m_backend->darkMode();
    const QColor match(dark ? QStringLiteral("#725b18") : QStringLiteral("#ffe58a"));
    const QColor current(dark ? QStringLiteral("#b36b20") : QStringLiteral("#ffad42"));
    QList<ExtraSelection> selections;
    selections.reserve(m_searchHighlights.size());
    for (const SearchHighlight &highlight : std::as_const(m_searchHighlights)) {
        ExtraSelection selection;
        selection.cursor = QTextCursor(document());
        selection.cursor.setPosition(highlight.start);
        selection.cursor.setPosition(highlight.end, QTextCursor::KeepAnchor);
        selection.format.setBackground(highlight.current ? current : match);
        selections.append(selection);
    }
    setExtraSelections(selections);
}
