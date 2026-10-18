#include "backend.h"


#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QtConcurrentRun>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QMimeData>
#include <QPrintDialog>
#include <QPrinter>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QUrl>
#include <QWindow>

#include <algorithm>
#include <optional>
#include <iterator>

#include "markdownhighlighter.h"
#include "plaintextlayout.h"

constexpr qreal typoraLineHeightPercent = 140;

static QTextBlockFormat lineHeightFormat() {
    QTextBlockFormat format;
    format.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);
    return format;
}
const QString lastSaveDirectorySetting = QStringLiteral("file/lastSaveDirectory");
const QString textZoomSetting = QStringLiteral("text/zoom");

// The user's own text size, multiplied onto the desktop's text scale.
// Chromium's zoom ladder, trimmed to the range the whole window still reads
// well at, so Ctrl+wheel here steps like Ctrl+wheel in a browser.
constexpr qreal textZoomSteps[] = {0.67, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0};
constexpr int textZoomStepCount = int(std::size(textZoomSteps));

const QString editorFontSetting = QStringLiteral("editor/font");

namespace {

// A fingerprint for telling whether two texts are the same without keeping
// either; SHA-256 makes a false match out of reach.
QByteArray bytesDigest(QByteArrayView bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QByteArray textDigest(const QString &text) {
    return bytesDigest(QByteArrayView(reinterpret_cast<const char *>(text.constData()),
                                      text.size() * qsizetype(sizeof(QChar))));
}

// A shade of `from` carried part of the way towards `to`.
QColor blend(const QColor &from, const QColor &to, qreal amount) {
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount,
                            from.greenF() + (to.greenF() - from.greenF()) * amount,
                            from.blueF() + (to.blueF() - from.blueF()) * amount);
}

}

QString Backend::normalizedLinkUrl(const QString &clipboardText) {
    QString candidate = clipboardText.trimmed();
    static const QRegularExpression lineBreakRe(QStringLiteral("[\\r\\n]"));
    const int lineBreak = candidate.indexOf(lineBreakRe);
    if (lineBreak >= 0)
        candidate = candidate.left(lineBreak).trimmed();

    if (candidate.isEmpty())
        return {};

    if (candidate.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
        candidate.prepend(QStringLiteral("https://"));

    static const QRegularExpression schemeRe(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    if (!schemeRe.match(candidate).hasMatch())
        return {};

    const QUrl url(candidate);
    if (!url.isValid() || url.scheme().isEmpty())
        return {};

    const QString scheme = url.scheme().toLower();
    const bool webUrl = scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("ftp");
    if (webUrl && url.host().isEmpty())
        return {};

    if (!webUrl && scheme != QStringLiteral("mailto"))
        return {};

    return url.toString();
}

// Each window keeps its draft in a numbered slot, held by a lock while the
// window is open.
static QString recoverySlotBase(int slot) {
    const QString stateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(stateDirectory).filePath(QStringLiteral("recovery-%1").arg(slot));
}

constexpr int recoverySlotCount = 100;

Backend::Backend(QObject *parent) : QObject(parent) {
    m_textZoom = storedTextScale();
    // An untitled document starts out matching its empty baseline.
    setKnownFileContents(QByteArray(), false);

    const QString savedFont = QSettings().value(editorFontSetting).toString();
    m_editorFont = editorFonts().contains(savedFont) ? savedFont : defaultEditorFont();

    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    // Claim an orphaned snapshot before taking an empty slot. This ensures a
    // crash in window 2 is still recovered even if window 1 exited normally.
    for (int pass = 0; pass < 2 && !m_recoveryLock; ++pass) {
        for (int slot = 0; slot < recoverySlotCount; ++slot) {
            const QString base = recoverySlotBase(slot);
            const bool snapshotExists = QFileInfo::exists(base + QStringLiteral(".json"));
            if ((pass == 0) != snapshotExists)
                continue;
            auto lock = std::make_unique<QLockFile>(base + QStringLiteral(".lock"));
            if (lock->tryLock()) {
                m_recoveryPath = base + QStringLiteral(".json");
                m_recoveryLock = std::move(lock);
                break;
            }
        }
    }
    m_wordCountTimer.setSingleShot(true);
    m_wordCountTimer.setInterval(250);
    connect(&m_wordCountTimer, &QTimer::timeout, this, &Backend::refreshWordCount);
    connect(&m_wordCountWatcher, &QFutureWatcher<int>::finished, this, [this]() {
        // Edits made while counting are counted again rather than shown late.
        if (m_wordCountStale) {
            m_wordCountStale = false;
            refreshWordCount();
            return;
        }
        setWordCount(m_wordCountWatcher.result());
    });
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(750);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &Backend::writeRecovery);
    m_autoSaveTimer.setSingleShot(true);
    m_autoSaveTimer.setInterval(500);
    connect(&m_autoSaveTimer, &QTimer::timeout, this, &Backend::autoSave);
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
                if (path != m_fileUrl.toLocalFile())
                    return;

                // Atomic saves replace the watched inode, which drops it from
                // the watcher, so re-arm it either way. Our own save is not an
                // outside edit.
                watchCurrentFile();
                if (!diskMatchesKnownContents())
                    reportExternalChange(!QFileInfo::exists(path));
            });

    applyTheme();
}

Backend::~Backend() = default;

// A settings file truncated by a crash reads back as an empty value, and both
// that and a NaN would clamp to the smallest size rather than fall back to the
// default.
qreal Backend::storedTextScale() {
    bool zoomOk = false;
    const qreal storedZoom = QSettings().value(textZoomSetting, 1.0).toDouble(&zoomOk);
    return zoomOk && storedZoom > 0 ? qBound(textZoomSteps[0], storedZoom,
                                             textZoomSteps[textZoomStepCount - 1])
                                    : 1.0;
}

int Backend::unclaimedRecoveryDrafts() {
    int drafts = 0;
    for (int slot = 0; slot < recoverySlotCount; ++slot) {
        const QString base = recoverySlotBase(slot);
        if (!QFileInfo::exists(base + QStringLiteral(".json")))
            continue;
        QLockFile lock(base + QStringLiteral(".lock"));
        if (lock.tryLock())
            ++drafts;
    }
    return drafts;
}

void Backend::setParentWindow(QWindow *window) {
    m_parentWindow = window;
}

QString Backend::fileName() const {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty())
        return QStringLiteral("Untitled.md");

    if (m_fileUrl.isLocalFile()) {
        const QFileInfo info(m_fileUrl.toLocalFile());
        if (!info.fileName().isEmpty())
            return info.fileName();
    }

    const QString name = m_fileUrl.fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    applyTheme();
    emit darkModeChanged();
}

void Backend::increaseTextSize() {
    setTextZoom(steppedTextZoom(1));
}

void Backend::decreaseTextSize() {
    setTextZoom(steppedTextZoom(-1));
}

void Backend::resetTextSize() {
    setTextZoom(1.0);
}

// The neighbouring rung, starting from whichever rung the current zoom sits
// closest to; the ends of the ladder hold.
qreal Backend::steppedTextZoom(int direction) const {
    int closest = 0;
    for (int step = 1; step < textZoomStepCount; ++step) {
        if (qAbs(textZoomSteps[step] - m_textZoom) < qAbs(textZoomSteps[closest] - m_textZoom))
            closest = step;
    }

    return textZoomSteps[qBound(0, closest + direction, textZoomStepCount - 1)];
}

void Backend::setTextZoom(qreal zoom) {
    // Report the size even when the ladder has run out, so a keypress that
    // changes nothing still says why.
    setStatus(QStringLiteral("Text size %1%").arg(qRound(zoom * 100)));
    setTextScale(zoom);
}

void Backend::setTextScale(qreal scale) {
    if (qFuzzyCompare(m_textZoom, scale))
        return;

    m_textZoom = scale;
    QSettings().setValue(textZoomSetting, scale);
    emit textScaleChanged();
}

void Backend::attachDocument(QTextDocument *document) {
    if (!document) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    if (m_highlighter)
        delete m_highlighter.data();

    m_document = document;
    // Qt's own layout moves every line below an edit, which in a long
    // document is most of the time a keystroke takes.
    if (!qobject_cast<PlainTextLayout *>(m_document->documentLayout()))
        m_document->setDocumentLayout(new PlainTextLayout(m_document));
    // Taken now, as the highlighter's first pass reports a change before any
    // edit, and must not be mistaken for one that saves or drops a draft.
    takeEditState();
    connect(m_document, &QTextDocument::undoCommandAdded, this,
            [this]() { m_editState.commandAdded = true; });
    m_highlighter = new MarkdownHighlighter(m_document);
    m_highlighter->setTheme(m_darkMode, m_themeBackground, m_themeForeground, m_themeAccent,
                            m_themeCodeBackground);
    m_highlighter->setCaret(m_caretPosition);

    connect(m_document, &QTextDocument::contentsChange, this,
            [this](int position, int, int charsAdded) {
                if (m_formattingTypography || m_loading)
                    return;
                m_lastChangePos = position;
                m_lastChangeAdded = charsAdded;
            });

    applyDocumentTypography();
    restoreRecovery();
}

void Backend::openDialog() {
    emit openDialogRequested();
}

void Backend::open(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be opened."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("Could not open %1.").arg(targetName));
        return;
    }

    const QByteArray raw = file.readAll();
    // Text mode drops carriage returns, matching how the file is compared
    // and saved later.
    file.setTextModeEnabled(true);
    file.seek(0);
    const QByteArray contents = file.readAll();
    loadDocumentText(QString::fromUtf8(contents));
    clearRecovery();
    setKnownFileContents(contents, true);
    m_autoSaveSuspended = false;
    setFileUrl(url);
    watchCurrentFile();
    setModified(false);
    if (savesUnchanged(raw)) {
        setStatus(QStringLiteral("Opened %1").arg(fileName()));
    } else {
        // Saving writes UTF-8 with plain line endings, which would change
        // bytes this file holds. Leave that to an explicit save.
        suspendAutoSave();
        setStatus(QStringLiteral("Opened %1. Auto-save is off to keep its encoding and line endings.")
                      .arg(fileName()));
    }
}

void Backend::save() {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty()) {
        saveAsDialog();
        return;
    }

    saveTo(m_fileUrl);
}

void Backend::saveAsDialog() {
    emit saveDialogRequested(suggestedSaveUrl());
}

void Backend::saveAs(const QUrl &url) {
    saveTo(url);
}

void Backend::discardRecovery() {
    clearRecovery();
    // Discarded changes must never reach the file, even if the open or close
    // that follows fails and leaves them in the editor. Opening a file or an
    // explicit save lifts this.
    suspendAutoSave();
}

// A file named on the command line need not exist yet: `Write new.md`
// starts a blank document that the first save writes to that path. Opening
// from the file picker or reloading still requires the file to be there.
void Backend::openOrStartFile(const QUrl &requestedUrl) {
    // A relative path names a file in the directory Write was started
    // from; a recovery draft must not resolve it against another one later.
    const QFileInfo info(requestedUrl.toLocalFile());
    const QUrl url = requestedUrl.isLocalFile()
        ? QUrl::fromLocalFile(info.absoluteFilePath()) : requestedUrl;
    if (m_modified) {
        // A recovered draft fills this window; opening over it would lose it.
        setStatus(QStringLiteral("Recovered unsaved changes; %1 was not opened.")
                      .arg(info.fileName()));
        return;
    }
    if (!url.isLocalFile() || info.exists() || !info.absoluteDir().exists()) {
        open(url);
        return;
    }

    loadDocumentText(QString());
    clearRecovery();
    // Nothing has been read, so no text counts as saved, and auto-save holds
    // off until an explicit save creates the file (see autoSave()).
    setKnownFileContents(QByteArray(), false);
    setFileUrl(url);
    watchCurrentFile();
    setModified(false);
    setStatus(QStringLiteral("New file %1").arg(fileName()));
}

void Backend::reloadFromDisk() {
    if (m_fileUrl.isLocalFile())
        open(m_fileUrl);
}

void Backend::keepExternalVersion() {
    QFile file(m_fileUrl.toLocalFile());
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray raw = file.readAll();
        file.setTextModeEnabled(true);
        file.seek(0);
        setKnownFileContents(file.readAll(), true);
        // Keeping our text over an edited file is a deliberate choice to
        // overwrite it, so auto-save may resume, unless saving would change
        // the file's encoding or line endings. A removed file stays removed
        // until the writer saves it again.
        m_autoSaveSuspended = !savesUnchanged(raw);
    } else {
        setKnownFileContents(QByteArray(), false);
    }
    setModified(true);
    scheduleRecovery();
    scheduleAutoSave();
    watchCurrentFile();
    setStatus(QStringLiteral("Kept your version"));
}

QFont Backend::printFont(const QFont &editorFont, qreal screenDpi) {
    // The editor sizes its font in pixels, which a printer takes as device dots.
    // Convert to points at the screen's DPI so the page matches the editor.
    QFont font = editorFont;
    if (font.pixelSize() <= 0)
        return font;

    const qreal dpi = screenDpi > 0.0 ? screenDpi : 96.0;
    font.setPointSizeF(font.pixelSize() * 72.0 / dpi);
    return font;
}

void Backend::printDocument() {
    if (!m_document) {
        setStatus(QStringLiteral("There is no document to print."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(QStringLiteral("Print %1").arg(fileName()));
    dialog.winId();
    if (dialog.windowHandle() && m_parentWindow)
        dialog.windowHandle()->setTransientParent(m_parentWindow);

    if (dialog.exec() == QDialog::Accepted) {
        QTextDocument rendered;
        const QScreen *screen = m_parentWindow ? m_parentWindow->screen()
                                               : QGuiApplication::primaryScreen();
        rendered.setDefaultFont(printFont(m_document->defaultFont(),
                                          screen ? screen->logicalDotsPerInchY() : 0.0));
        rendered.setMarkdown(currentDocumentText());
        rendered.print(&printer);
    }
}

// The window manager opens it, as windows belong to the whole app.
void Backend::newWindow() {
    emit newWindowRequested();
}

bool Backend::isUntouched() const {
    return m_fileUrl.isEmpty() && !m_modified && currentDocumentText().isEmpty();
}

void Backend::copyAsHtml() {
    QTextDocument rendered;
    rendered.setMarkdown(currentDocumentText());

    // Drop Qt's fixed fonts and margins from the body and blocks, so the pasted
    // text takes on the destination's own typeface and paragraph spacing.
    QString html = rendered.toHtml();
    static const QRegularExpression blockStyle(
        QStringLiteral("<(body|p|h[1-6]|ul|ol|li)\\s+style=\"[^\"]*\""));
    html.replace(blockStyle, QStringLiteral("<\\1"));

    auto *mimeData = new QMimeData;
    mimeData->setHtml(html);
    mimeData->setText(rendered.toPlainText());
    QGuiApplication::clipboard()->setMimeData(mimeData);
    setStatus(QStringLiteral("Copied as HTML."));
}

QString Backend::clipboardUrl() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard ? linkUrlFrom(clipboard->mimeData()) : QString();
}

QString Backend::linkUrlFrom(const QMimeData *mimeData) {
    if (!mimeData)
        return {};

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            const QString normalized = normalizedLinkUrl(url.toString());
            if (!normalized.isEmpty())
                return normalized;
        }
    }

    if (!mimeData->hasText())
        return {};

    return normalizedLinkUrl(mimeData->text());
}

QString Backend::clipboardText() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    return mimeData && mimeData->hasText() ? mimeData->text() : QString();
}

bool Backend::editorTextChanged() {
    if (m_loading || m_formattingTypography || !m_document)
        return false;

    // An empty baseline means a pristine untitled document, but only when there
    // is no file behind it. Once there is one, an unknown baseline is unknown
    // rather than empty, and emptying the editor is a change like any other.
    const bool baselineKnown = m_hasKnownFileContents
        || !m_fileUrl.isValid() || m_fileUrl.isEmpty();

    // A whole new text set at once, the same length and with no undo history,
    // leaves no trace in the edit state; the saved text still tells, as an
    // unmodified document stops matching it. Its length settles that quickly.
    std::optional<bool> matchesBaseline;
    if (!documentEdited()) {
        matchesBaseline = baselineKnown && documentMatchesKnownText();
        if (*matchesBaseline != m_modified)
            return false;
    }

    reapplyTypographyToChange();
    takeEditState();

    scheduleWordCount();

    if (matchesBaseline.value_or(baselineKnown && documentMatchesKnownText())) {
        setModified(false);
        clearRecovery();
        if (m_hasKnownFileContents)
            setStatus(QStringLiteral("Saved %1").arg(fileName()));
        else
            setStatus(QString());
    } else {
        setModified(true);
        setStatus(QStringLiteral("Unsaved"));
        scheduleRecovery();
        scheduleAutoSave();
    }

    return true;
}

void Backend::setCaretPosition(int position) {
    m_caretPosition = position;
    if (m_highlighter)
        m_highlighter->setCaret(position);
}

// The emphasis span of the marker's kind that the caret sits in or against,
// in document positions, so a toggle with nothing selected can act on it.
std::optional<EmphasisSpan> Backend::emphasisSpanAt(int position, const QString &marker) const {
    if (!m_document)
        return {};

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid() || block.userState() >= MarkdownHighlighter::InsideFence
            || block.previous().userState() >= MarkdownHighlighter::InsideFence)
        return {};

    using Kind = MarkdownHighlighter::InlineKind;
    const Kind kind = marker == QStringLiteral("**") ? Kind::Bold
        : marker == QStringLiteral("~~")              ? Kind::Strikethrough
                                                      : Kind::Italic;
    const int offset = position - block.position();
    const QList<MarkdownHighlighter::InlineMarkup> markup =
        MarkdownHighlighter::inlineMarkup(block.text());
    for (const auto &item : markup) {
        const int start = item.markers[0].start;
        const int end = item.markers[1].start + item.markers[1].length;
        if (item.kind != kind || offset < start || offset > end)
            continue;
        // The visible text ends before the chain of nested emphasis closing
        // with it, as the italic "*" inside "***word***".
        const int contentEnd = item.content.start + item.content.length;
        int textEnd = contentEnd;
        for (bool moved = true; moved;) {
            moved = false;
            for (const auto &nested : markup) {
                const auto &closing = nested.markers[1];
                if (closing.start >= item.content.start && closing.length > 0
                        && closing.start + closing.length == textEnd) {
                    textEnd = closing.start;
                    moved = true;
                }
            }
        }
        const int base = block.position();
        return EmphasisSpan{base + start, base + end, base + item.content.start,
                            base + contentEnd, base + textEnd};
    }
    return {};
}

// A heading's text as it reads, without the markers around its markup or
// a link's destination.
static QString shownText(const QString &text) {
    QList<bool> marker(text.size(), false);
    for (const MarkdownHighlighter::InlineMarkup &item : MarkdownHighlighter::inlineMarkup(text)) {
        for (const MarkdownHighlighter::Span &span : item.markers)
            std::fill(marker.begin() + span.start, marker.begin() + span.start + span.length, true);
    }
    QString shown;
    for (int i = 0; i < text.size(); ++i) {
        if (!marker[i] && text[i] != QLatin1Char('`'))
            shown += text[i];
    }
    return shown;
}

// GitHub's anchor for a heading: lower case, spaces as hyphens, and no
// punctuation but hyphens and underscores.
static QString headingSlug(const QString &shown) {
    QString slug;
    for (const QChar c : shown.trimmed().toLower()) {
        if (c.isLetterOrNumber() || c.isMark() || c == QLatin1Char('-') || c == QLatin1Char('_'))
            slug += c;
        else if (c == QLatin1Char(' '))
            slug += QLatin1Char('-');
    }
    return slug;
}

static QString lettersAndDigits(const QString &text) {
    QString kept;
    for (const QChar c : text) {
        if (c.isLetterOrNumber())
            kept += c;
    }
    return kept;
}

// The heading a "#fragment" link names, by GitHub's anchor, numbered from a
// repeat's second; failing that, by the same letters and digits, as tools
// that keep the punctuation write it. -1 when there is none.
int Backend::headingPosition(const QString &fragment) const {
    if (!m_document)
        return -1;
    const QString wanted = QUrl::fromPercentEncoding(fragment.toUtf8()).toLower();
    if (wanted.isEmpty())
        return 0;
    const QString wantedLetters = lettersAndDigits(wanted);

    static const QRegularExpression headingRe(
        QStringLiteral("^#{1,6}[ \\t]+(.*?)(?:[ \\t]+#+)?[ \\t]*$"));
    QHash<QString, int> seen;
    int sameLetters = -1;
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        const QString line = block.text();
        if (!line.startsWith(QLatin1Char('#')) || MarkdownHighlighter::isFencedCode(block))
            continue;
        const QRegularExpressionMatch match = headingRe.match(line);
        if (!match.hasMatch())
            continue;
        const QString shown = shownText(match.captured(1));
        const QString slug = headingSlug(shown);
        const int repeat = seen[slug]++;
        if ((repeat ? slug + QLatin1Char('-') + QString::number(repeat) : slug) == wanted)
            return block.position();
        if (sameLetters < 0 && !wantedLetters.isEmpty() && lettersAndDigits(shown.toLower()) == wantedLetters)
            sameLetters = block.position();
    }
    return sameLetters;
}

// A link leads to a heading here, which the caller shows, or to a page or a
// file beside the document, which the system opens.
int Backend::followLink(const QString &destination) {
    if (destination.startsWith(QLatin1Char('#'))) {
        const int position = headingPosition(destination.mid(1));
        if (position < 0)
            setStatus(QStringLiteral("No heading %1 in this document").arg(destination));
        return position;
    }
    QUrl url(destination);
    if (url.isRelative()) {
        if (!m_fileUrl.isValid()) {
            setStatus(QStringLiteral("Save the document to follow links beside it"));
            return -1;
        }
        url = m_fileUrl.resolved(url);
    }
    QDesktopServices::openUrl(url);
    return -1;
}

QString Backend::linkUrlAt(int position) const {
    if (!m_document)
        return {};

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return {};

    // Fenced code is literal, so nothing in it is a link.
    if (block.userState() >= MarkdownHighlighter::InsideFence
            || block.previous().userState() >= MarkdownHighlighter::InsideFence)
        return {};

    return MarkdownHighlighter::linkUrlAt(block.text(), position - block.position());
}

int Backend::cursorPositionStep(int position, int direction) const
{
    if (!m_document || direction == 0)
        return position;

    const int documentLength = m_document->characterCount() - 1;
    position = qBound(0, position, documentLength);

    if (direction > 0) {
        if (position >= documentLength)
            return documentLength;
    } else if (position <= 0) {
        return 0;
    }

    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid())
        return position;

    const int blockStart = block.position();
    const int blockTextLength = block.text().size();
    // A block's text excludes its paragraph separator, so this is the last
    // cursor position inside the block, just before the separator.
    const int blockEnd = blockStart + blockTextLength;

    if (direction > 0) {
        // The separator is the next valid cursor position past a block; the
        // layout only knows the block's own characters.
        if (position >= blockEnd)
            return position + 1;
    } else if (position <= blockStart) {
        return position - 1;
    }

    // A fresh layout over the block's own text is always in sync with the
    // document, unlike the document's display layout, whose text() cannot be
    // checked for staleness. Building lines is unnecessary for cursor
    // stepping and dominates the cost, so the layout is left unformatted.
    // QTextLayout steps by whole grapheme clusters, so surrogate pairs, skin
    // tone modifiers and ZWJ sequences are crossed in a single move.
    QTextLayout layout(block.text());
    const int local = position - blockStart;
    const int stepped = direction > 0
        ? layout.nextCursorPosition(local, QTextLayout::SkipCharacters)
        : layout.previousCursorPosition(local, QTextLayout::SkipCharacters);
    return blockStart + qBound(0, stepped, blockTextLength);
}

// Matched case-insensitively in the text itself, so the offsets are the
// document's even where lowercasing would change a string's length.
QList<int> Backend::searchMatches(const QString &query) const {
    QList<int> matches;
    if (query.isEmpty())
        return matches;
    const QString text = currentDocumentText();
    int from = 0;
    while ((from = text.indexOf(query, from, Qt::CaseInsensitive)) >= 0) {
        matches.append(from);
        from += query.length();
    }
    return matches;
}

// One edit command, so a single undo takes the whole replacement back.
// Where the line holding `position` starts, as text.lastIndexOf("\n",
// position - 1) + 1 finds it, from the document's own index of its lines
// rather than a copy of the whole text.
int Backend::lineStart(int position) const {
    if (!m_document)
        return 0;
    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    return block.isValid() ? block.position() : 0;
}

// The line break that ends the line holding `position`, or -1 on the last
// line, as text.indexOf("\n", position) finds it.
int Backend::lineEnd(int position) const {
    if (!m_document)
        return -1;
    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid() || !block.next().isValid())
        return -1;
    return block.position() + block.length() - 1;
}

// A block's state says what follows its line, so a fence line itself is only
// code on its inner side: past an opening fence's backticks or tildes, or anywhere
// before the end of a closing one.
bool Backend::insideFence(int position) const {
    if (!m_document)
        return false;
    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return false;
    const bool opensOrContinues = block.userState() >= MarkdownHighlighter::InsideFence;
    const bool afterOpening = block.previous().userState() >= MarkdownHighlighter::InsideFence;
    const int offset = position - block.position();
    if (opensOrContinues && !afterOpening) {
        static const QRegularExpression fenceMark(QStringLiteral("[`~]"));
        return offset >= block.text().indexOf(fenceMark) + 3;
    }
    if (afterOpening && !opensOrContinues)
        return offset < block.text().length();
    return opensOrContinues;
}

WindowGeometry Backend::windowGeometry() const {
    QSettings settings;
    const WindowGeometry defaults;
    return {settings.value(QStringLiteral("window/x"), defaults.x).toInt(),
            settings.value(QStringLiteral("window/y"), defaults.y).toInt(),
            settings.value(QStringLiteral("window/width"), defaults.width).toInt(),
            settings.value(QStringLiteral("window/height"), defaults.height).toInt(),
            settings.value(QStringLiteral("window/maximized"), defaults.maximized).toBool()};
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    if (!maximized) {
        settings.setValue(QStringLiteral("window/x"), x);
        settings.setValue(QStringLiteral("window/y"), y);
        settings.setValue(QStringLiteral("window/width"), width);
        settings.setValue(QStringLiteral("window/height"), height);
    }
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

QString Backend::defaultEditorFont() {
    return QStringLiteral("IBM Plex Mono");
}

// The monospaced fonts Write offers, by the names View › Font shows. Each
// comes with macOS or with Write, but one this Mac lacks is left out rather
// than offered and then missing.
QStringList Backend::editorFonts() {
    QStringList fonts;
    for (const QString &name : {QStringLiteral("IBM Plex Mono"), QStringLiteral("Menlo"),
                                QStringLiteral("Monaco"), QStringLiteral("PT Mono"),
                                QStringLiteral("SF Mono")}) {
        if (QFontDatabase::hasFamily(fontFamily(name)))
            fonts << name;
    }
    return fonts;
}

// "SF Mono" names Apple's separate download of it. macOS's own copy goes by
// a hidden name.
QString Backend::fontFamily(const QString &name) {
    if (name == QLatin1String("SF Mono"))
        return QStringLiteral(".SF NS Mono");
    return name;
}

// Write's own font, from the bundle, and macOS's SF Mono, which Qt does not
// list among the system's fonts.
bool Backend::addFonts(const QDir &bundled) {
    QStringList files;
    for (const QString &file : bundled.entryList({QStringLiteral("*.ttf")}, QDir::Files))
        files << bundled.filePath(file);
    if (files.isEmpty())
        return false;
    files << QStringLiteral("/System/Library/Fonts/SFNSMono.ttf")
          << QStringLiteral("/System/Library/Fonts/SFNSMonoItalic.ttf");
    bool added = true;
    for (const QString &file : std::as_const(files))
        added = QFontDatabase::addApplicationFont(file) != -1 && added;
    return added;
}

void Backend::setEditorFont(const QString &name) {
    if (!editorFonts().contains(name) || name == m_editorFont)
        return;

    m_editorFont = name;
    QSettings().setValue(editorFontSetting, name);
    emit editorFontChanged();
}

bool Backend::editorFontFixedPitch() const {
    return QFontDatabase::isFixedPitch(fontFamily(m_editorFont));
}

void Backend::documentFontChanged() {
    if (m_highlighter)
        m_highlighter->refreshFont();
}

void Backend::loadDocumentText(const QString &text) {
    if (!m_document) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    m_loading = true;
    // As setPlainText does, with undo off and in one edit from an empty
    // document, but formatting each line as it goes in: a new line takes the
    // format of the one it breaks from, so the first line carries the line
    // height to all the rest, where a pass afterwards would format every line
    // again one by one.
    const bool undoEnabled = m_document->isUndoRedoEnabled();
    m_document->setUndoRedoEnabled(false);
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    m_document->clear();
    cursor.setBlockFormat(lineHeightFormat());
    cursor.insertText(text);
    cursor.endEditBlock();
    m_document->setUndoRedoEnabled(undoEnabled);
    m_loading = false;
    takeEditState();

    m_wordCountTimer.stop();
    countWordsInBackground(text);
}

void Backend::setFileUrl(const QUrl &url) {
    if (m_fileUrl == url)
        return;

    m_fileUrl = url;
    emit fileUrlChanged();
}

void Backend::setModified(bool modified) {
    if (m_modified == modified)
        return;

    m_modified = modified;
    emit modifiedChanged();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void Backend::saveTo(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be saved."));
        emit saveFailed();
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QSaveFile file(url.toLocalFile());
    // Without a temporary file beside it, as in a folder only the file itself
    // may be written in, write the file in place rather than fail.
    file.setDirectWriteFallback(true);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not save %1.").arg(targetName));
        emit saveFailed();
        return;
    }

    const QByteArray contents = currentDocumentText().toUtf8();
    file.write(contents);

    // QSaveFile commits by replacing the target. Stop watching the old inode
    // before that replacement so our own write is not classified as external.
    unwatchFiles();

    // commit() flushes, fsyncs, and atomically renames the temp file into place,
    // returning false (and leaving the original untouched) on any write error.
    if (!file.commit()) {
        watchCurrentFile();
        setStatus(QStringLiteral("Could not write %1.").arg(targetName));
        emit saveFailed();
        return;
    }

    setKnownFileContents(contents, true);
    m_autoSaveSuspended = false;
    m_autoSaveTimer.stop();
    setFileUrl(url);
    watchCurrentFile();
    QSettings().setValue(lastSaveDirectorySetting,
                         QFileInfo(url.toLocalFile()).absolutePath());
    setModified(false);
    setStatus(QStringLiteral("Saved %1").arg(fileName()));
    clearRecovery();
    emit saveSucceeded();
}

void Backend::scheduleRecovery() {
    m_recoveryTimer.start();
}

void Backend::scheduleAutoSave() {
    // Only auto-save documents with a file name. Untitled drafts are left to
    // manual save so auto-save never silently opens a file picker or creates a
    // file the writer did not name; crash recovery still covers them. A named
    // file that does not exist yet waits for an explicit save (see autoSave()).
    if (m_fileUrl.isLocalFile() && !m_autoSaveSuspended && !m_autoSaveHeld)
        m_autoSaveTimer.start();
}

void Backend::holdAutoSave(bool held) {
    m_autoSaveHeld = held;
    if (held)
        m_autoSaveTimer.stop();
    else if (m_modified)
        scheduleAutoSave();
}

void Backend::autoSave() {
    if (!m_modified || !m_fileUrl.isLocalFile() || m_autoSaveSuspended || m_autoSaveHeld)
        return;

    // Never write over a version we have not seen. The watcher normally
    // catches outside edits first, but it can miss some (or fire late), so
    // check the file itself right before replacing it.
    const bool exists = QFileInfo::exists(m_fileUrl.toLocalFile());
    if (!m_hasKnownFileContents) {
        // A removed file, a new one not yet saved, or a recovered draft whose
        // file was gone waits for an explicit save rather than being created.
        suspendAutoSave();
        if (exists)
            emit externalChangeDetected(false, m_modified);
        return;
    }
    if (!diskMatchesKnownContents()) {
        reportExternalChange(!exists);
        return;
    }

    saveTo(m_fileUrl);
}

void Backend::suspendAutoSave() {
    // Held until the writer reloads, keeps their version, or saves explicitly.
    m_autoSaveTimer.stop();
    m_autoSaveSuspended = true;
}

// Whether the file still holds what we last read or wrote, read as open()
// reads it so carriage returns compare equal.
bool Backend::diskMatchesKnownContents() const {
    if (!m_hasKnownFileContents)
        return false;
    QFile file(m_fileUrl.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QByteArray contents = file.readAll();
    return contents.size() == m_known.size && bytesDigest(contents) == m_known.digest;
}

void Backend::reportExternalChange(bool deleted) {
    // Whatever is on disk now, it is not what we last read, so the baseline is
    // unknown until the writer picks a version. A deletion counts: the old
    // text is not saved anywhere either.
    setKnownFileContents(QByteArray(), false);
    suspendAutoSave();
    emit externalChangeDetected(deleted, m_modified);
}

// Saving writes UTF-8 with plain line endings; anything else on disk would be
// rewritten by it.
bool Backend::savesUnchanged(const QByteArray &raw) {
    return !raw.contains('\r') && QString::fromUtf8(raw).toUtf8() == raw;
}

void Backend::setKnownFileContents(const QByteArray &contents, bool known) {
    m_hasKnownFileContents = known;
    // Every caller reads in text mode or writes plain line endings, so there
    // are no carriage returns to strip.
    const QString text = known ? QString::fromUtf8(contents) : QString();
    m_known = {known ? contents.size() : 0, bytesDigest(known ? contents : QByteArray()),
               text.size(), textDigest(text)};
}

void Backend::writeRecovery() {
    if (!m_modified)
        return;
    const QString path = m_recoveryPath;
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject recovery{{QStringLiteral("fileUrl"), m_fileUrl.toString()},
                               {QStringLiteral("text"), currentDocumentText()}};
    file.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
    file.commit();
}

void Backend::restoreRecovery() {
    QFile file(m_recoveryPath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject() || !json.object().contains(QStringLiteral("text"))) {
        // Unreadable now means unreadable every launch; do not keep claiming it.
        file.remove();
        return;
    }
    const QJsonObject recovery = json.object();
    loadDocumentText(recovery.value(QStringLiteral("text")).toString());
    const QUrl recoveredUrl(recovery.value(QStringLiteral("fileUrl")).toString());
    QFile diskFile(recoveredUrl.toLocalFile());
    if (recoveredUrl.isLocalFile() && diskFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setKnownFileContents(diskFile.readAll(), true);
    } else {
        setKnownFileContents(QByteArray(), false);
    }
    setFileUrl(recoveredUrl);
    // The file may have moved on since this draft was written, and the draft
    // alone cannot tell. Leave overwriting it to an explicit save.
    m_autoSaveSuspended = recoveredUrl.isLocalFile();
    watchCurrentFile();
    setModified(true);
    setStatus(QStringLiteral("Recovered unsaved changes"));
}

void Backend::clearRecovery() {
    m_recoveryTimer.stop();
    QFile::remove(m_recoveryPath);
}

void Backend::unwatchFiles() {
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
}

void Backend::watchCurrentFile() {
    unwatchFiles();
    if (m_fileUrl.isLocalFile() && QFileInfo::exists(m_fileUrl.toLocalFile()))
        m_fileWatcher.addPath(m_fileUrl.toLocalFile());
}

// The colours follow the system appearance, light or dark.
void Backend::applyTheme() {
    const QStringList previousColors{m_themeBackground, m_themeForeground, m_themeAccent,
                                     m_themeSelection, m_themeSelectionForeground,
                                     m_themeCodeBackground};

    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");
    m_themeSelection = m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2");
    m_themeSelectionForeground = m_themeForeground;
    // Code sits on a panel a shade of the page towards the text. Mixing,
    // unlike lightening, still moves on a pure black background.
    m_themeCodeBackground = blend(QColor(m_themeBackground), QColor(m_themeForeground), 0.06).name();

    if (m_highlighter) {
        m_highlighter->setTheme(m_darkMode, m_themeBackground, m_themeForeground,
                                m_themeAccent, m_themeCodeBackground);
    }

    const QStringList colors{m_themeBackground, m_themeForeground, m_themeAccent,
                             m_themeSelection, m_themeSelectionForeground,
                             m_themeCodeBackground};
    if (colors != previousColors)
        emit themeColorsChanged();
}

QUrl Backend::suggestedSaveUrl() const {
    if (m_fileUrl.isLocalFile())
        return m_fileUrl;

    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    const QDir directory = savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir::home()
        : QDir(savedDirectory);
    return QUrl::fromLocalFile(
        directory.filePath(suggestedFileName(currentDocumentText())));
}

QString Backend::currentDocumentText() const {
    return m_document ? m_document->toPlainText() : QString();
}

int Backend::countWords(const QString &text) {
    // Only letters and numbers: PCRE2 reads \p{Han} as Script_Extensions, which
    // puts 。 、 「 and the CJK radicals in Han and would count each as a word.
    static const QRegularExpression cjkRe(
        QStringLiteral("(?=[\\p{L}\\p{N}])[\\p{Han}\\p{Hiragana}\\p{Katakana}]"));
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}\\p{N}]+(?:['-][\\p{L}\\p{N}]+)*"));

    // Count unspaced Han and kana characters individually, then replace them
    // so they remain boundaries between surrounding space-delimited words.
    int count = 0;

    QRegularExpressionMatchIterator cjkIt = cjkRe.globalMatch(text);
    while (cjkIt.hasNext()) {
        cjkIt.next();
        ++count;
    }

    QString nonCjkText = text;
    nonCjkText.replace(cjkRe, QStringLiteral(" "));

    QRegularExpressionMatchIterator it = wordRe.globalMatch(nonCjkText);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

QString Backend::suggestedFileName(const QString &text) {
    QString name = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    name.replace(QRegularExpression(QStringLiteral("[/\\x00-\\x1f\\x7f]")),
                 QStringLiteral("-"));
    name = name.left(120).trimmed();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = QStringLiteral("Untitled");
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    return name;
}

void Backend::setWordCount(int words) {
    if (m_wordCount == words)
        return;

    m_wordCount = words;
    emit wordCountChanged();
}

void Backend::refreshWordCount() {
    countWordsInBackground(currentDocumentText());
}

// Counting a long document takes a while, so it runs on a worker thread and
// typing never waits for it.
void Backend::countWordsInBackground(const QString &text) {
    if (m_wordCountWatcher.isRunning()) {
        m_wordCountStale = true;
        return;
    }
    m_wordCountWatcher.setFuture(QtConcurrent::run(&Backend::countWords, text));
}

bool Backend::documentEdited() const {
    return m_editState.commandAdded
        || m_document->characterCount() != m_editState.length
        || m_document->availableUndoSteps() != m_editState.undoSteps
        || m_document->availableRedoSteps() != m_editState.redoSteps;
}

void Backend::takeEditState() {
    m_editState = {m_document->characterCount(), m_document->availableUndoSteps(),
                   m_document->availableRedoSteps(), false};
}

// Lengths differ for almost every edit, which settles it without reading the
// whole text.
bool Backend::documentMatchesKnownText() const {
    if (!m_document || m_document->characterCount() - 1 != m_known.textLength)
        return false;
    return textDigest(currentDocumentText()) == m_known.textDigest;
}

void Backend::scheduleWordCount() {
    m_wordCountTimer.start();
}

void Backend::applyDocumentTypography() {
    if (!m_document)
        return;

    // A full pass is only used for freshly loaded/attached documents, so it is
    // safe to drop undo history here (re-enabling clears the stack anyway).
    const bool undoEnabled = m_document->isUndoRedoEnabled();
    m_document->setUndoRedoEnabled(false);

    // Turning undo back on reports a change too, so the pass ends after it.
    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(lineHeightFormat());
    m_document->setUndoRedoEnabled(undoEnabled);
    m_formattingTypography = false;
    takeEditState();
}

void Backend::reapplyTypographyToChange() {
    if (!m_document)
        return;

    // Format only the block(s) touched by the last edit instead of the whole
    // document, and fold the change into the preceding edit command so a single
    // undo reverts both the text and its formatting.
    const int maxPos = m_document->characterCount() - 1;
    const int start = qBound(0, m_lastChangePos, maxPos);
    const int end = qBound(start, m_lastChangePos + m_lastChangeAdded, maxPos);

    // Blocks that already have it are left alone: an undo brings their format
    // back with the text, and merging anyway would push a command that clears
    // the redo stack.
    bool needed = false;
    for (QTextBlock block = m_document->findBlock(start);
         block.isValid() && block.position() <= end; block = block.next()) {
        const QTextBlockFormat format = block.blockFormat();
        if (format.lineHeightType() != QTextBlockFormat::ProportionalHeight
                || format.lineHeight() != typoraLineHeightPercent) {
            needed = true;
            break;
        }
    }
    if (!needed)
        return;

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.joinPreviousEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.mergeBlockFormat(lineHeightFormat());
    cursor.endEditBlock();
    m_formattingTypography = false;
}
