// Opens a copy of a Markdown file in Write's editor and times typing in it:
// each key with the repaint it causes. Works on a temporary copy with Qt's
// test mode on, so no real file, setting or draft is touched.
//
// Usage: editorbench FILE [KEYS]; BENCH_POS=middle types mid-document.
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "backend.h"
#include "markdowneditor.h"

static double milliseconds(const QElapsedTimer &timer) {
    return double(timer.nsecsElapsed()) / 1e6;
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
    MarkdownEditor editor(&backend);
    editor.resize(1280, 820);
    editor.show();
    if (!QTest::qWaitForWindowExposed(&editor))
        return 1;

    QElapsedTimer clock;
    clock.start();
    backend.openOrStartFile(QUrl::fromLocalFile(copy));
    const double loaded = milliseconds(clock);
    editor.viewport()->repaint();
    const double opened = milliseconds(clock);

    const bool middle = qgetenv("BENCH_POS") == "middle";
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(middle ? editor.document()->characterCount() / 2 : 5);
    editor.setTextCursor(cursor);
    editor.setFocus();
    editor.viewport()->repaint();

    QList<double> times;
    for (int key = 0; key < keys; ++key) {
        QElapsedTimer timer;
        timer.start();
        QTest::keyClick(&editor, Qt::Key_X);
        editor.viewport()->repaint();
        times.append(milliseconds(timer));
    }
    double total = 0;
    for (double time : times)
        total += time;
    std::printf("open: %.0f ms, %.0f ms to the first paint | %s: key avg %.1f ms max %.1f ms over %d keys\n",
                loaded, opened,
                middle ? "middle" : "top", total / times.size(),
                *std::max_element(times.cbegin(), times.cend()), keys);
    backend.discardRecovery();
    return 0;
}
