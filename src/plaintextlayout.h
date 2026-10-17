#pragma once

#include <QAbstractTextDocumentLayout>
#include <QList>
#include <QPointF>
#include <QSizeF>

#include <utility>

class QTextLayout;

// Lays out a plain text document, one frame of blocks, line for line as
// QTextDocumentLayout does. Qt's layout walks every block after an edit to move
// it down, so typing near the top of a long document costs time in proportion
// to the whole document. This one keeps each block's height in a Fenwick tree:
// an edit lays out only the blocks it touched, and finding the block at a
// height, or a block's height on the page, is a search rather than a walk.
//
// Heights are summed in Qt's 26.6 fixed point, so every block lands on exactly
// the y QTextDocumentLayout would give it. Lines are found and broken by the
// same QTextLayout calls with the same widths and options.
//
// Assumes what Write's documents are: no tables, child frames, or inline
// objects, line heights that never pull a line above its block, and no paint
// device (Qt Quick sets none).
class PlainTextLayout : public QAbstractTextDocumentLayout {
    Q_OBJECT

public:
    explicit PlainTextLayout(QTextDocument *document);

    void draw(QPainter *painter, const PaintContext &context) override;
    int hitTest(const QPointF &point, Qt::HitTestAccuracy accuracy) const override;
    int pageCount() const override;
    QSizeF documentSize() const override;
    QRectF frameBoundingRect(QTextFrame *frame) const override;
    QRectF blockBoundingRect(const QTextBlock &block) const override;

    // How many blocks the last change laid out, for tests and benchmarks.
    int lastLayoutBlockCount() const { return m_lastLayoutBlockCount; }

protected:
    void documentChanged(int from, int charsRemoved, int charsAdded) override;

private:
    // Where the root frame puts its blocks, in 26.6 fixed point.
    struct Frame {
        int top = 0;
        int bottom = 0;
        int left = 0;
        int contentsWidth = 0;
        int marginWidth = 0;
        bool operator!=(const Frame &other) const {
            return top != other.top || bottom != other.bottom || left != other.left
                || contentsWidth != other.contentsWidth || marginWidth != other.marginWidth;
        }
    };

    enum class Hit { Before, After, Inside, Exact };

    bool canLayout() const;
    Frame currentFrame() const;
    void layoutAll();
    // Lays out the block, returning its height and widest line.
    std::pair<int, int> layoutBlock(const QTextBlock &block) const;
    int marginAbove(const QTextBlock &block) const;
    void setBlockAdvance(int index, int advance);
    void rebuildTree();
    qint64 heightAbove(int index) const;
    int blockIndexAt(qint64 y) const;
    QPointF blockPosition(const QTextBlock &block) const;
    Hit hitTestBlock(const QTextBlock &block, const QPointF &point, int *position,
                     QTextLayout **layout, Qt::HitTestAccuracy accuracy) const;
    void updateSize();

    Frame m_frame;
    bool m_laidOut = false;
    // Each block's own height and widest line, and in the tree its height
    // plus the margin above it.
    QList<int> m_heights;
    QList<int> m_widths;
    QList<int> m_advances;
    QList<qint64> m_tree;
    mutable int m_widest = 0;
    mutable bool m_widestStale = false;
    QSizeF m_size;
    int m_lastLayoutBlockCount = 0;
};
