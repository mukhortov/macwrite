#include <QFont>
#include <QFontDatabase>
#include <QApplication>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>
#include <QFile>

#include "appeventfilter.h"
#include "backend.h"
#include "cli.h"
#include "systemtheme.h"
#include "windowmanager.h"

int main(int argc, char *argv[]) {
    // Answered before the application exists, so they need no display.
    if (const std::optional<int> exitCode =
            Cli::handleArguments(argc, argv, QStringLiteral(MACWRITE_VERSION)))
        return *exitCode;

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("macwrite"));
    app.setApplicationVersion(QStringLiteral(MACWRITE_VERSION));

    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/IBMPlexMono-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/IBMPlexMono-Italic.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/IBMPlexMono-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/IBMPlexMono-BoldItalic.ttf"));
    // Settings land in the bundle's own preferences, com.mukhortov.macwrite.
    app.setOrganizationDomain(QStringLiteral("mukhortov.com"));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    // A Mac app keeps running with every window closed, until it is quit.
    app.setQuitOnLastWindowClosed(false);

    // Carry the text scale into the default font, so the chrome that inherits
    // it (dialog titles, buttons) grows along with the writing area.
    const QFont interfaceFont(QStringLiteral("IBM Plex Mono"));
    const qreal basePointSize = interfaceFont.pointSizeF() > 0
        ? interfaceFont.pointSizeF()
        : app.font().pointSizeF();
    const auto applyInterfaceFont = [&app, interfaceFont, basePointSize](qreal textScale) {
        QFont scaled = interfaceFont;
        scaled.setPointSizeF(basePointSize * textScale);
        app.setFont(scaled);
    };
    applyInterfaceFont(Backend::storedTextScale());

    QQmlEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });

    WindowManager windows(&engine, QUrl(QStringLiteral("qrc:/Main.qml")));
    QObject::connect(&windows, &WindowManager::textScaleChanged, &app, applyInterfaceFont);

    SystemTheme systemTheme(&app);
    windows.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &windows,
                     &WindowManager::setDarkMode);

    windows.openStartupWindows();
    if (windows.windows().isEmpty()) {
        qCritical() << "Could not load the Macwrite interface; resource available:"
                    << QFile::exists(QStringLiteral(":/Main.qml"));
        return -1;
    }

    const QStringList args = app.arguments();
    if (args.size() > 1)
        windows.openFile(QUrl::fromLocalFile(args.at(1)));
    AppEventFilter appEvents(&windows);
    app.installEventFilter(&appEvents);

    return app.exec();
}
