# Write's interface in Qt Widgets

Date: 2026-10-19
Branch: `peter/widgets-ui`, from `peter/macos-port`

## Goal

Write's interface is QML: `Main.qml`, six more `.qml` files and
`EditorMutations.js`, about 2,200 lines run by Qt Quick, QtQml and its
JavaScript engine. A standalone DMG has to carry all of that, about 25 MB of
the app on top of what any Qt app needs.

Rewrite the interface in C++ with Qt Widgets, with native macOS controls, so
the app no longer needs Qt Quick, QtQml, QML or JavaScript.

### Success criteria

1. The build links no Qt Quick or QtQml, and the app holds no `.qml` or `.js`.
2. `bin/test` passes.
3. No regression on `/Users/peter/Personal/5.4mb-test-file.md` (measured on a
   copy):
   - a key typed at the top of the document reaches the screen within about
     one frame;
   - opening takes about 1.2 s or less;
   - memory is at most about 248 MB.
4. Every feature listed below works in the real app.

## Decisions

| Question | Choice |
|---|---|
| Look | Native macOS controls, not a rebuild of today's Material look. |
| Editor widget | `QTextEdit` with `PlainTextLayout`, keeping 140% line spacing, pixel scrolling and the large-file speed. |
| Structure | One `MainWindow` per document with focused widgets; `Backend` stays the per-document model, with its QML-only interface made plain C++. |

## What carries over unchanged

### Writing area

- Centred column, as wide as the smallest of: 65 average characters of the
  editor font; the window less 20 average characters, but no less than
  360 px; the window less 48 px.
- Editor font from `backend.editorFont`, at 20 px times the text scale, with
  shaping off for fixed-pitch fonts.
- 140% proportional line height, from `Backend`'s typography pass.
- Space above the first line of `max(42, 5% of the window height)` px, and
  220 px below the last, so the end of the text can scroll up.
- Placeholder `# Start writing` in the muted colour (`#909191` dark,
  `#aeb1b5` light), shown while the document is empty and the editor is not
  focused.
- The caret stays at least two lines (2 × font size) inside the viewport
  while typing or moving.
- ⌘+scroll changes the text size: a wheel notch is one step, and touchpad
  pixels are banked until they add up to one (50 px per step).
- Hovering a link shows its destination as plain text in a tooltip, elided in
  the middle to fit the window.
- The caret shows as soon as a file opened from the Open dialog loads.

### Editing behaviour

All of today's rules, with today's tests as the reference:

- Return continues `-`, `+`, `*`, numbered (`1.` / `1)`, counting up) and
  `>` lines, ends the list on an empty item, inserts a plain break inside a
  fenced code block, and otherwise starts a paragraph with a blank line
  (a single break when the line or what is left of it is blank).
  Shift+Return is a single break.
- Backspace right after a paragraph break removes both breaks, under the
  same conditions as `deleteParagraphBreakBehindCursor` today.
- Left and Right step over hidden Markdown markers (`hiddenRangesAt`,
  `cursorPositionStep`); with a selection they collapse it to its edge.
- Page Up and Page Down move the caret by the viewport less two lines, with
  Shift extending the selection.
- Pasting a URL over selected text makes a Markdown link of that text,
  keeping surrounding spaces outside it; any other paste is plain text, with
  line endings normalised.
- Bold (⌘B), Italic (⌘I) and Strikethrough (⇧⌘X) toggle `**`, `*` and `~~`,
  with today's rules for whole marker runs, `***`, and spans around the
  caret.
- Link (⌘K) wraps the selection, or `link text`, as `[label](url)`, taking
  the URL from the clipboard when there is one and selecting what is left to
  type.
- Each edit is one undo step.

### Find and replace

- ⌘F opens the find bar, ⌥⌘F opens it with the replace row.
- Matches are case-insensitive. The count reads `N of M`.
- Typing in the field selects the first match. Return and ⌘G go to the next
  match, ⇧Return to the previous one. Esc closes the bar and returns to the
  editor, clearing the selection.
- Matches within the visible area, up to 2,000, are highlighted:
  `#725b18` / `#ffe58a` (dark / light), and the current one
  `#b36b20` / `#ffad42`.
- Edits to the document update the matches without moving the caret.
- Replace replaces the current match and carries on past the replacement;
  All replaces every match as one undo step.
- While the find or replace field has focus, the Edit menu's commands act on
  that field.

### Files, windows and the app

- File menu: New Window ⌘N, Open… ⌘O, Save ⌘S, Save As… ⇧⌘S, Print… ⌘P,
  Close Window ⌘W.
- Edit menu: Undo ⌘Z, Redo ⇧⌘Z (and ⌘Y), Cut, Copy, Paste, Select All,
  Copy as HTML ⇧⌘C, Find ⌘F, Find and Replace ⌥⌘F, Bold, Italic,
  Strikethrough, Link.
- View menu: Bigger ⌘= (and ⌘+), Smaller ⌘-, Actual Size ⌘0, Font…, Enter /
  Exit Full Screen ⌃⌘F.
- Window menu: Minimize ⌘M, Zoom.
- The no-window menu bar keeps New Window and Open.
- All windows run in one process; a new window cascades from the front one;
  the last window to close sets where the next launch opens; ⌘Q asks each
  window with unsaved changes, front first, and stops at Cancel.
- Opening a file that is already open brings its window forward; an
  untouched blank window takes the file.
- Auto-save, crash-recovery drafts, the file watcher, the word count and
  Markdown highlighting are `Backend`'s and do not change.
- Colours follow the system's dark or light mode, from `Backend`'s theme.

## What changes in behaviour

| Today | Widgets version |
|---|---|
| Unsaved-changes dialog inside the window | A sheet from the title bar: "Do you want to save the changes you made to “name”?", with "Your changes will be lost if you don't save them." Buttons Save (Return), Don't Save (⌘D), Cancel (Esc, ⌘.). |
| File-changed dialog inside the window | A sheet titled "File changed" or "File removed", with today's explanations. Buttons Keep Mine and Reload. Esc keeps the writer's text. |
| Rounded find pane over the text | A find bar across the top of the window, as in Safari and TextEdit. |
| Footer with Save, Open and Font icons, status and word count | A slim status line: status on the left, word count on the right. The icons go; their commands stay in the menus. |
| Font picker popup | View › Font… opens the macOS Fonts panel. Write takes only the family; size stays with ⌘= and ⌘-. The panel lists all fonts, not only Latin ones. |
| Keyboard Shortcuts dialog and the Help menu | Removed. The menus show every shortcut. |
| Chromium-style animated wheel scrolling | Native scrolling: trackpad momentum, standard mouse wheel steps. |
| Title `* name - Write` | Title is the file name, with the document's icon. Unsaved changes show as the dot in the close button. |
| Interface font IBM Plex Mono, scaled with the text | Native controls use the system font at its normal size. The writing font and its size are unchanged. |

## Architecture

### New files

**`mainwindow.{h,cpp}`**: a `QMainWindow` per document.

- Builds the menus. Each command is enabled only while no sheet or the
  Fonts panel is up.
- Lays out the find bar above the editor and the status line below it.
- Owns closing. `closeEvent` with unsaved changes opens the sheet and
  ignores the close. The pending close or open, and the rule that only a
  save chosen in the sheet completes it, move here from `Main.qml`
  (`pendingAction`, `awaitingPendingSave`, `completePendingAction`,
  `cancelPendingAction`). `Backend::holdAutoSave` is on while the sheet is
  up.
- Emits `closeCancelled()` for `WindowManager`'s quit.
- Shows Open as a panel (`QFileDialog`, application-modal) and Save As as a
  sheet; cancelling either cancels a pending close or open.
- Handles full screen, Minimize, Zoom, and the window's first geometry from
  `Backend::windowGeometry()` or the cascade.
- Connects `Backend`'s signals (`openDialogRequested`,
  `saveDialogRequested`, `saveSucceeded`, `saveFailed`,
  `externalChangeDetected`, theme, font and text-scale changes).

**`markdowneditor.{h,cpp}`**: a `QTextEdit` subclass.

- Plain text only (`setAcceptRichText(false)`), word wrap at the column.
- Installs `PlainTextLayout` on its document and hands the document to
  `Backend::attachDocument`.
- Places the column with viewport margins and the document margin, from
  the width formula above; recomputes on resize, font and scale changes.
- `keyPressEvent` handles Return, Backspace, Left, Right, Page Up and Page
  Down through `markdownedits`.
- `insertFromMimeData` handles every paste and drop.
- `paintEvent` paints the placeholder.
- `mouseMoveEvent` shows the link tooltip.
- `wheelEvent` with ⌘ changes the text size; otherwise `QTextEdit`'s.
- Keeps the caret two lines inside the viewport.
- Shows search matches as `QTextEdit` extra selections.

**`markdownedits.{h,cpp}`**: the editing rules as free functions on a
`QTextDocument` and positions, independent of any widget: `replaceRange`
(one undo step, then a caret or selection inside the replacement),
`toggleWrap`, `insertLink`, `markdownLink` with its escaping,
`smartReturn`, `deleteParagraphBreakBehindCursor`, `pasteText`. They are
`EditorMutations.js` and the editor functions of `Main.qml` in C++.

**`findbar.{h,cpp}`**: the find bar widget (search field, count, previous
and next, Done, and the replace row with Replace and All), and
`SearchMatches`, a small class with no interface: the match list, the
current index, the binary search for the first match at a position, the
matches inside a range of positions, and following edits.

### Changed files

**`plaintextlayout.{h,cpp}`**: `draw()` draws everything `QTextEdit`
expects of a layout: text in the palette's colour, the selection, extra
selections, and the caret at `PaintContext::cursorPosition`, as
`QTextDocumentLayout` does. A test draws the same document through both
layouts and requires the same pixels.

**`backend.{h,cpp}`**:

- `attachDocument(QTextDocument *)`. The Qt Quick viewport flag goes.
- `hiddenRangesAt` returns `QList<Range>`, `emphasisSpanAt` returns
  `std::optional<EmphasisSpan>`, `windowGeometry` returns a
  `WindowGeometry` struct.
- `Q_INVOKABLE` and properties used only by QML go; the notify signals
  stay, as `MainWindow` listens to them.
- Everything else is unchanged.

**`windowmanager.{h,cpp}`**: creates a `Backend` and a `MainWindow` per
window instead of loading `Main.qml`; tracks activation and closing through
widget events. Its behaviour is unchanged.

**`main.cpp`**: no QML engine, no Quick style, no interface font.

**`write.pro`, `tests/tests.pro`**: `QT = core gui widgets printsupport
concurrent`.

**`resources.qrc`**: the IBM Plex Mono fonts only.

### Removed

`Main.qml`, `FontPicker.qml`, `UnsavedChangesDialog.qml`,
`ExternalChangeDialog.qml`, `FooterIconButton.qml`, `SearchIconButton.qml`,
`SquareDialogButton.qml`, `EditorMutations.js`.

## Error handling

As today: `Backend` reports file errors in the status line, and a failed
save cancels the close or open it was for. Cancelling a file dialog or a
sheet cancels any pending close or open, and a cancelled close calls off a
waiting quit.

## Testing

- Unchanged: the `PlainTextLayout` comparison tests, the `Backend` tests
  (saving, drafts and recovery, file watching, word count, digests), the
  command-line tests.
- Adapted: the `WindowManager` tests, to `MainWindow`.
- Ported with the same checks, driving widgets with `QTest`: every test that
  drives today's QML editor, menus or dialogs: smart Return, Backspace,
  emphasis toggles, links and paste, search and replace, the unsaved-changes
  and file-changed flows, ⌘W and ⌘Q, and the caret after opening a file.
- New:
  - `markdownedits` unit tests;
  - `SearchMatches` unit tests;
  - a drawing test: the same document, with a caret, a selection and extra
    selections, drawn through `QTextDocumentLayout` and `PlainTextLayout`
    into images, which must be identical.
- Dropped: the font picker, footer button, shortcuts dialog and QML binding
  loop tests.
- Build check: `otool -L` on the app binary lists no QtQuick or QtQml.
- Performance: the scratchpad benchmark, ported to `MainWindow`, run on a
  copy of the 5.4 MB file for the criteria above.

## Risks

1. **`QTextEdit` with a custom layout.** `QTextEdit` was written around
   `QTextDocumentLayout`. The first task is a spike: `MarkdownEditor` with
   `PlainTextLayout` on the 5.4 MB file, checking typing, selection, the
   caret, input methods and speed, before anything else is built. If it
   fails, the fallback is `QPlainTextEdit`, which loses the 140% line
   spacing; that needs the writer's go-ahead.
2. **Upstream fixes.** Omawrite's interface fixes will no longer merge, as
   `Main.qml` is gone; they would be ported by hand. `Backend` and
   highlighter fixes still merge.
3. **The Fonts panel.** If Qt's native font dialog misbehaves, the fallback
   is Qt's own font dialog, limited to choosing a family.

## Out of scope

The DMG release script: official Qt binaries, bundling, trimming unused
parts, signing and notarizing. The final download size is measured there.
