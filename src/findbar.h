#pragma once

#include <QList>
#include <QString>

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
