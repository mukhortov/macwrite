#pragma once

#include <QString>

#include <functional>
#include <optional>
#include <utility>

#include "backend.h"

class QTextCursor;

// The editor's Markdown rules: what Return, Backspace, pasting and the
// formatting commands do to the text. Each works on the document a cursor
// belongs to, starting from the cursor's caret or selection and leaving the
// cursor where the writer's caret or selection lands, and each is one edit,
// so one undo takes it back.
namespace MarkdownEdits {

// Line endings from elsewhere, \r\n and \r, become \n.
QString normalizePlainText(const QString &text);
QString escapeLinkText(const QString &text);
QString escapeLinkDestination(const QString &url);
QString markdownLink(const QString &label, const QString &url);

// Replaces [start, end). Offsets into the replacement select that part of it,
// equal ones placing the caret; without them the caret follows it.
QString replaceRange(QTextCursor &cursor, int start, int end, const QString &replacement,
                     std::optional<std::pair<int, int>> select = std::nullopt);

// Wraps the selection in the marker, or unwraps it when the marker is already
// there, selected or just outside the selection. With nothing selected it
// acts on the span around the caret.
void toggleWrap(QTextCursor &cursor, const QString &marker, const std::optional<EmphasisSpan> &span);

// Links the selection, or a placeholder, to the clipboard's URL, and selects
// what is left to type.
void insertLink(QTextCursor &cursor, const QString &clipboardUrl);

// Links the selected text to a pasted URL. False when there is nothing to
// link: no selection, no URL, or only spaces selected.
bool pasteUrlAsLink(QTextCursor &cursor, const QString &clipboardUrl);
void pastePlainText(QTextCursor &cursor, const QString &text);

// Return: lists, numbers and quotes carry on, an empty item ends them, and a
// paragraph ends with a blank line. Inside fenced code each line is its own.
void smartReturn(QTextCursor &cursor, bool softBreak, bool insideFence);

// Backspace right after a paragraph break takes the whole break. False when
// there is none to take and the editor's own Backspace should run.
bool deleteParagraphBreakBehind(QTextCursor &cursor);

// Tab nests the list items the caret or selection touches four spaces
// deeper, and Shift+Tab lifts them back out by the same step, keeping the
// nesting among them. False, for the editor to type a tab, unless the lines
// start at an item and hold only items, their indented lines and blanks,
// with neither end inside fenced code.
bool nestListItems(QTextCursor &cursor, bool outdent, const std::function<bool(int)> &insideFence);

}
