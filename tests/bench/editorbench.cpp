// Opens a copy of a Markdown file in a Write window and times what a writer
// waits on: opening it, typing at the top and in the middle, scrolling,
// searching and switching theme. Each step is timed with the repaint it
// causes. Works on a temporary copy with Qt's test mode on, so no real file,
// setting or draft is touched.
//
// Usage: editorbench FILE [KEYS]
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QLineEdit>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "backend.h"
#include "findbar.h"
#include "mainwindow.h"
#include "markdowneditor.h"

namespace {

double milliseconds(const QElapsedTimer &timer) {
    return double(timer.nsecsElapsed()) / 1e6;
}

// Times each run of the step, with the repaint it causes.
QList<double> timeSteps(int count, QWidget *painted, const std::function<void(int)> &step) {
    QList<double> times;
    for (int index = 0; index < count; ++index) {
        QElapsedTimer timer;
        timer.start();
        step(index);
        painted->repaint();
        times.append(milliseconds(timer));
    }
    return times;
}

void report(const char *what, const QList<double> &times) {
    double total = 0;
    for (double time : times)
        total += time;
    std::printf("%s: avg %.1f ms, max %.1f ms over %lld\n", what, total / times.size(),
                *std::max_element(times.cbegin(), times.cend()), qlonglong(times.size()));
}

}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: editorbench FILE [KEYS]\n");
        return 2;
    }
    QStandardPaths::setTestModeEnabled(true);
    // The fonts the app bundles, so the editor writes in the same one.
    for (const char *face : {"Regular", "Italic", "Bold", "BoldItalic"}) {
        QFontDatabase::addApplicationFont(
            QStringLiteral(SOURCE_ROOT "/fonts/IBMPlexMono-%1.ttf").arg(QLatin1String(face)));
    }
    const int keys = argc > 2 ? std::atoi(argv[2]) : 40;

    QTemporaryDir directory;
    const QString copy = directory.filePath(QStringLiteral("bench.md"));
    if (!QFile::copy(QString::fromLocal8Bit(argv[1]), copy)) {
        std::fprintf(stderr, "could not copy %s\n", argv[1]);
        return 1;
    }
    QFile::setPermissions(copy, QFile::ReadOwner | QFile::WriteOwner);

    Backend backend;
    MainWindow window(&backend);
    window.resize(1280, 820);
    window.show();
    if (!QTest::qWaitForWindowExposed(&window))
        return 1;
    MarkdownEditor *editor = window.editor();
    QWidget *view = editor->viewport();

    QElapsedTimer clock;
    clock.start();
    backend.openOrStartFile(QUrl::fromLocalFile(copy));
    const double loaded = milliseconds(clock);
    window.repaint();
    std::printf("open: %.0f ms, %.0f ms to the first paint\n", loaded, milliseconds(clock));

    const auto typeAt = [&](int position) {
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(position);
        editor->setTextCursor(cursor);
        editor->setFocus();
        view->repaint();
        return timeSteps(keys, view, [editor](int) { QTest::keyClick(editor, Qt::Key_X); });
    };
    report("key at the top", typeAt(5));
    report("key in the middle", typeAt(editor->document()->characterCount() / 2));

    QScrollBar *scroll = editor->verticalScrollBar();
    const int middle = scroll->maximum() / 2;
    report("scroll step", timeSteps(200, view, [scroll, middle](int step) {
        scroll->setValue(middle + step * 48);
    }));

    window.findBar()->open(false);
    const QString query = QStringLiteral("the s");
    report("search key", timeSteps(int(query.size()), &window, [&window, &query](int index) {
        QTest::keyClick(window.findBar()->searchField(), query.at(index).toLatin1());
    }));
    std::printf("matches: %d\n", window.findBar()->matches().count());
    window.findBar()->dismiss();

    report("theme switch", timeSteps(2, &window, [&backend](int) {
        backend.setDarkMode(!backend.darkMode());
    }));
    backend.discardRecovery();
    return 0;
}
