#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);

    void setTheme(bool darkMode, const QString &background, const QString &foreground,
                  const QString &accent, const QString &codeBackground);
    // Code takes the family given, or the text's own when there is none.
    void refreshFont(const QString &codeFamily);

    struct Span {
        int start;
        int length;
    };

    enum class InlineKind { Bold, Italic, Strikethrough, Link };

    // Carried from block to block so a fenced run of code knows it is inside
    // one. Stored on the block, which is where Backend::hiddenRangesAt reads it.
    // Inside a fence the state is InsideFence plus twice the fence characters
    // past three that opened it, plus one for tildes, since only a fence of the
    // same character at least that long closes it.
    enum BlockState { Prose = 0, InsideFence = 1 };

    struct InlineMarkup {
        InlineKind kind;
        Span content;
        Span markers[2];
        Span destination{};
    };

    // Single source of truth for inline markdown spans: the highlighter uses it
    // to style content and hide markers, and the editor uses it (via
    // Backend::hiddenRangesAt) to skip the caret over the hidden markers.
    static QList<InlineMarkup> inlineMarkup(const QString &text);
    // How many characters open the block before its text: a heading's
    // hashes, a quote's ">", a list's marker and task box, and the spaces
    // around them. Its wrapped lines line up after them. None in fenced code.
    static int hangingIndentLength(const QTextBlock &block);
    // Whether the block is fenced code, its fence lines included.
    static bool isFencedCode(const QTextBlock &block);
    static QString linkUrlAt(const QString &text, int position);

    // Inline code spans, backticks included.
    static QList<Span> codeSpans(const QString &text);
    static int openingFenceLength(const QString &text);
    static bool isThematicBreak(const QString &text);
    static bool closesFence(const QString &text, int openingLength,
                            QChar marker = QLatin1Char('`'));

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildFormats();
    void highlightMarkers(const QString &text);
    void highlightInline(const QString &text);

    bool m_darkMode = true;
    QString m_customBackground;
    QString m_customForeground;
    QString m_customAccent;
    QString m_customCodeBackground;
    QString m_codeFamily;
    QTextCharFormat m_markerFormat;
    QTextCharFormat m_hiddenMarkerFormat;
    QTextCharFormat m_headingFormat;
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_codeFormat;
    // Fenced lines draw on the block's own background, so they have none.
    QTextCharFormat m_fencedCodeFormat;
    QTextCharFormat m_fenceFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_linkFormat;
};
