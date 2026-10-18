#include "findbar.h"

#include <algorithm>

void SearchMatches::update(const QList<int> &starts, int queryLength, bool selectFirst) {
    const int previous = qMax(0, currentStart());
    m_starts = starts;
    m_queryLength = queryLength;
    m_current = selectFirst ? (m_starts.isEmpty() ? -1 : 0) : atOrAfterWrapping(previous);
}

void SearchMatches::step(int direction) {
    if (m_starts.isEmpty())
        return;
    m_current = int((m_current + direction + m_starts.size()) % m_starts.size());
}

void SearchMatches::setCurrentAtOrAfter(int position) {
    m_current = atOrAfterWrapping(position);
}

int SearchMatches::firstAtOrAfter(int position) const {
    return int(std::lower_bound(m_starts.cbegin(), m_starts.cend(), position) - m_starts.cbegin());
}

int SearchMatches::atOrAfterWrapping(int position) const {
    if (m_starts.isEmpty())
        return -1;
    const int index = firstAtOrAfter(position);
    return index < m_starts.size() ? index : 0;
}

QList<SearchHighlight> SearchMatches::inRange(int first, int last, int limit) const {
    QList<SearchHighlight> highlights;
    for (int index = firstAtOrAfter(first - m_queryLength + 1);
         index < m_starts.size() && m_starts.at(index) <= last && highlights.size() < limit; ++index) {
        const int start = m_starts.at(index);
        highlights.append({start, start + m_queryLength, index == m_current});
    }
    return highlights;
}

QString SearchMatches::countText() const {
    if (m_queryLength == 0)
        return {};
    return QStringLiteral("%1 of %2").arg(m_current + 1).arg(m_starts.size());
}
