#include "markdowneditor.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>

#include "backend.h"
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
    document()->setDocumentLayout(m_layout);
    setCursorWidth(caretWidth);
    document()->clearUndoRedoStacks();
    m_backend->attachDocument(document());

    connect(m_backend, &Backend::editorFontChanged, this, &MarkdownEditor::applyFont);
    connect(m_backend, &Backend::textScaleChanged, this, &MarkdownEditor::applyFont);
    connect(m_backend, &Backend::themeColorsChanged, this, &MarkdownEditor::applyTheme);
    connect(m_backend, &Backend::darkModeChanged, this, &MarkdownEditor::applyTheme);
    connect(this, &QTextEdit::cursorPositionChanged, this, &MarkdownEditor::ensureCaretVisible);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &MarkdownEditor::viewChanged);
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
    QFont writing(m_backend->editorFont());
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
