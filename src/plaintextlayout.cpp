#include "plaintextlayout.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>
#include <QtMath>

#include <algorithm>
#include <climits>

namespace {

// Qt lays out in 26.6 fixed point (QFixed), and converts to and from it this
// way. Doing the same keeps every sum exactly what Qt's layout would get.
int fixed(qreal value) {
    return int(value * qreal(64));
}

qreal real(qint64 value) {
    return qreal(value) / qreal(64);
}

int rounded(int value) {
    return (value + 32) & -64;
}

// QGuiApplicationPrivate::visualAlignment, which QTextDocumentLayout uses.
Qt::Alignment visualAlignment(Qt::LayoutDirection direction, Qt::Alignment alignment) {
    if (!(alignment & Qt::AlignHorizontal_Mask))
        alignment |= Qt::AlignLeft;
    if (!(alignment & Qt::AlignAbsolute) && (alignment & (Qt::AlignLeft | Qt::AlignRight))) {
        if (direction == Qt::RightToLeft)
            alignment ^= (Qt::AlignLeft | Qt::AlignRight);
        alignment |= Qt::AlignAbsolute;
    }
    return alignment;
}

}

PlainTextLayout::PlainTextLayout(QTextDocument *document)
    : QAbstractTextDocumentLayout(document) {}

bool PlainTextLayout::canLayout() const {
    return document()->isLayoutEnabled() && !document()->pageSize().isNull();
}

PlainTextLayout::Frame PlainTextLayout::currentFrame() const {
    const QTextFrameFormat format = document()->rootFrame()->frameFormat();
    const int border = rounded(fixed(format.border()));
    const int padding = rounded(fixed(format.padding()));
    const int leftMargin = rounded(fixed(format.leftMargin()));
    const int rightMargin = rounded(fixed(format.rightMargin()));

    Frame frame;
    frame.top = rounded(fixed(format.topMargin())) + border + padding;
    frame.bottom = rounded(fixed(format.bottomMargin())) + border + padding;
    frame.left = leftMargin + border + padding;
    frame.leftMargin = leftMargin;
    frame.rightMargin = rightMargin;
    frame.marginWidth = 2 * (border + padding) + leftMargin + rightMargin;
    const qreal pageWidth = qMax(qreal(0), document()->pageSize().width());
    frame.contentsWidth = fixed(format.width().value(pageWidth)) - frame.marginWidth;
    return frame;
}

// QTextDocumentLayoutPrivate::layoutBlock, without floats, pages, or a paint
// device. The block's lines are placed from its own top, so the block can sit
// anywhere without being laid out again.
std::pair<int, int> PlainTextLayout::layoutBlock(const QTextBlock &block) const {
    if (!block.isVisible())
        return {0, 0};

    const QTextDocument *doc = document();
    QTextLayout *layout = block.layout();
    const QTextBlockFormat format = block.blockFormat();
    const QTextOption &defaults = doc->defaultTextOption();
    const Qt::LayoutDirection direction = block.textDirection();

    int extraMargin = 0;
    if (defaults.flags() & QTextOption::AddSpaceForLineAndParagraphSeparators)
        extraMargin = fixed(QFontMetricsF(block.charFormat().font()).horizontalAdvance(u'\x21B5'));
    const qreal indentLevel = format.indent();
    const int indent = qIsNull(indentLevel) ? 0 : fixed(indentLevel * doc->indentWidth());
    const int leftMargin = fixed(format.leftMargin())
            + (direction == Qt::RightToLeft ? extraMargin : indent);
    const int rightMargin = fixed(format.rightMargin())
            + (direction == Qt::RightToLeft ? indent : extraMargin);

    QTextOption option = defaults;
    option.setTextDirection(direction);
    option.setTabs(format.tabPositions());
    Qt::Alignment alignment = defaults.alignment();
    if (format.hasProperty(QTextFormat::BlockAlignment))
        alignment = format.alignment();
    option.setAlignment(visualAlignment(direction, alignment));
    if (format.nonBreakableLines() || doc->pageSize().width() < 0)
        option.setWrapMode(QTextOption::ManualWrap);
    layout->setTextOption(option);
    const bool wordOrAnywhere = option.wrapMode() == QTextOption::WrapAtWordBoundaryOrAnywhere;

    const int lineLeft = m_frame.left + leftMargin;
    const int lineRight = m_frame.left + m_frame.contentsWidth - rightMargin;
    const int lineHeightType = format.lineHeightType();
    // Wrapped lines start where the block's opening characters end, measured
    // on its first line, so they line up whatever those characters are.
    const int hangingPrefix =
        m_hangingIndentRule && direction == Qt::LeftToRight ? m_hangingIndentRule(block) : 0;
    int hanging = 0;
    int y = 0;
    int bottom = 0;
    int widest = 0;
    bool firstLine = true;

    layout->beginLayout();
    while (true) {
        QTextLine line = layout->createLine();
        if (!line.isValid())
            break;
        line.setLeadingIncluded(true);

        int left = lineLeft;
        int right = lineRight;
        const bool first = firstLine;
        if (firstLine) {
            const int textIndent = fixed(format.textIndent());
            if (direction == Qt::LeftToRight)
                left += textIndent;
            else
                right -= textIndent;
            firstLine = false;
        } else {
            left += hanging;
        }

        line.setLineWidth(real(right - left));
        if (fixed(line.naturalTextWidth()) > right - left) {
            line.setLineWidth(real(right - left));
            if (fixed(line.naturalTextWidth()) > right - left) {
                if (wordOrAnywhere) {
                    option.setWrapMode(QTextOption::WrapAnywhere);
                    layout->setTextOption(option);
                }
                line.setLineWidth(qMax<qreal>(line.naturalTextWidth(), real(right - left)));
                if (wordOrAnywhere) {
                    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
                    layout->setTextOption(option);
                }
            }
        }

        // Never so far that the wrapped lines have less than half the column.
        if (first && hangingPrefix > 0)
            hanging = qBound(0, fixed(line.cursorToX(hangingPrefix)), (lineRight - lineLeft) / 2);

        // getLineHeightParams in qtextdocumentlayout.cpp.
        const qreal height = line.height();
        const qreal rawHeight = qCeil(line.ascent() + line.descent() + line.leading());
        const int lineHeight = fixed(format.lineHeight(rawHeight, 1));
        const int lineBottom = fixed(format.lineHeight(height, 1));
        int adjustment = 0;
        if (lineHeightType == QTextBlockFormat::FixedHeight)
            adjustment = fixed(line.ascent() + qMax(line.leading(), qreal(0))) - (lineHeight * 4) / 5;
        else if (lineHeightType == QTextBlockFormat::MinimumHeight)
            adjustment = fixed(height) - lineHeight;

        line.setPosition(QPointF(real(left - m_frame.left), real(y - adjustment)));
        bottom = y + lineBottom;
        y += lineHeight;
        widest = qMax(widest, fixed(line.x() + line.naturalTextWidth()) + rightMargin);
    }
    layout->endLayout();

    return {qMax(y, bottom), widest};
}

// Space between two blocks goes to the larger of their margins. The first
// block's top margin is not kept, as in Qt.
int PlainTextLayout::marginAbove(const QTextBlock &block) const {
    const QTextBlock previous = block.previous();
    if (!previous.isValid() || !block.isVisible())
        return 0;
    return fixed(qMax(block.blockFormat().topMargin(), previous.blockFormat().bottomMargin()));
}

void PlainTextLayout::layoutAll() {
    const QTextDocument *doc = document();
    const int count = doc->blockCount();
    m_heights.resize(count);
    m_widths.resize(count);
    m_advances.resize(count);
    m_widest = 0;

    qint64 y = originY();
    int index = 0;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next(), ++index) {
        const auto [height, width] = layoutBlock(block);
        const int margin = marginAbove(block);
        block.layout()->setPosition(QPointF(real(originX()), real(y + margin)));
        m_heights[index] = height;
        m_widths[index] = width;
        m_advances[index] = block.isVisible() ? margin + height : 0;
        m_widest = qMax(m_widest, width);
        y += m_advances.at(index);
    }
    rebuildTree();
    m_laidOut = true;
    m_lastLayoutBlockCount = count;
}

// A Fenwick tree over each block's advance: the space from the bottom of the
// block above to its own bottom.
void PlainTextLayout::rebuildTree() {
    const qsizetype count = m_advances.size();
    m_tree.resize(count + 1);
    m_tree[0] = 0;
    for (qsizetype i = 1; i <= count; ++i)
        m_tree[i] = m_advances.at(i - 1);
    for (qsizetype i = 1; i <= count; ++i) {
        const qsizetype parent = i + (i & -i);
        if (parent <= count)
            m_tree[parent] += m_tree.at(i);
    }
}

void PlainTextLayout::setBlockAdvance(int index, int advance) {
    const qint64 change = qint64(advance) - m_advances.at(index);
    if (!change)
        return;
    m_advances[index] = advance;
    for (qsizetype i = index + 1; i < m_tree.size(); i += i & -i)
        m_tree[i] += change;
}

qint64 PlainTextLayout::heightAbove(int index) const {
    qint64 sum = 0;
    for (qsizetype i = index; i > 0; i -= i & -i)
        sum += m_tree.at(i);
    return sum;
}

// The last block that starts at or above y, counted from the frame's top.
int PlainTextLayout::blockIndexAt(qint64 y) const {
    const qsizetype count = m_advances.size();
    qsizetype step = 1;
    while (step * 2 <= count)
        step *= 2;
    qsizetype index = 0;
    for (; step > 0; step /= 2) {
        if (index + step <= count && m_tree.at(index + step) <= y) {
            index += step;
            y -= m_tree.at(index);
        }
    }
    return int(qMin(index, count - 1));
}

int PlainTextLayout::originX() const {
    return m_padding.left + m_frame.left;
}

qint64 PlainTextLayout::originY() const {
    return qint64(m_padding.top) + m_frame.top;
}

QPointF PlainTextLayout::blockPosition(const QTextBlock &block) const {
    const qint64 y = originY() + heightAbove(block.blockNumber()) + marginAbove(block);
    return QPointF(real(originX()), real(y));
}

void PlainTextLayout::documentChanged(int from, int charsRemoved, int charsAdded) {
    Q_UNUSED(charsRemoved);
    if (!canLayout()) {
        m_laidOut = false;
        return;
    }

    QTextDocument *doc = document();
    const int blockCount = doc->blockCount();
    const Frame frame = currentFrame();
    qreal changedTop = 0;

    // The blocks from the one the change starts in to the one holding the
    // first character after it are new; before and after them, the blocks
    // are the ones already laid out.
    const int lastPosition = doc->characterCount() - 1;
    const QTextBlock first = doc->findBlock(qBound(0, from, lastPosition));
    const QTextBlock last = doc->findBlock(qBound(0, from + charsAdded, lastPosition));
    const int firstIndex = first.blockNumber();
    const int newSpan = last.blockNumber() - firstIndex + 1;
    const int oldSpan = newSpan - (blockCount - int(m_advances.size()));

    if (!m_laidOut || frame != m_frame || newSpan == blockCount || oldSpan < 1
            || firstIndex + oldSpan > m_advances.size()) {
        m_frame = frame;
        layoutAll();
    } else {
        bool lostWidest = false;
        for (int i = firstIndex; i < firstIndex + oldSpan; ++i)
            lostWidest = lostWidest || m_widths.at(i) >= m_widest;

        if (newSpan != oldSpan) {
            m_heights.remove(firstIndex, oldSpan);
            m_widths.remove(firstIndex, oldSpan);
            m_advances.remove(firstIndex, oldSpan);
            m_heights.insert(firstIndex, newSpan, 0);
            m_widths.insert(firstIndex, newSpan, 0);
            m_advances.insert(firstIndex, newSpan, 0);
        }

        int index = firstIndex;
        int widestNew = 0;
        for (QTextBlock block = first;; block = block.next(), ++index) {
            const auto [height, width] = layoutBlock(block);
            m_heights[index] = height;
            m_widths[index] = width;
            widestNew = qMax(widestNew, width);
            const int advance = block.isVisible() ? marginAbove(block) + height : 0;
            if (newSpan == oldSpan)
                setBlockAdvance(index, advance);
            else
                m_advances[index] = advance;
            if (block == last)
                break;
        }

        // The margin above the next block counts the last one's bottom margin.
        const QTextBlock next = last.next();
        if (next.isValid()) {
            const int advance = next.isVisible() ? marginAbove(next) + m_heights.at(index + 1) : 0;
            if (newSpan == oldSpan)
                setBlockAdvance(index + 1, advance);
            else
                m_advances[index + 1] = advance;
        }
        if (newSpan != oldSpan)
            rebuildTree();

        if (lostWidest)
            m_widest = *std::max_element(m_widths.cbegin(), m_widths.cend());
        else
            m_widest = qMax(m_widest, widestNew);

        for (QTextBlock block = first;; block = block.next()) {
            block.layout()->setPosition(blockPosition(block));
            if (block == last)
                break;
        }
        changedTop = first.layout()->position().y();
        m_lastLayoutBlockCount = newSpan;
    }

    updateSize();
    emit update(QRectF(0, changedTop, qreal(INT_MAX), qreal(INT_MAX)));
}

void PlainTextLayout::updateSize() {
    const qint64 contents = heightAbove(int(m_advances.size()));
    const int width = qMax(m_frame.contentsWidth, qMax(0, m_widest));
    const QSizeF size(real(qint64(m_padding.left) + width + m_frame.marginWidth + m_padding.right),
                      real(originY() + contents + m_frame.bottom + m_padding.bottom));
    if (size != m_size) {
        m_size = size;
        emit documentSizeChanged(size);
    }
}

QSizeF PlainTextLayout::documentSize() const {
    return m_size;
}

int PlainTextLayout::pageCount() const {
    const qreal pageHeight = document()->pageSize().height();
    if (pageHeight <= 0)
        return 1;
    return qCeil(m_size.height() / pageHeight);
}

QRectF PlainTextLayout::frameBoundingRect(QTextFrame *frame) const {
    if (!canLayout() || frame != document()->rootFrame())
        return {};
    return QRectF(QPointF(0, 0), m_size);
}

QRectF PlainTextLayout::blockBoundingRect(const QTextBlock &block) const {
    if (!m_laidOut || !canLayout() || !block.isValid() || !block.isVisible()
            || block.blockNumber() >= m_advances.size())
        return {};

    // Blocks below an edit keep their old place until someone asks for it.
    QTextLayout *layout = block.layout();
    const QPointF position = blockPosition(block);
    if (layout->position() != position)
        layout->setPosition(position);
    QRectF rect = layout->boundingRect();
    rect.moveTopLeft(position);
    return rect;
}

// QTextDocumentLayoutPrivate::hitTest for a block.
PlainTextLayout::Hit PlainTextLayout::hitTestBlock(const QTextBlock &block, const QPointF &point,
                                                   int *position, QTextLayout **hitLayout,
                                                   Qt::HitTestAccuracy accuracy) const {
    QTextLayout *layout = block.layout();
    const QPointF origin = blockPosition(block);
    const QRectF rect = layout->boundingRect().translated(origin);
    *position = block.position();
    if (point.y() < rect.top() - block.blockFormat().topMargin())
        return Hit::Before;
    if (point.y() > rect.bottom()) {
        *position += block.length();
        return Hit::After;
    }

    const QPointF inBlock = point - origin;
    Hit hit = Hit::Inside;
    *hitLayout = layout;
    int offset = 0;
    for (int i = 0; i < layout->lineCount(); ++i) {
        const QTextLine line = layout->lineAt(i);
        const QRectF lineRect = line.naturalTextRect();
        if (lineRect.top() > inBlock.y()) {
            offset = qMin(offset, line.textStart());
        } else if (lineRect.bottom() <= inBlock.y()) {
            offset = qMax(offset, line.textStart() + line.textLength());
        } else {
            if (lineRect.left() <= inBlock.x() && lineRect.right() >= inBlock.x())
                hit = Hit::Exact;
            offset = line.xToCursor(inBlock.x(), accuracy == Qt::ExactHit
                                                         ? QTextLine::CursorOnCharacter
                                                         : QTextLine::CursorBetweenCharacters);
            break;
        }
    }
    *position += offset;
    return hit;
}

// Qt tries every block from the top, keeping the first the point is inside,
// or else the end of the last block above the point. Only the blocks around
// the point can be either, so the search starts there.
int PlainTextLayout::hitTest(const QPointF &point, Qt::HitTestAccuracy accuracy) const {
    if (!m_laidOut || !canLayout() || m_advances.isEmpty())
        return accuracy == Qt::ExactHit ? -1 : 0;

    const QPointF at(real(fixed(point.x())), real(fixed(point.y())));
    QTextBlock block = document()->findBlockByNumber(blockIndexAt(fixed(at.y()) - originY()));
    // A line taller than its share of the page reaches into the block below.
    for (QTextBlock previous = block.previous(); previous.isValid(); previous = previous.previous()) {
        if (at.y() > blockBoundingRect(previous).bottom())
            break;
        block = previous;
    }

    int position = block.position();
    Hit hit = Hit::Before;
    QTextLayout *hitLayout = nullptr;
    for (; block.isValid(); block = block.next()) {
        int blockPosition = 0;
        const Hit blockHit = hitTestBlock(block, at, &blockPosition, &hitLayout, accuracy);
        if (blockHit >= Hit::Inside) {
            hit = blockHit;
            position = blockPosition;
            break;
        }
        if (blockHit == Hit::Before) {
            if (blockPosition < position) {
                position = blockPosition;
                hit = blockHit;
            }
            // Every block from here on starts further down.
            break;
        }
        if (blockPosition > position) {
            position = blockPosition;
            hit = blockHit;
        }
    }

    if (accuracy == Qt::ExactHit && hit < Hit::Exact)
        return -1;

    int lastPosition = document()->rootFrame()->lastPosition();
    if (hitLayout && !hitLayout->preeditAreaText().isEmpty())
        lastPosition += hitLayout->preeditAreaText().size();
    if (position > lastPosition)
        return lastPosition;
    return qMax(0, position);
}

// QTextDocumentLayoutPrivate::drawFlow and drawBlock for one frame of plain
// blocks. QTextEdit leaves painting the text, its selections and the caret to
// the layout.
void PlainTextLayout::draw(QPainter *painter, const PaintContext &context) {
    if (!m_laidOut || !canLayout() || m_advances.isEmpty())
        return;

    // Qt keeps the text within the root frame less its side margins.
    const QRectF frameClip = QRectF(QPointF(0, 0), m_size)
        .adjusted(real(m_padding.left + m_frame.leftMargin), 0,
                  -real(m_padding.right + m_frame.rightMargin), 0);
    const QRectF &clip = context.clip;
    const QRectF textClip = clip.isValid() ? (clip & frameClip) : frameClip;
    // A line taller than its share of the page reaches into the block below.
    QTextBlock block = clip.isValid()
        ? document()->findBlockByNumber(qMax(0, blockIndexAt(fixed(clip.top()) - originY()) - 1))
        : document()->begin();
    for (; block.isValid(); block = block.next()) {
        if (!block.isVisible())
            continue;
        QTextLayout *layout = block.layout();
        const QPointF position = blockPosition(block);
        if (layout->position() != position)
            layout->setPosition(position);
        const QRectF rect = layout->boundingRect().translated(position);
        if (clip.isValid() && rect.top() > clip.bottom())
            break;
        if (clip.isValid() && rect.bottom() < clip.y())
            continue;

        const QTextBlockFormat format = block.blockFormat();
        const QBrush background = format.background();
        if (background != Qt::NoBrush) {
            QRectF backgroundRect = rect;
            if (document()->pageSize().width() <= 0)
                backgroundRect.setRight(m_size.width() - real(m_padding.right + m_frame.rightMargin));
            painter->fillRect(backgroundRect, background);
        }

        const int blockStart = block.position();
        const int blockLength = block.length();
        QList<QTextLayout::FormatRange> selections;
        for (const Selection &range : context.selections) {
            const int start = range.cursor.selectionStart() - blockStart;
            const int end = range.cursor.selectionEnd() - blockStart;
            if (start < blockLength && end > 0 && end > start) {
                selections.append({start, end - start, range.format});
            } else if (!range.cursor.hasSelection()
                       && range.format.hasProperty(QTextFormat::FullWidthSelection)
                       && block.contains(range.cursor.position())) {
                // A full-width selection takes the cursor's whole line.
                const QTextLine line = layout->lineForTextPosition(range.cursor.position() - blockStart);
                QTextLayout::FormatRange whole{line.textStart(), line.textLength(), range.format};
                if (whole.start + whole.length == blockLength - 1)
                    ++whole.length;
                selections.append(whole);
            }
        }

        const QPen pen = painter->pen();
        painter->setPen(context.palette.color(QPalette::Text));
        layout->draw(painter, QPointF(), selections, textClip);
        // A position below -1 counts back from the end of the preedit text.
        if ((context.cursorPosition >= blockStart && context.cursorPosition < blockStart + blockLength)
                || (context.cursorPosition < -1 && !layout->preeditAreaText().isEmpty())) {
            const int caret = context.cursorPosition < -1
                ? layout->preeditAreaPosition() - (context.cursorPosition + 2)
                : context.cursorPosition - blockStart;
            layout->drawCursor(painter, QPointF(), caret, m_cursorWidth);
        }
        painter->setPen(pen);
    }
}

void PlainTextLayout::setPadding(const QMarginsF &padding) {
    const Padding fixedPadding{fixed(padding.left()), fixed(padding.top()),
                               fixed(padding.right()), fixed(padding.bottom())};
    if (fixedPadding.left == m_padding.left && fixedPadding.top == m_padding.top
            && fixedPadding.right == m_padding.right && fixedPadding.bottom == m_padding.bottom)
        return;
    m_padding = fixedPadding;
    if (!m_laidOut || !canLayout())
        return;

    // Blocks find their place from the padding when asked, so nothing is
    // laid out again.
    m_lastLayoutBlockCount = 0;
    updateSize();
    emit update(QRectF(0, 0, qreal(INT_MAX), qreal(INT_MAX)));
}

QMarginsF PlainTextLayout::padding() const {
    return QMarginsF(real(m_padding.left), real(m_padding.top),
                     real(m_padding.right), real(m_padding.bottom));
}

void PlainTextLayout::setHangingIndentRule(HangingIndentRule rule) {
    m_hangingIndentRule = std::move(rule);
    if (m_laidOut && canLayout()) {
        layoutAll();
        updateSize();
        emit update(QRectF(0, 0, qreal(INT_MAX), qreal(INT_MAX)));
    }
}
