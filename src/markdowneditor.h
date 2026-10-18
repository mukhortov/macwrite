#pragma once

#include <QTextEdit>

#include <utility>

#include "findbar.h"

class Backend;
class PlainTextLayout;

// The writing area: plain text in a centred column, laid out by
// PlainTextLayout so an edit costs the lines it touched, not the document.
class MarkdownEditor : public QTextEdit {
    Q_OBJECT

public:
    explicit MarkdownEditor(Backend *backend, QWidget *parent = nullptr);

    // About 65 characters, narrowing with the window but never past its edges.
    static int columnWidth(qreal averageCharWidth, int windowWidth);
    int fontPixelSize() const;
    // Keeps two lines of room between the caret and the edges of the view,
    // so writing past the bottom scrolls the page along with the text.
    void ensureCaretVisible();
    // The first and last positions in view.
    std::pair<int, int> visiblePositions() const;

    // Bold, italic or strikethrough on or off at the caret or selection.
    void toggleWrap(const QString &marker);
    // Links the selection, or a placeholder, to the URL on the clipboard.
    void insertLink();

    // The link under a point of the viewport, or nothing.
    QString linkAt(const QPoint &point) const;
    QString hoveredLink() const { return m_hoveredLink; }
    // A link's destination as a tooltip shows it: as itself, cut in the
    // middle to fit.
    static QString linkTooltip(const QString &url, int maximumWidth);
    void setSearchHighlights(const QList<SearchHighlight> &highlights);

signals:
    // The view scrolled, resized or reflowed, so other positions are in it.
    void viewChanged();
    // The writer changed the text, not a pass of formatting or a load.
    void edited();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;

private:
    void applyFont();
    void applyTheme();
    void placeColumn();
    void moveCaretVisibly(int direction);
    void movePage(int direction, bool extendSelection);
    int skipHidden(int position, int direction) const;
    void zoomByWheel(qreal notches);
    void forgetHoveredLink();
    void applySearchHighlights();
    QColor mutedColour() const;

    Backend *m_backend;
    PlainTextLayout *m_layout;
    QString m_hoveredLink;
    QList<SearchHighlight> m_searchHighlights;
    // Touchpad scrolling toward a text size step, banked until it adds up to one.
    qreal m_zoomNotches = 0;
};
