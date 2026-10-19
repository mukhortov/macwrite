# Write

A macOS port of [Omawrite](https://github.com/omacom/omawrite), the dead-simple
Markdown writing app built with Qt Quick and C++ for Omarchy Linux. Write keeps
Omawrite's editing and rewrites its interface with Qt Widgets, so it runs as a
native Mac app: the menu bar, sheets and find bar are the Mac's own, and it
follows the system's light and dark appearance.

Omawrite itself is Linux-only and doesn't take macOS support, so report anything
about Write here rather than upstream.

## Download

Download `Write-VERSION.dmg` from
[Releases](https://github.com/mukhortov/write/releases), open it, and drag
Write to Applications. It needs macOS 14.4 or later and runs natively on Intel
and Apple silicon Macs. It is signed with a Developer ID and notarized by
Apple, so it opens like any other downloaded app.

## Build and install

```sh
brew install qt
bin/install
```

`bin/install` builds `build/Write.app` and copies it to `/Applications`; set
`APPLICATIONS` to install it somewhere else. `bin/build` only builds it, and
`bin/test` runs the tests. The app loads Qt from Homebrew, so keep `qt` installed.

## Making a disk image

```sh
uvx --from aqtinstall aqt install-qt mac desktop 6.12.0 clang_64 \
  --archives qtbase qtsvg -m qtimageformats -O ~/Qt
bin/dmg
```

`bin/dmg` builds Write afresh in `build-dmg` against Qt's own macOS binaries,
not Homebrew's, copies Qt into the app, and makes
`build-dmg/Write-VERSION.dmg` with the version from `write.pro`. The first
command puts those binaries in `~/Qt/6.12.0/macos`, using
[aqtinstall](https://github.com/miurahr/aqtinstall); Qt's online installer
works too, and `WRITE_QT` points `bin/dmg` at another install. Homebrew builds
Qt for the macOS it runs on, so an app made from it needs that macOS or a later
one, while Qt's own binaries run on Qt's minimum, macOS 14.4 for Qt 6.12, on
Intel and Apple silicon alike; see `macos/deployment-target.pri`. `bin/dmg`
stops if anything in the app would load from outside it or need a newer macOS
than the app says it does.

The app is signed ad hoc, so macOS blocks it the first time it opens from a
download. Allow it with Open Anyway in System Settings › Privacy & Security.
To sign with a Developer ID instead, set `WRITE_SIGNING_IDENTITY` to the
identity's name, or to its SHA-1 hash from `security find-identity -v -p
codesigning` when two certificates share a name. The app then runs with the
hardened runtime, and the disk image is signed too. Set `WRITE_NOTARY_PROFILE`
too, to a keychain profile from `xcrun notarytool store-credentials`, to
notarize the app and the disk image and staple a ticket to each.

## Opening files

Open Markdown files from Finder with Open With → Write, or from Terminal with
`open -a Write notes.md`. An untouched blank window takes the file, a file
that is already open comes forward in its window, and any other file gets a
window of its own. Every window runs in the one app, behind one Dock icon.

Closing the last window leaves Write running, as Mac apps do; click its Dock
icon for a new window.

A new window opens a step down and to the right of the front window. The last
window to close sets where the first window opens at the next launch.

`Write.app/Contents/MacOS/Write FILE` opens a file too, even one that does
not exist yet, which the first save creates. `--help` and `--version` answer in
the terminal without opening a window.

## Shortcuts

Every command is also in the menu bar, under File, Edit, View, and Window.

- `⌘S` saves. Untitled documents ask where to save.
- `⇧⌘S` saves as.
- `⌘O` opens a Markdown file.
- `⌘P` opens the print dialog.
- `⌘N` opens a new window and `⌘W` closes it.
- `⌘Z`, `⇧⌘Z`, and `⌘Y` handle undo and redo.
- `⌃⌘F` toggles fullscreen, and `⌘M` minimizes the window.
- `⌘F` searches the document, where a plain letter also finds its accented
  forms. Use `Return` or `⌘G` for the next match and `⇧Return` for the previous
  match; `Esc` closes the find bar.
- `⌥⌘F` opens find and replace.
- `⌘B`, `⌘I`, and `⇧⌘X` toggle bold, italic, and strikethrough Markdown. `⌘K` inserts a link.
- `Tab` and `⇧Tab` nest list items under the item above and lift them back out.
- `⌘-click` follows a link.
- `⌘=` and `⌘-` change the text size, `⌘0` returns to the default.
  `⌘-scroll` does the same.
- `⇧⌘C` copies the rendered document as HTML, with plain text as a fallback.
- In the unsaved-changes sheet, `⌘.` or `Esc` cancels, `⌘D` discards, and
  `Return` saves. In the sheet about a file changed outside Write, `Esc` keeps
  your text.

`⌘Q` asks about unsaved changes in every window before quitting, the front
window first. Cancelling any of those questions cancels the quit, and no window
closes until every question is answered.

Once a document has been saved to a file, edits are auto-saved to that file a
moment after you stop typing, so `⌘S` is rarely needed. Untitled drafts are
never auto-saved — they wait for an explicit save so the app never picks a file
name for you. Unsaved drafts are recovered after an abnormal exit, each in a
window of its own. Write also
watches open files and warns before an external change can replace local work;
auto-save pauses until you reload, keep your version, or save. A recovered draft
is never auto-saved over its file — save it to keep it.

## Writing

Write hides the Markdown markers around bold, italic, strikethrough and links,
and shows them again wherever the caret is, so you can see and change them.
A link's destination shows while the caret is in the link.

`⌘-click` on a link follows it. A link to `#a-heading` goes to that heading in
the document, whether the anchor is written GitHub's way or keeps the heading's
punctuation. A web link opens in your browser, and a relative link opens the
file it names, starting from the folder the document is saved in. Hold `⌘` over
a link and the pointer turns into a hand; the link's tooltip shows where it goes.

View › Font picks the writing font from IBM Plex Mono, the default, which comes
with Write, and Menlo, Monaco, PT Mono and SF Mono, which come with macOS.
Write remembers the choice.

The text size runs from 50% to 300% in browser-sized steps. Like the font, it is
one setting for every window, and is remembered for the next launch.

## License

MIT, like Omawrite, which is copyright David Heinemeier Hansson; see `LICENSE`.

The IBM Plex Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright IBM Corp.
