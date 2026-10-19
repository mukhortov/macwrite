# Write

A macOS port of [Omawrite](https://github.com/omacom/omawrite), the dead-simple
Markdown writing app built with Qt Quick and C++ for Omarchy Linux. Write runs
as a native Mac app and follows the system's light and dark appearance.

Omawrite itself is Linux-only and doesn't take macOS support, so report anything
about Write here rather than upstream.

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
bin/dmg
```

`bin/dmg` builds Write afresh in `build-dmg`, copies Qt and the other Homebrew
libraries it loads into the app, and makes `build-dmg/Write-VERSION.dmg` with
the version from `write.pro`. That app runs without Homebrew, but it needs the
macOS release it was built on or a later one, because Homebrew builds Qt for
the macOS it runs on; see `macos/deployment-target.pri`.

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

`Write.app/Contents/MacOS/Write FILE` opens a file too, even one that does
not exist yet, which the first save creates. `--help` and `--version` answer in
the terminal without opening a window.

## Shortcuts

Every command is also in the menu bar, under File, Edit, View, Window, and Help.

- `⌘S` saves. Untitled documents ask where to save.
- `⇧⌘S` saves as.
- `⌘O` opens a Markdown file.
- `⌘P` opens the print dialog.
- `⌘N` opens a new window and `⌘W` closes it.
- `⌘Z`, `⇧⌘Z`, and `⌘Y` handle undo and redo.
- `⌃⌘F` toggles fullscreen.
- `⌘F` searches the document. Use `Return` or `⌘G` for the next match and `⇧Return` for the previous match.
- `⌥⌘F` opens find and replace.
- `⌘B`, `⌘I`, and `⇧⌘X` toggle bold, italic, and strikethrough Markdown. `⌘K` inserts a link.
- `⌘=` and `⌘-` change the text size, `⌘0` returns to the default.
  `⌘-scroll` does the same.
- `⇧⌘C` copies the rendered document as HTML, with plain text as a fallback.
- `⌘?` shows the keyboard shortcut reference.
- In the unsaved-changes dialog, `⌘.` or `Esc` cancels, `⌘D` discards, and
  `Return` saves.

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

The font button in the footer picks the writing font from any installed text font,
and Write remembers the choice. IBM Plex Mono is the default.

The text size runs from 50% to 300% in browser-sized steps. Like the font, it is
one setting for every window, and is remembered for the next launch.

A new window opens a step down and to the right of the front window. The last
window to close sets where the first window opens at the next launch.

## License

MIT, like Omawrite, which is copyright David Heinemeier Hansson; see `LICENSE`.

The IBM Plex Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright IBM Corp.
