#include "findbar.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputMethod>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "backend.h"
#include "markdowneditor.h"
#include "markdownedits.h"

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

FindBar::FindBar(MarkdownEditor *editor, Backend *backend, QWidget *parent)
    : QWidget(parent), m_editor(editor), m_backend(backend) {
    setObjectName(QStringLiteral("findBar"));

    m_searchField = new QLineEdit(this);
    m_searchField->setObjectName(QStringLiteral("searchField"));
    m_searchField->setPlaceholderText(QStringLiteral("Find"));
    m_searchField->setClearButtonEnabled(true);
    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("matchCount"));
    auto *previous = new QToolButton(this);
    previous->setObjectName(QStringLiteral("previousMatchButton"));
    previous->setArrowType(Qt::LeftArrow);
    previous->setToolTip(QStringLiteral("Previous"));
    auto *next = new QToolButton(this);
    next->setObjectName(QStringLiteral("nextMatchButton"));
    next->setArrowType(Qt::RightArrow);
    next->setToolTip(QStringLiteral("Next"));
    auto *done = new QPushButton(QStringLiteral("Done"), this);
    done->setObjectName(QStringLiteral("doneButton"));

    m_replaceRow = new QWidget(this);
    m_replaceField = new QLineEdit(m_replaceRow);
    m_replaceField->setObjectName(QStringLiteral("replaceField"));
    m_replaceField->setPlaceholderText(QStringLiteral("Replace with"));
    auto *replace = new QPushButton(QStringLiteral("Replace"), m_replaceRow);
    replace->setObjectName(QStringLiteral("replaceButton"));
    auto *replaceAll = new QPushButton(QStringLiteral("All"), m_replaceRow);
    replaceAll->setObjectName(QStringLiteral("replaceAllButton"));
    // Return in a field belongs to the bar, not to a default button.
    for (QPushButton *button : {done, replace, replaceAll})
        button->setAutoDefault(false);

    auto *findRow = new QHBoxLayout;
    findRow->addWidget(m_searchField, 1);
    findRow->addWidget(m_count);
    findRow->addWidget(previous);
    findRow->addWidget(next);
    findRow->addWidget(done);
    auto *replaceRowLayout = new QHBoxLayout(m_replaceRow);
    replaceRowLayout->setContentsMargins(0, 0, 0, 0);
    replaceRowLayout->addWidget(m_replaceField, 1);
    replaceRowLayout->addWidget(replace);
    replaceRowLayout->addWidget(replaceAll);
    auto *rows = new QVBoxLayout(this);
    rows->addLayout(findRow);
    rows->addWidget(m_replaceRow);
    setTabOrder(m_searchField, m_replaceField);

    m_searchField->installEventFilter(this);
    m_replaceField->installEventFilter(this);
    m_searchDelay.setSingleShot(true);
    m_searchDelay.setInterval(150);
    connect(&m_searchDelay, &QTimer::timeout, this, [this]() { search(true); });
    connect(m_searchField, &QLineEdit::textChanged, &m_searchDelay, qOverload<>(&QTimer::start));
    connect(previous, &QToolButton::clicked, this, [this]() { step(-1); });
    connect(next, &QToolButton::clicked, this, [this]() { step(1); });
    connect(done, &QPushButton::clicked, this, &FindBar::dismiss);
    connect(replace, &QPushButton::clicked, this, &FindBar::replaceCurrent);
    connect(replaceAll, &QPushButton::clicked, this, &FindBar::replaceAll);
    connect(m_editor, &MarkdownEditor::edited, this, &FindBar::documentChanged);
    connect(m_editor, &MarkdownEditor::viewChanged, this, &FindBar::updateHighlights);

    m_replaceRow->hide();
    hide();
}

// The matches stop following the field and the text while the bar is
// closed, and a search still waiting for a pause is dropped on closing, so
// opening the bar searches again for what the field holds.
void FindBar::open(bool withReplace) {
    show();
    if (withReplace)
        m_replaceRow->show();
    // A search still waiting in an open bar lands on its first match.
    search(m_searchDelay.isActive());
    m_searchField->setFocus();
    m_searchField->selectAll();
}

// The text takes the focus before the bar hides: hiding the field that has
// it leaves the input method with nothing focused for a moment.
void FindBar::dismiss() {
    m_searchDelay.stop();
    QGuiApplication::inputMethod()->reset();
    m_editor->setFocus();
    hide();
    m_replaceRow->hide();
    m_editor->setSearchHighlights({});
    QTextCursor cursor = m_editor->textCursor();
    cursor.clearSelection();
    m_editor->setTextCursor(cursor);
}

void FindBar::step(int direction) {
    // A pending search lands on its first match, which is where the writer
    // meant to go, rather than one past it.
    if (m_searchDelay.isActive()) {
        search(true);
        return;
    }
    if (m_matches.count() == 0)
        return;
    m_matches.step(direction);
    showCurrent(true);
}

void FindBar::flushSearch() {
    if (m_searchDelay.isActive())
        search(true);
}

void FindBar::search(bool selectMatch) {
    m_searchDelay.stop();
    const QString query = m_searchField->text();
    m_matches.update(query.isEmpty() ? QList<int>() : m_backend->searchMatches(query),
                     int(query.size()), selectMatch);
    showCurrent(selectMatch);
}

void FindBar::showCurrent(bool selectMatch) {
    const int start = m_matches.currentStart();
    if (selectMatch && start >= 0) {
        QTextCursor cursor = m_editor->textCursor();
        cursor.setPosition(start);
        cursor.setPosition(start + m_matches.queryLength(), QTextCursor::KeepAnchor);
        m_editor->setTextCursor(cursor);
    }
    m_count->setText(m_matches.countText());
    updateHighlights();
}

// Only the matches in view are drawn: a short query in a long document can
// match hundreds of thousands of times.
void FindBar::updateHighlights() {
    if (!isOpen() || m_matches.count() == 0) {
        m_editor->setSearchHighlights({});
        return;
    }
    const auto [first, last] = m_editor->visiblePositions();
    m_editor->setSearchHighlights(m_matches.inRange(first, last));
}

void FindBar::documentChanged() {
    if (isOpen())
        search(false);
}

// Carries on past what was just written, so a replacement the query also
// matches is not found again.
void FindBar::replaceCurrent() {
    flushSearch();
    const int start = m_matches.currentStart();
    if (start < 0)
        return;
    const QString replacement = m_replaceField->text();
    QTextCursor cursor = m_editor->textCursor();
    MarkdownEdits::replaceRange(cursor, start, start + m_matches.queryLength(), replacement);
    m_editor->setTextCursor(cursor);
    m_matches.update(m_backend->searchMatches(m_searchField->text()), m_matches.queryLength(), false);
    m_matches.setCurrentAtOrAfter(start + int(replacement.size()));
    showCurrent(true);
}

// One replacement over the span of every match, so a single undo takes them
// all back.
void FindBar::replaceAll() {
    flushSearch();
    if (m_matches.count() == 0)
        return;
    const QString text = m_editor->toPlainText();
    const QString replacement = m_replaceField->text();
    const int length = m_matches.queryLength();
    const QList<SearchHighlight> all = m_matches.inRange(0, int(text.size()), m_matches.count());
    QString replaced;
    int position = all.first().start;
    for (const SearchHighlight &match : all) {
        replaced += QStringView(text).mid(position, match.start - position) + replacement;
        position = match.start + length;
    }
    QTextCursor cursor = m_editor->textCursor();
    MarkdownEdits::replaceRange(cursor, all.first().start, position, replaced);
    m_editor->setTextCursor(cursor);
}

bool FindBar::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Escape) {
        dismiss();
        return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        if (watched == m_replaceField)
            replaceCurrent();
        else
            step(key->modifiers() & Qt::ShiftModifier ? -1 : 1);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
