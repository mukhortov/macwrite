#pragma once

#include <QObject>
#include <QPointer>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QString>
#include <QStringList>
#include <QFutureWatcher>
#include <QTimer>
#include <QUrl>
#include <memory>
#include <optional>

class MarkdownHighlighter;
class QFont;
class QTextDocument;
class QWindow;
class QLockFile;
class QMimeData;

// A stretch of the document, in positions.
struct TextRange {
    int start = 0;
    int end = 0;
};

// An emphasis span: its markers from start to end, the text between them,
// and where the visible text ends before markers nested inside closing with it.
struct EmphasisSpan {
    int start = 0;
    int end = 0;
    int contentStart = 0;
    int contentEnd = 0;
    int textEnd = 0;
};

// Where a window opens; -1 leaves the place to the system.
struct WindowGeometry {
    int x = -1;
    int y = -1;
    int width = 1280;
    int height = 820;
    bool maximized = false;
};

class Backend : public QObject {
    Q_OBJECT

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    void setParentWindow(QWindow *window);

    QUrl fileUrl() const { return m_fileUrl; }
    QString fileName() const;

    bool modified() const { return m_modified; }
    QString status() const { return m_status; }
    int wordCount() const { return m_wordCount; }
    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textZoom; }
    // Every window shares the text size and the font, so another window's
    // change arrives here as well.
    void setTextScale(qreal scale);
    static qreal storedTextScale();
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeSelection() const { return m_themeSelection; }
    QString themeCodeBackground() const { return m_themeCodeBackground; }
    QString themeSelectionForeground() const { return m_themeSelectionForeground; }
    QString editorFont() const { return m_editorFont; }
    void setEditorFont(const QString &family);
    bool editorFontFixedPitch() const;
    static QString defaultEditorFont();
    static int countWords(const QString &text);
    static QString normalizedLinkUrl(const QString &clipboardText);
    static QString suggestedFileName(const QString &text);
    static QFont printFont(const QFont &editorFont, qreal screenDpi);

    void attachDocument(QTextDocument *document);
    void openDialog();
    void open(const QUrl &url);
    void openOrStartFile(const QUrl &url);
    // A blank window nobody has written in, which a file opened from Finder
    // may take over.
    bool isUntouched() const;
    // Drafts left by windows that did not close, such as after a crash, that
    // no window has taken yet.
    static int unclaimedRecoveryDrafts();
    void save();
    void saveAsDialog();
    void saveAs(const QUrl &url);
    void discardRecovery();
    void reloadFromDisk();
    void keepExternalVersion();
    void increaseTextSize();
    void decreaseTextSize();
    void resetTextSize();
    void holdAutoSave(bool held);
    void printDocument();
    void copyAsHtml();
    void newWindow();
    QString clipboardUrl() const;
    // The link a paste or drop carries, if it is one.
    static QString linkUrlFrom(const QMimeData *mimeData);
    QString clipboardText() const;
    bool editorTextChanged();
    QList<TextRange> hiddenRangesAt(int position) const;
    QString linkUrlAt(int position) const;
    std::optional<EmphasisSpan> emphasisSpanAt(int position, const QString &marker) const;
    int cursorPositionStep(int position, int direction) const;
    QList<int> searchMatches(const QString &query) const;
    bool insideFence(int position) const;
    int lineStart(int position) const;
    int lineEnd(int position) const;
    WindowGeometry windowGeometry() const;
    void saveWindowGeometry(int x, int y, int width, int height, bool maximized);
    void documentFontChanged();

signals:
    void fileUrlChanged();
    void modifiedChanged();
    void statusChanged();
    void wordCountChanged();
    void darkModeChanged();
    void textScaleChanged();
    void themeColorsChanged();
    void editorFontChanged();
    void saveFailed();
    void openDialogRequested();
    void saveDialogRequested(const QUrl &suggestedUrl);
    void saveSucceeded();
    void externalChangeDetected(bool deleted, bool locallyModified);
    void newWindowRequested();

private:
    friend class WriteTest;

    void loadDocumentText(const QString &text);
    qreal steppedTextZoom(int direction) const;
    void setTextZoom(qreal zoom);
    void setFileUrl(const QUrl &url);
    void setModified(bool modified);
    void setStatus(const QString &status);
    void saveTo(const QUrl &url);
    QUrl suggestedSaveUrl() const;
    QString currentDocumentText() const;
    void setWordCount(int words);
    void refreshWordCount();
    void scheduleWordCount();
    void countWordsInBackground(const QString &text);
    bool documentMatchesKnownText() const;
    bool documentEdited() const;
    void takeEditState();
    void applyDocumentTypography();
    void reapplyTypographyToChange();
    void scheduleRecovery();
    void writeRecovery();
    void scheduleAutoSave();
    void autoSave();
    void suspendAutoSave();
    void restoreRecovery();
    void clearRecovery();
    bool diskMatchesKnownContents() const;
    void reportExternalChange(bool deleted);
    static bool savesUnchanged(const QByteArray &raw);
    void setKnownFileContents(const QByteArray &contents, bool known);
    void unwatchFiles();
    void watchCurrentFile();
    void applyTheme();

    QUrl m_fileUrl;
    bool m_modified = false;
    QString m_status;
    int m_wordCount = 0;
    bool m_darkMode = true;
    qreal m_textZoom = 1.0;
    bool m_loading = false;
    bool m_formattingTypography = false;
    int m_lastChangePos = 0;
    int m_lastChangeAdded = 0;
    QTimer m_wordCountTimer;
    QTimer m_recoveryTimer;
    QTimer m_autoSaveTimer;
    QFileSystemWatcher m_fileWatcher;
    QPointer<QTextDocument> m_document;
    QPointer<QWindow> m_parentWindow;
    QPointer<MarkdownHighlighter> m_highlighter;
    // What the document looked like when its text was last taken in. Edits
    // change its length or its undo history; highlighting and line-height
    // passes change neither, so an edit is told apart without copying the text.
    struct EditState {
        int length = -1;
        int undoSteps = 0;
        int redoSteps = 0;
        bool commandAdded = false;
    } m_editState;
    QFutureWatcher<int> m_wordCountWatcher;
    bool m_wordCountStale = false;
    // What the file held when last read or written, kept as lengths and
    // digests rather than copies: equality is all they are asked, and a copy
    // of a long document and its text would cost three times its size.
    struct KnownContents {
        qsizetype size = 0;
        QByteArray digest;
        qsizetype textLength = 0;
        QByteArray textDigest;
    } m_known;
    bool m_hasKnownFileContents = false;
    bool m_autoSaveSuspended = false;
    bool m_autoSaveHeld = false;
    QString m_recoveryPath;
    std::unique_ptr<QLockFile> m_recoveryLock;

    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeSelection;
    QString m_themeCodeBackground;
    QString m_themeSelectionForeground;
    QString m_editorFont;
};
