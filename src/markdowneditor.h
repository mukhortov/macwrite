#pragma once

#include <QTextEdit>

#include <utility>

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

signals:
    // The view scrolled, resized or reflowed, so other positions are in it.
    void viewChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void applyFont();
    void applyTheme();
    void placeColumn();
    QColor mutedColour() const;

    Backend *m_backend;
    PlainTextLayout *m_layout;
};
