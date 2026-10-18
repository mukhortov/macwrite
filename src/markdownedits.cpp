#include "markdownedits.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

namespace {

// The text between two positions, with the document's paragraph and line
// separators read as the \n they were typed as.
QString textBetween(QTextDocument *document, int from, int to) {
    QTextCursor cursor(document);
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return text;
}

int lineStart(QTextDocument *document, int position) {
    return document->findBlock(position).position();
}

int lineEnd(QTextDocument *document, int position) {
    const QTextBlock block = document->findBlock(position);
    return block.position() + block.length() - 1;
}

bool isBlank(const QString &text) {
    static const QRegularExpression blank(QStringLiteral("^\\s*$"));
    return blank.match(text).hasMatch();
}

void replaceSelection(QTextCursor &cursor, const QString &text) {
    MarkdownEdits::replaceRange(cursor, cursor.selectionStart(), cursor.selectionEnd(), text);
}

bool hasText(const QString &line) {
    return std::any_of(line.cbegin(), line.cend(), [](QChar c) { return !c.isSpace(); });
}

// "* * *" and "- - -" are rules, not items.
bool isListItem(const QString &line) {
    static const QRegularExpression item(QStringLiteral("^[ \\t]*(?:[-+*]|\\d+[.)])[ \\t]"));
    static const QRegularExpression rule(QStringLiteral("^ {0,3}([-*])([ \\t]*\\1){2,}[ \\t]*$"));
    return item.match(line).hasMatch() && !rule.match(line).hasMatch();
}

// The spaces and tabs a line starts with.
int indentLength(const QString &line) {
    int length = 0;
    while (length < line.size() && (line.at(length) == QLatin1Char(' ') || line.at(length) == QLatin1Char('\t')))
        ++length;
    return length;
}

// How many columns a line's indent spans, a tab running to the next
// four-column stop.
int indentColumns(const QString &line) {
    const int length = indentLength(line);
    int column = 0;
    for (int i = 0; i < length; ++i)
        column = line.at(i) == QLatin1Char('\t') ? column + 4 - column % 4 : column + 1;
    return column;
}

}

namespace MarkdownEdits {

QString normalizePlainText(const QString &text) {
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return normalized;
}

QString escapeLinkText(const QString &text) {
    QString escaped = text;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('['), QStringLiteral("\\["));
    escaped.replace(QLatin1Char(']'), QStringLiteral("\\]"));
    return escaped;
}

QString escapeLinkDestination(const QString &url) {
    QString escaped = url;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('('), QStringLiteral("\\("));
    escaped.replace(QLatin1Char(')'), QStringLiteral("\\)"));
    return escaped;
}

QString markdownLink(const QString &label, const QString &url) {
    return QLatin1Char('[') + escapeLinkText(label) + QStringLiteral("](")
        + escapeLinkDestination(url) + QLatin1Char(')');
}

QString replaceRange(QTextCursor &cursor, int start, int end, const QString &replacement,
                     std::optional<std::pair<int, int>> select) {
    QTextDocument *document = cursor.document();
    const int length = document->characterCount() - 1;
    start = qBound(0, start, length);
    end = qBound(start, end, length);
    const QString inserted = normalizePlainText(replacement);

    QTextCursor edit(document);
    edit.beginEditBlock();
    edit.setPosition(start);
    edit.setPosition(end, QTextCursor::KeepAnchor);
    edit.insertText(inserted);
    edit.endEditBlock();

    const int insertedEnd = start + int(inserted.size());
    if (select) {
        const int from = qBound(start, start + select->first, insertedEnd);
        const int to = qBound(start, start + select->second, insertedEnd);
        cursor.setPosition(from);
        cursor.setPosition(to, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(insertedEnd);
    }
    return inserted;
}

// A marker only counts as a whole run, so the "*" of "**bold**" is not taken
// for italic; "***" counts as both.
void toggleWrap(QTextCursor &cursor, const QString &marker, const std::optional<EmphasisSpan> &span) {
    QTextDocument *document = cursor.document();
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();

    // At the end of the span's text the caret steps past the closing marker,
    // so typing carries on plain, and steps back in on a second press;
    // anywhere else in it the span comes off.
    if (start == end && span) {
        if (start == span->end) {
            cursor.setPosition(span->textEnd);
        } else if (start >= span->textEnd) {
            cursor.setPosition(span->end);
        } else {
            const QString content = textBetween(document, span->contentStart, span->contentEnd);
            const int caret = qMax(0, start - span->contentStart);
            replaceRange(cursor, span->start, span->end, content, std::pair{caret, caret});
        }
        return;
    }

    const int length = document->characterCount() - 1;
    const QChar mark = marker.at(0);
    const int size = int(marker.size());
    const auto charAt = [document, length](int position) {
        return position < 0 || position >= length ? QChar() : document->characterAt(position);
    };
    const auto runFrom = [&](int position, int step, int limit) {
        int run = 0;
        while (position + run * step != limit && charAt(position + run * step) == mark)
            ++run;
        return run;
    };
    const auto holds = [&](int run) {
        return run == size || (mark == QLatin1Char('*') && run == 3);
    };
    const auto hasMarker = [&](const QString &part) {
        for (int i = 0; i < part.size();) {
            int run = 0;
            while (i + run < part.size() && part.at(i + run) == mark)
                ++run;
            if (run > 0 && holds(run))
                return true;
            i += qMax(1, run);
        }
        return false;
    };
    const QString selected = textBetween(document, start, end);
    const QString inner = selected.size() >= 2 * size
        ? selected.mid(size, selected.size() - 2 * size) : QString();

    // A marker in the middle means separate spans, as in "**a** and **b**":
    // unwrapping would break them, so wrap instead.
    if (holds(runFrom(start - 1, -1, -1)) && holds(runFrom(end, 1, length))
            && !hasMarker(selected)) {
        replaceRange(cursor, start - size, end + size, selected, std::pair{0, int(selected.size())});
    } else if (selected.size() >= 2 * size && holds(runFrom(start, 1, end))
               && holds(runFrom(end - 1, -1, start - 1)) && !hasMarker(inner)
               && charAt(start - 1) != mark && charAt(end) != mark) {
        replaceRange(cursor, start, end, inner, std::pair{0, int(inner.size())});
    } else {
        replaceRange(cursor, start, end, marker + selected + marker,
                     std::pair{size, size + int(selected.size())});
    }
}

void insertLink(QTextCursor &cursor, const QString &clipboardUrl) {
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    const QString selected = textBetween(cursor.document(), start, end);
    const QString label = selected.isEmpty() ? QStringLiteral("link text") : selected;
    const QString markdown =
        markdownLink(label, clipboardUrl.isEmpty() ? QStringLiteral("https://") : clipboardUrl);
    const int labelLength = int(escapeLinkText(label).size());
    if (selected.isEmpty())
        replaceRange(cursor, start, end, markdown, std::pair{1, 1 + labelLength});
    else if (clipboardUrl.isEmpty())
        replaceRange(cursor, start, end, markdown, std::pair{labelLength + 3, int(markdown.size()) - 1});
    else
        replaceRange(cursor, start, end, markdown);
}

bool pasteUrlAsLink(QTextCursor &cursor, const QString &clipboardUrl) {
    if (!cursor.hasSelection() || clipboardUrl.isEmpty())
        return false;

    const QString selected = textBetween(cursor.document(), cursor.selectionStart(), cursor.selectionEnd());
    // Spaces selected along with the words stay outside the link.
    static const QRegularExpression leadingSpace(QStringLiteral("^\\s*"));
    static const QRegularExpression trailingSpace(QStringLiteral("\\s*$"));
    const QString leading = leadingSpace.match(selected).captured();
    const QString trailing = trailingSpace.match(selected).captured();
    if (leading.size() == selected.size())
        return false;
    const QString linkText = selected.mid(leading.size(), selected.size() - leading.size() - trailing.size());
    replaceSelection(cursor, leading + markdownLink(linkText, clipboardUrl) + trailing);
    return true;
}

void pastePlainText(QTextCursor &cursor, const QString &text) {
    if (!text.isEmpty())
        replaceSelection(cursor, text);
}

void smartReturn(QTextCursor &cursor, bool softBreak, bool insideFence) {
    if (softBreak || insideFence) {
        replaceSelection(cursor, QStringLiteral("\n"));
        return;
    }

    // Only the lines involved are read; a copy of the whole text costs as
    // much as the document is long.
    QTextDocument *document = cursor.document();
    const int position = cursor.position();
    const int start = lineStart(document, position);
    const QString line = textBetween(document, start, position);
    static const QRegularExpression item(QStringLiteral("^(\\s*)([-+*]|\\d+[.)]|>+)\\s+(.*)$"));
    const QRegularExpressionMatch match = item.match(line);
    if (match.hasMatch()) {
        if (match.captured(3).isEmpty()) {
            replaceRange(cursor, start, position, QStringLiteral("\n"));
        } else {
            QString marker = match.captured(2);
            if (marker.at(0).isDigit())
                marker = QString::number(marker.chopped(1).toInt() + 1) + marker.back();
            replaceSelection(cursor, QLatin1Char('\n') + match.captured(1) + marker + QLatin1Char(' '));
        }
        return;
    }

    // A new paragraph stands apart from the one above it by a blank line,
    // which is the second break here. A line that is already blank has
    // nothing to stand apart from, so that break would only be a gap nobody
    // asked for. The break lands on what the selection leaves behind, which
    // is not the caret's line when it was dragged right to left.
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QString head = textBetween(document, lineStart(document, selectionStart), selectionStart);
    const QString rest = textBetween(document, selectionEnd, lineEnd(document, selectionEnd));
    replaceSelection(cursor, isBlank(head) && isBlank(rest) ? QStringLiteral("\n")
                                                            : QStringLiteral("\n\n"));
}

bool deleteParagraphBreakBehind(QTextCursor &cursor) {
    QTextDocument *document = cursor.document();
    const int position = cursor.position();
    if (cursor.hasSelection() || position < 2)
        return false;
    if (textBetween(document, position - 2, position) != QStringLiteral("\n\n"))
        return false;

    const int start = position - 2;
    const int end = lineEnd(document, position);
    // Something on the caret's line and the pair above it is a paragraph
    // break and nothing else, either the one Return wrote to end a paragraph
    // or the one the writer is now closing to join what it separates. Both
    // breaks go.
    if (isBlank(textBetween(document, position, end))) {
        // On a blank line Return writes a single break, so up here the pair
        // may be that break and one that was already in the document. Above a
        // blank line Return writes a single break too, and so wrote neither.
        if (isBlank(textBetween(document, lineStart(document, start), start)))
            return false;
        // Past that the two Returns leave the same text and the same caret,
        // and no reading of either says which was pressed. What decides
        // instead is that the gap is left standing: both breaks go only while
        // a blank line of it survives them. The end of the document below the
        // caret leaves it none...
        if (!document->findBlock(position).next().isValid())
            return false;
        // ...and so does the next paragraph, which the pair would otherwise be
        // pulled up against.
        if (!isBlank(textBetween(document, end + 1, lineEnd(document, end + 1))))
            return false;
    }

    replaceRange(cursor, start, position, QString());
    return true;
}

bool nestListItems(QTextCursor &cursor, bool outdent, const std::function<bool(int)> &insideFence) {
    QTextDocument *document = cursor.document();
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    // A selection that ends at the start of a line leaves that line be.
    const int start = lineStart(document, selectionStart);
    const int end = lineEnd(document, qMax(start, selectionEnd - 1));
    const QStringList lines = textBetween(document, start, end).split(QLatin1Char('\n'));

    // The lines start at an item and hold only lines that belong to one:
    // other items, blanks, and indented lines. A fence indented under an item
    // moves along with it whole; a selection that starts or ends inside code
    // would split it.
    QStringList filled;
    for (const QString &line : lines) {
        if (hasText(line))
            filled.append(line);
    }
    const bool strayParagraph = std::any_of(filled.cbegin(), filled.cend(), [](const QString &line) {
        return !line.at(0).isSpace() && !isListItem(line);
    });
    if (filled.isEmpty() || !isListItem(filled.first()) || strayParagraph || insideFence(start)
            || insideFence(end))
        return false;

    // Every line moves by the same step, so the nesting among them is kept.
    // Four spaces nest under any marker up to "99.", and Shift+Tab lifts no
    // further than the shallowest line can go.
    int step = 4;
    if (outdent) {
        for (const QString &line : std::as_const(filled))
            step = qMin(step, indentColumns(line));
        step = -step;
    }
    if (step == 0)
        return true;
    QStringList moved;
    for (const QString &line : lines) {
        moved.append(hasText(line)
                         ? QString(indentColumns(line) + step, QLatin1Char(' ')) + line.mid(indentLength(line))
                         : line);
    }
    const QString nested = moved.join(QLatin1Char('\n'));

    // Neither end of the selection leaves its line, even when the indent it
    // sat in is gone, and the selection keeps its direction.
    const int from = qMax(start, selectionStart + int(moved.first().size() - lines.first().size()));
    const int lastStart = start + int(nested.size() - moved.last().size());
    const int to = qMax(qMax(from, lastStart), selectionEnd + int(nested.size()) - (end - start));
    const bool forward = cursor.position() == selectionEnd;
    replaceRange(cursor, start, end, nested);
    cursor.setPosition(forward ? from : to);
    cursor.setPosition(forward ? to : from, QTextCursor::KeepAnchor);
    return true;
}

}
