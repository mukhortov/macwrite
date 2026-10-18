#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class Backend;
class MarkdownEditor;
class QLabel;
class QLineEdit;
class QPushButton;

// A match in view, for the editor to highlight.
struct SearchHighlight {
    int start = 0;
    int end = 0;
    bool current = false;
};

// The matches of a search: where each starts, which one is current, and which
// fall in view. A one-letter query can match hundreds of thousands of times
// in a long document, so each question here is a search, not a walk.
class SearchMatches {
public:
    // New results for the query. selectFirst makes the first match current;
    // otherwise the current match is the first at or after where the current
    // one was, so an edit moves the matches along without moving the writer.
    void update(const QList<int> &starts, int queryLength, bool selectFirst);
    int count() const { return int(m_starts.size()); }
    int currentIndex() const { return m_current; }
    int currentStart() const { return m_current < 0 ? -1 : m_starts.at(m_current); }
    int queryLength() const { return m_queryLength; }
    // Steps to the next or previous match, round from either end.
    void step(int direction);
    void setCurrentAtOrAfter(int position);
    // The index of the first match starting at or past the position, or
    // count() when there is none.
    int firstAtOrAfter(int position) const;
    // The matches overlapping [first, last], at most limit of them.
    QList<SearchHighlight> inRange(int first, int last, int limit = 2000) const;
    // "N of M", or nothing while there is no query.
    QString countText() const;

private:
    int atOrAfterWrapping(int position) const;

    QList<int> m_starts;
    int m_queryLength = 0;
    int m_current = -1;
};

// Find, and find and replace, across the top of the window. The matches
// follow the writer's edits without taking the caret; only the field and
// the next and previous buttons select a match.
class FindBar : public QWidget {
    Q_OBJECT

public:
    FindBar(MarkdownEditor *editor, Backend *backend, QWidget *parent = nullptr);

    // Shows the bar, with the replace row too when asked, and puts the writer
    // in the search field.
    void open(bool withReplace);
    // Hides the bar and its highlights and returns the writer to the text.
    // Not close(), which a widget already has.
    void dismiss();
    bool isOpen() const { return isVisible(); }
    void step(int direction);
    QLineEdit *searchField() const { return m_searchField; }
    QLineEdit *replaceField() const { return m_replaceField; }
    const SearchMatches &matches() const { return m_matches; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void search(bool selectMatch);
    void showCurrent(bool selectMatch);
    void updateHighlights();
    void documentChanged();
    void replaceCurrent();
    void replaceAll();

    MarkdownEditor *m_editor;
    Backend *m_backend;
    SearchMatches m_matches;
    QLineEdit *m_searchField;
    QLineEdit *m_replaceField;
    QLabel *m_count;
    QWidget *m_replaceRow;
};
