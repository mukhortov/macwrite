# Write's Interface in Qt Widgets: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Write's QML interface with a native-looking Qt Widgets one, so the app links no Qt Quick, QtQml or JavaScript engine, with every feature and the large-file speed kept.

**Architecture:**

- Each document gets a `MainWindow` (`QMainWindow`) that holds:
  - a `MarkdownEditor` (`QTextEdit` laid out by the existing `PlainTextLayout`);
  - a `FindBar`;
  - a status line.
- The editing rules move from JavaScript into `markdownedits`, plain functions on a `QTextCursor`.
- `Backend` stays the per-document model, with typed C++ calls replacing its QML-shaped ones.
- `WindowManager` creates `MainWindow`s instead of loading `Main.qml`.
- The QML stays working until Task 13 removes it, so the app builds and `bin/test` passes after every task.

**Tech Stack:** C++17, Qt 6.12 (Homebrew), Qt Widgets, QtTest, qmake, macOS.

**Spec:** `docs/superpowers/specs/2026-10-19-widgets-interface-design.md`

## Global Constraints

- Branch `peter/widgets-ui`. Commit messages: an imperative subject and a prose body saying why. No `Co-Authored-By` or any other AI attribution.
- macOS only. After Task 13: `QT = core gui widgets printsupport concurrent` in `write.pro` and `tests/tests.pro`.
- Every new source file is added to both `write.pro` and `tests/tests.pro`.
- Comments follow the codebase: prose saying why, not what.
- Editor font: `backend->editorFont()` at `max(1, round(20 * textScale))` px, `QFont::Normal`, with `QFont::PreferNoShaping` when `backend->editorFontFixedPitch()`.
- Column width: `min(round(avgCharWidth * 65), max(360, windowWidth - round(avgCharWidth * 20)), max(0, windowWidth - 48))`.
- Space above the first line: `max(42, round(0.05 * windowHeight))` px. Space below the last line: 220 px.
- Caret margin: `2 * fontPixelSize`. Page step: `max(fontPixelSize, viewportHeight - 2 * fontPixelSize)`.
- Placeholder `# Start writing`. Muted colour `#909191` (dark) / `#aeb1b5` (light).
- Search highlights:
  - match `#725b18` (dark) / `#ffe58a` (light);
  - current match `#b36b20` (dark) / `#ffad42` (light);
  - at most 2,000 in view.
- ⌘+scroll text size: one step per 120 angle units, or per 50 px of touchpad pixels banked.
- Every edit the editor makes is one undo step.
- Sheet wording:
  - Unsaved changes: "Do you want to save the changes you made to “<name>”?" / "Your changes will be lost if you don't save them." Buttons Save, Don't Save, Cancel.
  - File changed: "File changed" or "File removed", with today's three explanations. Buttons Keep Mine, Reload.

**Commands.** A single test: `cd build-tests && make -j10 && QT_QPA_PLATFORM=offscreen ./tst_write <testName>`. The full suite: `bin/test` (it also creates `build-tests`).

## Review Focus

1. **Input-method composition** (dead keys, Japanese). A preedit inside a `PlainTextLayout` document must draw, place the caret and commit like Qt's layout. *Task 5: `composesTextWithAnInputMethod`.*
2. **Right-to-left and mixed-direction lines.** The caret and selection must draw where Qt's layout draws them. *Task 1: the drawing test includes the Arabic and Hebrew sample lines, with the caret inside them.*
3. **One-letter searches in a huge file** (about 350,000 matches). Highlighting and counting must stay instant while scrolling. *Task 4: `findsMatchesInViewAmongManyQuickly`.*
4. **Closing or quitting while a sheet or the Fonts panel is up.** There must be no second sheet, and the quit must wait for the answer. *Task 10: `holdsCommandsWhileASheetIsUp`.*
5. **Resizing a wide window with a long document.** When the column width stays the same, nothing is laid out again. *Task 1: `movesBlocksWithoutLayingThemOutWhenOnlyMarginsChange`. Task 5: `resizesAWideWindowWithoutLayingOut`.*

---

### Task 1: PlainTextLayout draws like Qt, and moves instead of relaying out for margin changes

`QTextEdit` relies on the layout to paint text, selections and the caret. The editor will also centre its column by changing the root frame's margins on every resize.

**Files:**
- Modify: `src/plaintextlayout.h`, `src/plaintextlayout.cpp`
- Test: `tests/tst_write.cpp`

**Interfaces:**
- Produces:
  - `PlainTextLayout::draw(QPainter *, const PaintContext &)` paints the same pixels as `QTextDocumentLayout::draw`.
  - A change to the root frame's top, bottom, left or right margin that leaves the content width as it was lays out no blocks; `lastLayoutBlockCount()` is then `0`.
  - The caret width comes from the dynamic property `"cursorWidth"`, defaulting to 1. That is how `QWidgetTextControl` passes it to any layout that isn't a `QTextDocumentLayout`.

- [ ] **Step 1: Write the failing tests**

`plainTextLayoutDrawsLikeQt`:
- Build `qt` and `ours` with `setUpLayoutDocument` / `fillLayoutDocument(layoutSample())` and a `MarkdownHighlighter`, at width 480.
- For each case below, draw both through `documentLayout()->draw` into a `QImage` of the document size (`Format_ARGB32_Premultiplied`, filled white), with `PaintContext::palette` Text `#d6d6d6` and Highlight `#264f78`, and `QCOMPARE` the two images. The cases:
  - `cursorPosition` -1;
  - 0;
  - inside the Arabic line;
  - inside the Hebrew line;
  - at the end;
  - a selection from the middle of block 2 to the middle of block 5;
  - two extra `Selection`s with backgrounds `#725b18` and `#b36b20`;
  - a clip rect covering only the middle third.

`movesBlocksWithoutLayingThemOutWhenOnlyMarginsChange`:
- On both documents, set the root frame's left margin to 40 and right margin to 0, then left 0 and right 40, then top 80 and bottom 220.
- After each change, `QCOMPARE(layout->lastLayoutBlockCount(), 0)` for ours, and `QVERIFY(layoutDifference(&qt, &ours, true).isEmpty())`.

- [ ] **Step 2: Run them to verify they fail**

Expected: `plainTextLayoutDrawsLikeQt` fails, because the images differ (no caret, no selections). The margin test fails with `lastLayoutBlockCount()` equal to the block count.

- [ ] **Step 3: Implement**

- `draw()`: follow `QTextDocumentLayoutPrivate::drawBlock` and `drawFlow` in Qt 6.12's `qtbase/src/gui/text/qtextdocumentlayout.cpp` (copy in the scratchpad at `qtsrc/qtextdocumentlayout.cpp`):
  - the pen from `context.palette.text()`;
  - the context's selections clipped to each block as `QTextLayout::FormatRange`s;
  - `QTextLayout::draw` with the clip;
  - `drawCursor` for the block holding `context.cursorPosition`, including preedit positions.
- `documentChanged`: when `currentFrame()` differs from `m_frame` only in `top`, `bottom`, `left` or `marginWidth`, store the new frame, call `updateSize()`, set `m_lastLayoutBlockCount = 0`, emit `update`, and return. Positions already come from `m_frame` lazily in `blockBoundingRect`.

- [ ] **Step 4: Run the tests and the existing layout tests**

Run `plainTextLayoutDrawsLikeQt`, `movesBlocksWithoutLayingThemOutWhenOnlyMarginsChange`, `plainTextLayoutMatchesQtsLayout`, `plainTextLayoutFollowsEditsLikeQt`. Expected: all PASS.

- [ ] **Step 5: Commit** ("Draw the caret and selections in PlainTextLayout, and move blocks for margin changes")

### Task 2: Backend speaks C++ to the widgets

**Files:**
- Modify: `src/backend.h`, `src/backend.cpp`, `src/Main.qml` (three call sites), `tests/tst_write.cpp` (helpers `makeEditor`, `attachTextEdit`)

**Interfaces:**
- Produces, in `backend.h`:

```cpp
struct TextRange { int start = 0; int end = 0; };
struct EmphasisSpan { int start = 0; int end = 0; int contentStart = 0; int contentEnd = 0; int textEnd = 0; };
struct WindowGeometry { int x = -1; int y = -1; int width = 1280; int height = 820; bool maximized = false; };

void Backend::attachDocument(QTextDocument *document);
QList<TextRange> Backend::hiddenRangesAt(int position) const;
std::optional<EmphasisSpan> Backend::emphasisSpanAt(int position, const QString &marker) const;
WindowGeometry Backend::windowGeometry() const;
```

- Transitional, for QML only, removed in Task 13: `Q_INVOKABLE attachQuickDocument(QObject *)` (it sets the viewport flag, then calls `attachDocument`), `qmlHiddenRangesAt`, `qmlEmphasisSpanAt`, `qmlWindowGeometry`, with today's `QVariant` shapes.

- [ ] **Step 1: Write the failing test `answersEditorsInPlainTypes`**
  - Attach a `QTextDocument` holding `"a **b** c"`.
  - `hiddenRangesAt(3)` returns `{2,4}` and `{5,7}`.
  - `emphasisSpanAt(4, "**")` returns start 2, end 7, contentStart 4, contentEnd 5, textEnd 5.
  - `emphasisSpanAt(0, "*")` returns `std::nullopt`.
  - In a fresh `QSettings`, `windowGeometry()` returns `{-1, -1, 1280, 820, false}`.
- [ ] **Step 2: Run it.** Expected: compile failure, as the types don't exist.
- [ ] **Step 3: Implement** the typed functions, moving today's bodies into them, and make the `qml*` wrappers convert. Rename the QML callers in `Main.qml` (`attachDocument`, `hiddenRangesAt`, `emphasisSpanAt`, `windowGeometry`) and in the two test helpers.
- [ ] **Step 4: Run `bin/test`.** Expected: all PASS.
- [ ] **Step 5: Commit** ("Give Backend typed calls for a C++ interface")

### Task 3: markdownedits: the editing rules in C++

These are `EditorMutations.js` and the editor functions of `Main.qml` (`smartReturn`, `deleteParagraphBreakBehindCursor`, `insertLink`, the paste functions). Today's JavaScript is the reference for every branch.

**Files:**
- Create: `src/markdownedits.h`, `src/markdownedits.cpp`
- Modify: `write.pro`, `tests/tests.pro`
- Test: `tests/tst_write.cpp`

**Interfaces:**
- Consumes: `EmphasisSpan` (Task 2).
- Produces (namespace `MarkdownEdits`). A `QTextCursor &` carries the editor's selection in and the resulting caret or selection out.

```cpp
QString normalizePlainText(const QString &text);          // \r\n and \r become \n
QString escapeLinkText(const QString &text);              // \ [ ] get a backslash
QString escapeLinkDestination(const QString &url);        // \ ( ) get a backslash
QString markdownLink(const QString &label, const QString &url);
// Replaces [start, end) as one undo step. Offsets into the replacement select
// it (equal offsets place the caret); without them the caret follows it.
QString replaceRange(QTextCursor &cursor, int start, int end, const QString &replacement,
                     std::optional<std::pair<int, int>> select = std::nullopt);
void toggleWrap(QTextCursor &cursor, const QString &marker, const std::optional<EmphasisSpan> &span);
void insertLink(QTextCursor &cursor, const QString &clipboardUrl);
bool pasteUrlAsLink(QTextCursor &cursor, const QString &clipboardUrl);
void pastePlainText(QTextCursor &cursor, const QString &text);
void smartReturn(QTextCursor &cursor, bool softBreak, bool insideFence);
bool deleteParagraphBreakBehind(QTextCursor &cursor);
```

- [ ] **Step 1: Write the failing tests**, each on a `QTextDocument` with a cursor set to the stated selection:

`markdownEditsWrapAndUnwrap`:
- `toggleWrap` on selected `b` in `"a b c"` with `"**"` gives `"a **b** c"`, with `b` selected.
- Again gives `"a b c"`.
- `"*"` inside `"**bold**"` wraps, rather than taking the `**` for italic.
- With nothing selected:
  - the caret at the span's end steps to `textEnd`;
  - the caret inside the span unwraps it, keeping the caret on the same letter.

`markdownEditsInsertLinks`:
- With nothing selected and no URL: `"[link text](https://)"`, with `link text` selected.
- With `word` selected and no URL: `"[word](https://)"`, with `https://` selected.
- With `word` selected and URL `"https://a.b/(x)"`: `"[word](https://a.b/\\(x\\))"`.
- `escapeLinkText("a[b]\\")` is `"a\\[b\\]\\\\"`.

`markdownEditsPaste`:
- `pasteUrlAsLink` over `" word "` with `"https://x.y"` gives `" [word](https://x.y) "` and returns `true`.
- It returns `false` with nothing selected, with an empty URL, or with a blank selection.
- `pastePlainText` turns `"a\r\nb\rc"` into `"a\nb\nc"`.

`markdownEditsReturn`: every case from `breaksLinesOnReturn`, through `smartReturn` instead of key presses. That covers lists, numbering (`9.` → `10.`), `>` quotes, the empty item, fences (`insideFence = true` gives one break), the blank line, Shift (`softBreak`), and a selection dragged right to left.

`markdownEditsBackspace`: every case from `closesParagraphBreaksOnBackspace`, through `deleteParagraphBreakBehind`.

`markdownEditsUndoInOneStep`: after each operation above, one `document->undo()` restores the text as it was before.

- [ ] **Step 2: Run them.** Expected: compile failure.
- [ ] **Step 3: Implement** `src/markdownedits.cpp`, porting each JS function branch for branch. Each edit uses `beginEditBlock` / `endEditBlock` on a cursor of the same document, so it's one undo step.
- [ ] **Step 4: Run the six tests.** Expected: PASS.
- [ ] **Step 5: Commit** ("Port the Markdown editing rules to C++")

### Task 4: SearchMatches

**Files:**
- Create: `src/findbar.h`, `src/findbar.cpp` (`SearchMatches` only for now)
- Modify: `write.pro`, `tests/tests.pro`
- Test: `tests/tst_write.cpp`

**Interfaces:**
- Produces:

```cpp
struct SearchHighlight { int start = 0; int end = 0; bool current = false; };
class SearchMatches {
public:
    // New results for the query. selectFirst picks match 0; otherwise the
    // current match is the first at or after the old current start, wrapping to 0.
    void update(const QList<int> &starts, int queryLength, bool selectFirst);
    int count() const;
    int currentIndex() const;   // -1 with no matches
    int currentStart() const;   // -1 with no matches
    int queryLength() const;
    void step(int direction);    // wraps both ways
    void setCurrentAtOrAfter(int position);
    int firstAtOrAfter(int position) const;   // binary search; count() when none
    // Matches overlapping [first, last], at most `limit`.
    QList<SearchHighlight> inRange(int first, int last, int limit = 2000) const;
    QString countText() const;   // "" for an empty query, "N of M", "0 of 0"
};
```

- [ ] **Step 1: Write the failing tests**

`searchMatchesStepAndFollow`:
- Starts `{2, 10, 30}`, query length 3, `selectFirst`: current 0, `countText() == "1 of 3"`.
- `step(-1)` gives current 2. `step(1)` gives 0.
- `update({4, 12, 32}, 3, false)` from current start 10 gives current start 12.
- `setCurrentAtOrAfter(31)` gives 32; `setCurrentAtOrAfter(40)` wraps to 4.
- Empty starts: `countText() == "0 of 0"` and `currentStart() == -1`.

`findsMatchesInViewAmongManyQuickly`:
- 350,000 starts spaced 15 apart, query length 1.
- `inRange(1'000'000, 1'003'000)` returns the 200 matches between them, none marked `current`, as the current match is the first.
- `inRange(0, 6'000'000).size() == 2000`.
- Both calls together take under 5 ms by `QElapsedTimer`.

- [ ] **Step 2: Run them.** Expected: compile failure.
- [ ] **Step 3: Implement** as today's `firstMatchFrom`, `matchAtOrAfter` and `updateVisibleSearchMatches` in `Main.qml`. `inRange` starts at `firstAtOrAfter(first - queryLength + 1)`.
- [ ] **Step 4: Run them.** Expected: PASS.
- [ ] **Step 5: Commit** ("Keep search matches in a class of their own")

### Task 5: MarkdownEditor: column, font, theme, placeholder (spike gate)

**Files:**
- Create: `src/markdowneditor.h`, `src/markdowneditor.cpp`, `tests/bench/bench.pro`, `tests/bench/editorbench.cpp`
- Modify: `write.pro`, `tests/tests.pro`
- Test: `tests/tst_write.cpp`

**Interfaces:**
- Consumes: `Backend::attachDocument(QTextDocument *)`, `PlainTextLayout`.
- Produces:

```cpp
class MarkdownEditor : public QTextEdit {
    Q_OBJECT
public:
    explicit MarkdownEditor(Backend *backend, QWidget *parent = nullptr);  // objectName "sourceEditor"
    static int columnWidth(qreal averageCharWidth, int windowWidth);  // the Global Constraints formula
    int fontPixelSize() const;                                         // max(1, round(20 * textScale))
    void ensureCaretVisible();                                         // keeps 2 * fontPixelSize() of margin
    std::pair<int, int> visiblePositions() const;                      // hit tests at the viewport's corners
signals:
    void viewChanged();   // scrolled, resized or reflowed
};
```

- Construction:
  - `setAcceptRichText(false)`, `WidgetWidth` wrap;
  - installs `PlainTextLayout`, then calls `backend->attachDocument(document())`;
  - applies font, palette and column, and reapplies them on `editorFontChanged`, `textScaleChanged`, `themeColorsChanged` and resize.
- Applying the font calls `backend->documentFontChanged()`.
- Palette: Base = `themeBackground`, Text = `themeForeground`, Highlight = `themeSelection`, HighlightedText = `themeSelectionForeground`.
- The column comes from root frame margins:
  - left = `24 + round((width() - 48 - column) / 2)`;
  - right = `viewport width - left - column`;
  - top = `max(42, round(0.05 * window()->height()))`;
  - bottom = 220.

- [ ] **Step 1: Write the failing tests.** `EditorFixture` (in the test file) holds a `Backend` and a shown `MarkdownEditor`, with `load(text, caret)` and `text()`.

| Test | Asserts |
|---|---|
| `centresTheColumnAtTheTextSize` | At width 1280, the text column is `columnWidth(avg, 1280)` wide and centred within one pixel. After `backend.increaseTextSize()`, the font pixel size is `round(20 * textScale())`. |
| `keepsTextColumnInsideNarrowWindows` | Port: at width 400, the column is `400 - 48`, and no line is wider than the viewport. |
| `resizesAWideWindowWithoutLayingOut` | With 20,000 lines at width 2000, resizing to 2100 leaves `lastLayoutBlockCount() == 0`. |
| `showsThePlaceholderOnlyWhileEmptyAndUnfocused` | Grab the viewport into an image. The muted colour is present while empty and unfocused; absent after `setFocus()`, and after typing `"a"` and focusing another widget. |
| `keepsTheCaretTwoLinesInsideTheView` | Load 300 lines and put the caret on the last one. The caret rect's bottom is at most `viewport height - 2 * fontPixelSize()`. |
| `composesTextWithAnInputMethod` | Send a `QInputMethodEvent` with preedit `"か"`: `cursorRect()` is valid and inside the viewport, and painting doesn't crash. Commit `"漢"`: the text contains `"漢"`, and one undo removes it. |
| `typingInALongDocumentLaysOutOnlyTheEditedLine` | Port to `EditorFixture` with `QTest::keyClick(editor, 'x')`, the same assertions. |

- [ ] **Step 2: Run them.** Expected: compile failure.
- [ ] **Step 3: Implement `MarkdownEditor`** with the interface above. The placeholder is painted in `paintEvent` after `QTextEdit::paintEvent`, at the first line's position, in the editor font and the muted colour. `viewChanged` is emitted from the scroll bar's `valueChanged`, `resizeEvent` and the layout's `documentSizeChanged`.
- [ ] **Step 4: Run them.** Expected: PASS.
- [ ] **Step 5: Spike gate: the real file**
  - Write `tests/bench/editorbench.cpp`. It takes a file and a key count, copies the file into a `QTemporaryDir`, opens it in a shown `MarkdownEditor` via `backend.openOrStartFile`, and puts the caret at position 5.
  - For each key it calls `QTest::keyClick` and then `viewport()->repaint()`, and prints the average and maximum per-key time and the open time.
  - Run it on `/Users/peter/Personal/5.4mb-test-file.md` with 40 keys, once offscreen and once on the Cocoa platform.
  - Expected: per-key average ≤ 17 ms, and open ≤ 1.2 s.
  - By hand on Cocoa: typing, selecting with the mouse, ⇧-arrow selection, and composing `é` with ⌥E all behave.

  **If the per-key time exceeds 17 ms, or any of those misbehave, stop and ask the writer** (spec, risk 1).
- [ ] **Step 6: Commit** ("Add the widget editor with Write's column, font and theme")

### Task 6: MarkdownEditor: keys, paste, emphasis, links

**Files:**
- Modify: `src/markdowneditor.h`, `src/markdowneditor.cpp`
- Test: `tests/tst_write.cpp`

**Interfaces:**
- Consumes: `MarkdownEdits::*` (Task 3), `Backend::emphasisSpanAt`, `hiddenRangesAt`, `cursorPositionStep`, `insideFence`, `clipboardUrl`, `clipboardText`.
- Produces: `void MarkdownEditor::toggleWrap(const QString &marker)`, `void MarkdownEditor::insertLink()`.
  - `keyPressEvent`:
    - Return and Enter without ⌘/⌥/⌃ go to `smartReturn`, with Shift meaning `softBreak`.
    - Backspace goes to `deleteParagraphBreakBehind`, falling back to `QTextEdit`.
    - Left and Right without modifiers collapse a selection, or step with `cursorPositionStep` and then skip hidden ranges (today's `skipHiddenForward` / `skipHiddenBackward`).
    - Page Up and Page Down move by the page step via `cursorForPosition`, with Shift extending the selection.
  - `insertFromMimeData` tries `pasteUrlAsLink`, then `pastePlainText` of the clipboard's text. `canInsertFromMimeData` accepts text.

- [ ] **Step 1: Port the failing tests to `EditorFixture`**, with the same texts and expected values, using `QTest::keyClick(editor, …)` and `editor->toPlainText()`:
  - `breaksLinesOnReturn`
  - `breaksLinesAfterInlineBackticks`
  - `closesParagraphBreaksOnBackspace`
  - `togglesEmphasisAtTheCaret`
  - `togglesEmphasisMarkers`
  - `keepsCursorAndSelectionStableAcrossInsertions`
  - `stepsOverWholeCharactersWhenTypingAndSaving`
  - `stepsCursorByWholeGraphemeClusters`
  - `undoesAnEditInOneStep`
  - `keepsLineHeightAcrossUndoAndPaste`

  Delete their QML versions.

  Add `pagesByTheViewLessTwoLines`: with 500 short lines, Page Down moves the caret's y by `pageStep ± one line`, and ⇧Page Down extends the selection.
- [ ] **Step 2: Run them.** Expected: FAIL (default `QTextEdit` behaviour).
- [ ] **Step 3: Implement** as described in the Interfaces block above.
- [ ] **Step 4: Run them.** Expected: PASS.
- [ ] **Step 5: Commit** ("Move Return, Backspace, arrows, paste and emphasis into the editor")

### Task 7: MarkdownEditor: link tooltip, ⌘-scroll text size, search highlights

**Files:**
- Modify: `src/markdowneditor.h`, `src/markdowneditor.cpp`
- Test: `tests/tst_write.cpp`

**Interfaces:**
- Consumes: `SearchHighlight` (Task 4), `Backend::linkUrlAt`, `increaseTextSize`, `decreaseTextSize`, `darkMode`.
- Produces:
  - `void MarkdownEditor::setSearchHighlights(const QList<SearchHighlight> &highlights)` sets the extra selections, with the Global Constraints colours by `backend->darkMode()`.
  - `QString MarkdownEditor::hoveredLink() const` returns the URL the pointer is over, or empty.
  - `mouseMoveEvent` shows the URL with `QToolTip::showText` 12 px right and 20 px below the pointer, as plain text, elided in the middle to `window()->width() - 32`. A character counts only if the pointer is inside its box (today's `linkUrlUnder`).
  - `wheelEvent` with `Qt::ControlModifier` banks steps:
    - one step per 120 angle units, or per 50 px of `pixelDelta`;
    - the bank resets when the direction flips.

- [ ] **Step 1: Port the failing tests**:
  - `showsLinkUrlOnlyOverTheLinkText` and `showsLinkTooltipUrlAsPlainText`, checking `hoveredLink()` and the tooltip text;
  - `banksTouchpadScrollBeforeChangingTextSize`, sending `QWheelEvent`s with `ControlModifier`.

  Add `highlightsSearchMatchesInTheirColours`: `setSearchHighlights({{2, 5, false}, {9, 12, true}})` gives two extra selections, with backgrounds `#725b18` and `#b36b20` in dark mode and `#ffe58a` and `#ffad42` in light mode.
- [ ] **Step 2: Run them.** Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them.** Expected: PASS.
- [ ] **Step 5: Commit** ("Show links, zoom with ⌘-scroll and highlight matches in the editor")

### Task 8: FindBar

**Files:**
- Modify: `src/findbar.h`, `src/findbar.cpp`
- Test: `tests/tst_write.cpp`

**Interfaces:**
- Consumes: `SearchMatches`, `MarkdownEditor::setSearchHighlights`, `visiblePositions`, `viewChanged`, `ensureCaretVisible`, `Backend::searchMatches`, `MarkdownEdits::replaceRange`.
- Produces:

```cpp
class FindBar : public QWidget {   // objectName "findBar", hidden until opened
    Q_OBJECT
public:
    FindBar(MarkdownEditor *editor, Backend *backend, QWidget *parent = nullptr);
    void open(bool withReplace);   // shows; focuses the search field and selects its text
    void close();                  // hides; clears highlights and the selection; focuses the editor
    bool isOpen() const;
    void documentChanged();        // matches follow an edit without moving the caret
    void step(int direction);
    QLineEdit *searchField() const;    // "searchField", placeholder "Find"
    QLineEdit *replaceField() const;   // "replaceField", placeholder "Replace with"
    const SearchMatches &matches() const;
};
```

- Other children:
  - `QLabel "matchCount"`, showing `countText()`;
  - `QToolButton`s `"previousMatchButton"` and `"nextMatchButton"`;
  - `QPushButton`s `"doneButton"` (Done), `"replaceButton"` (Replace) and `"replaceAllButton"` (All).
- Typing in the search field selects the first match.
- Return goes to the next match, ⇧Return to the previous one; Return in the replace field runs Replace; Esc in either field closes the bar.
- Replace carries on from `start + replacement length`.
- All is one `replaceRange` over the span from the first match to the end of the last, built from `document()->toPlainText()` slices.

- [ ] **Step 1: Port the failing tests**:
  - `searchFollowsEditsWithoutTakingTheCaret`
  - `replacesOneMatchAfterAnother`
  - `reachesAndLeavesTheReplaceFieldFromTheKeyboard`

  Drive them with `FindBar` plus `MarkdownEditor` in a parent widget.

  Add `replacesEveryMatchInOneUndo`: replacing all 3 matches of `"x"` with `"yy"` in `"x x x"` gives `"yy yy yy"`, and one undo gives `"x x x"`.
- [ ] **Step 2: Run them.** Expected: compile failure.
- [ ] **Step 3: Implement.** On `viewChanged`, recompute `inRange(visiblePositions())` while open.
- [ ] **Step 4: Run them.** Expected: PASS.
- [ ] **Step 5: Commit** ("Add the find bar")

### Task 9: MainWindow: menus, commands, status line, title, full screen

**Files:**
- Create: `src/mainwindow.h`, `src/mainwindow.cpp`
- Modify: `write.pro`, `tests/tests.pro`
- Test: `tests/tst_write.cpp`

**Interfaces:**
- Consumes: `MarkdownEditor`, `FindBar`, `Backend` signals and commands.
- Produces:

```cpp
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Backend *backend, QWidget *parent = nullptr);
    MarkdownEditor *editor() const;
    FindBar *findBar() const;
    bool commandsEnabled() const;          // no sheet and no Fonts panel up
    void applyGeometry(const WindowGeometry &geometry);
signals:
    void activated();                      // became the active window
    void closeCancelled();                 // Task 10
};
```

- Menus and shortcuts: exactly the spec's File, Edit, View and Window menus.
  - Extra `QShortcut`s: ⌘+ for Bigger, ⌘Y for Redo, and ⌘G for the next match (enabled only while the find bar is open).
  - Every `QAction` uses `QAction::NoRole`, which replaces the zero-width-space trick.
  - Every command except Minimize and Zoom is enabled only when `commandsEnabled()`.
  - The full-screen item reads "Enter Full Screen" or "Exit Full Screen".
- Undo, Redo, Cut, Copy, Paste and Select All act on the focused `QLineEdit` when it's the search or replace field, and on the editor otherwise.
- Central widget: a `QVBoxLayout` holding `FindBar` above `MarkdownEditor`. Each text change calls `backend->editorTextChanged()`, then `findBar->documentChanged()` when that returns `true` and the bar is open.
- Status line: `QStatusBar` with:
  - a `QLabel "statusLabel"` (backend status, elided right to `min(360, width() / 3)`);
  - a permanent `QLabel "wordCountLabel"` (`"1 Word"` or `"N Words"`).
- Title: `fileName() + "[*]"`, with `setWindowModified(backend->modified())` and `setWindowFilePath` for a local file. Calls `backend->setParentWindow(windowHandle())` once created.

- [ ] **Step 1: Port the failing tests to `WindowFixture`** (a `Backend` plus a shown `MainWindow`, with `editor()`, `load()` and `text()`):
  - `putsEveryCommandInTheMenuBar` (the spec's list, with the Help menu and Keyboard Shortcuts gone);
  - `givesEveryKeyOneCommand`;
  - `runsEditCommandsFromTheMenu`;
  - `runsDocumentAndWindowCommandsFromTheMenu`;
  - `leavesMacSystemShortcutsAlone`;
  - `growsTextFromEitherPlusKey`.

  Find actions with `findChildren<QAction *>()` by text.

  Add `titlesTheWindowAfterItsFile`: the title is `"Untitled.md[*]"` and the window is unmodified; after typing it's modified; after saving to `x.md` the title is `"x.md[*]"` and the window is unmodified.

  Add `showsStatusAndWordCount`: after typing `"one two"` the word count label reads `"2 Words"` (`QTRY_COMPARE`).
- [ ] **Step 2: Run them.** Expected: compile failure.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them.** Expected: PASS.
- [ ] **Step 5: Commit** ("Add the main window with Write's menus and status line")

### Task 10: MainWindow: closing, sheets, file dialogs, Fonts panel

**Files:**
- Modify: `src/mainwindow.h`, `src/mainwindow.cpp`
- Test: `tests/tst_write.cpp`

**Interfaces:**
- Consumes: `Backend::save`, `saveAs`, `open`, `discardRecovery`, `holdAutoSave`, `keepExternalVersion`, `reloadFromDisk`, `setEditorFont`, and the signals `openDialogRequested`, `saveDialogRequested`, `saveSucceeded`, `saveFailed`, `externalChangeDetected`.
- Produces:
  - `void MainWindow::requestOpen(const QUrl &url)` asks first when there are unsaved changes.
  - `closeEvent` with unsaved changes ignores the close and opens the unsaved-changes sheet: a window-modal `QMessageBox` named `"unsavedChangesSheet"`, Save (default) / Discard / Cancel (escape). macOS labels Discard "Don't Save".
  - Pending close or open, as in `Main.qml`: Discard calls `discardRecovery()` and then completes; Save completes only on the next `saveSucceeded`; `saveFailed`, Cancel, or cancelling the save sheet cancels, and a cancelled close emits `closeCancelled()`. `holdAutoSave(true)` while the sheet is up.
  - External change: a `QMessageBox "externalChangeSheet"` with Keep Mine (escape, and the default for a removed file) and Reload (the default otherwise).
  - Open shows `QFileDialog::getOpenFileUrl`-style application-modal panel, Save As shows a window-modal `QFileDialog` sheet. Both use the filters "Markdown files (*.md *.markdown)" and "All files (*)".
  - View › Font… opens a `QFontDialog "fontsPanel"` at the editor's family. `fontSelected` calls `backend->setEditorFont(font.family())`. If the native panel misbehaves on Cocoa (Task 14), fall back to `QFontDialog::DontUseNativeDialog` (spec, risk 3).

- [ ] **Step 1: Port the failing tests**, answering sheets by clicking `QMessageBox::button(...)`:
  - `asksAboutUnsavedChangesBeforeClosingOrQuitting` (the single-window part);
  - `unsavedChangesDialogAnswersToItsKeys` (Return saves, Esc cancels, the Don't Save button discards; ⌘D and ⌘. come from the native alert and are checked by hand in Task 14);
  - `dropsAPendingCloseWhenItsSaveFails`;
  - `holdsAutoSaveWhileTheWriterDecides`;
  - `keepsTheWritersTextWhenTheChangeDialogIsDismissed`;
  - `keepsShortcutsBehindDialogs`;
  - `showsTheCaretAfterOpeningFromTheOpenDialog` (after `requestOpen` while another window has focus, the editor gets focus once the window is active, and the caret is inside the viewport).

  Add `holdsCommandsWhileASheetIsUp`: with the unsaved sheet open, Close Window is disabled, a second `close()` opens no second sheet, and `commandsEnabled()` is `false`. The same holds while the Fonts panel is visible.

  Add `choosesTheEditorFontFromTheFontsPanel`: `setCurrentFont(QFont("Menlo"))` and `accept()` set `backend.editorFont()` to `"Menlo"`.
- [ ] **Step 2: Run them.** Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them.** Expected: PASS.
- [ ] **Step 5: Commit** ("Ask about unsaved and outside changes in native sheets")

### Task 11: WindowManager and main.cpp run MainWindows

**Files:**
- Modify: `src/windowmanager.h`, `src/windowmanager.cpp`, `src/main.cpp`, `src/appeventfilter.cpp` (only the Quit comment)
- Test: `tests/tst_write.cpp`

**Interfaces:**
- Consumes: `MainWindow` (Tasks 9 and 10), `WindowGeometry`.
- Produces:

```cpp
explicit WindowManager(QObject *parent = nullptr);
QList<MainWindow *> windows() const;   // most recently active first
MainWindow *activeWindow() const;
Backend *backendFor(const MainWindow *window) const;
MainWindow *newWindow();
WindowGeometry cascadedGeometry() const;
```

- `MainWindow`s have `Qt::WA_DeleteOnClose`. Accepted closes, activation and destruction drive today's `windowClosed`, `windowActivated` and `windowDestroyed`.
- `main.cpp` builds the `WindowManager` without a QML engine. The Quick style and the scaled IBM Plex Mono interface font (`applyInterfaceFont`) go, so native controls use the system font. The QML files stay in the build until Task 13 and are unused.

- [ ] **Step 1: Port the failing tests to `MainWindow`**:
  - `runsEveryWindowInOneProcess`
  - `opensAWindowFromTheMenuBarWithNoWindowOpen`
  - `cascadesNewWindowsAndRemembersTheLastToClose`
  - `sharesTextSizeAndFontAcrossWindows`
  - `routesFilesOpenedFromFinder`
  - `reopensAWindowFromTheDock`
  - `quitsOnlyOnceEveryWindowAgrees`
  - `restoresEveryDraftACrashLeftBehind`
  - `followsTheSystemAppearance`
  - the multi-window part of `asksAboutUnsavedChangesBeforeClosingOrQuitting`
- [ ] **Step 2: Run them.** Expected: compile failure.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run `bin/test`.** Expected: all PASS.
- [ ] **Step 5: Check the real app.** Run `bin/build`, then `open build/Write.app` on a copy of `/Users/peter/Personal/DND-SRD-5.2-CC.md`. Expected: the window shows the document in the new interface, ⌘S saves, ⌘W closes, and ⌘Q quits.
- [ ] **Step 6: Commit** ("Run Write's windows as widgets")

### Task 12: Port the remaining backend tests to the widget editor

**Files:**
- Modify: `tests/tst_write.cpp`

The `makeEditor` and `attachTextEdit` helpers now return a `MarkdownEditor` on the given `Backend`. Replace `setProperty("text", …)` with `setPlainText`, `property("text")` with `toPlainText()`, and `cursorPosition` with `textCursor()`. Change only the expected values that name a QML detail.

- [ ] **Step 1: Switch the helpers and port**:
  - `restylesALongDocumentAtOnce`
  - `findsLinesWithoutCopyingTheText`
  - `keepsCaretRangesConsistentWithCode`
  - `copiesRenderedHtml`
  - all the auto-save, recovery, file-watching, revert and command-line tests in the QML-dependent list (from `autoSavesTitledDocumentsAfterEdits` to `keepsADocumentUnsavedAfterItsFileDisappears`)
- [ ] **Step 2: Run `bin/test`.** Expected: all PASS, with no test loading `Main.qml` except the four removed in Task 13.
- [ ] **Step 3: Commit** ("Test the backend through the widget editor")

### Task 13: Remove QML and Qt Quick

**Files:**
- Delete:
  - `src/Main.qml`, `src/FontPicker.qml`, `src/UnsavedChangesDialog.qml`, `src/ExternalChangeDialog.qml`
  - `src/FooterIconButton.qml`, `src/SearchIconButton.qml`, `src/SquareDialogButton.qml`
  - `src/EditorMutations.js`
- Modify:
  - `src/resources.qrc` (fonts only);
  - `src/backend.h` and `src/backend.cpp`: remove the `qml*` wrappers, `attachQuickDocument`, `Q_INVOKABLE`, `Q_PROPERTY`, and `availableFonts`/`selectableFontFamilies` with their CoreText code;
  - `write.pro` and `tests/tests.pro`: `QT = core gui widgets printsupport concurrent`; drop `-framework CoreText` if nothing else uses it;
  - `tests/tst_write.cpp`.
- Delete tests:
  - `togglesTheFontPickerFromItsButton`
  - `savesAndOpensFromFooterButtons`
  - `shortcutsDialogHasNoBindingLoop`
  - `flashesScrollBarOnlyWhenPaging`
  - `listsInstalledTextFontsOnce`
  - `keepsNotoFromCrowdingTheFontList`
  - the `BindingLoopCounter` helper and the Quick style in `initTestCase`

- [ ] **Step 1: Add the failing test `linksNoQtQuick`**: run `otool -L` on `build/Write.app/Contents/MacOS/Write` with `QProcess`, skipping when the app isn't built. Its output contains neither `QtQuick` nor `QtQml`.
- [ ] **Step 2: Run it after `bin/build`.** Expected: FAIL.
- [ ] **Step 3: Remove** the files and code listed above.
- [ ] **Step 4: Run `bin/build`, then `bin/test`.** Expected: the build succeeds and every test passes, including `linksNoQtQuick`. `grep -rn "qml\|Quick" src write.pro tests/tests.pro` shows nothing but comments explaining history, if any.
- [ ] **Step 5: Commit** ("Drop the QML interface and Qt Quick")

### Task 14: Measure and check the real app

**Files:**
- Modify: `tests/bench/editorbench.cpp`

- [ ] **Step 1: Extend the bench to a whole `MainWindow`**: open time, key-to-frame time at the top and in the middle, scroll steps, search keys and theme switch. Report as the scratchpad bench did.
- [ ] **Step 2: Run it on a copy of the 5.4 MB file.** Expected:
  - a key at the top ≤ 17 ms to its frame;
  - opening ≤ 1.2 s;
  - `footprint` ≤ 248 MB with the file open.

  Record the numbers in the commit message.
- [ ] **Step 3: Check by hand in `build/Write.app`**, against the spec's lists:
  - **Sheets:** ⌘D and ⌘. in the unsaved-changes sheet; Esc in the file-changed sheet.
  - **Fonts and scrolling:** the Fonts panel; ⌘-scroll; trackpad momentum.
  - **Editing:** a link tooltip; find and replace with 350,000 matches; composing `é` and Japanese input.
  - **Window:** full screen; the proxy icon and the edited dot.
  - **App:** ⌘Q with two unsaved windows.

  Note anything that fails and fix it before the commit, with a test where one can be written.
- [ ] **Step 4: Run `bin/test`.** Expected: all PASS.
- [ ] **Step 5: Commit** ("Measure the widget interface on the 5.4 MB file")
