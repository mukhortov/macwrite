#include <CoreFoundation/CoreFoundation.h>
#include <QFont>
#include <QApplication>
#include <QDir>
#include <QUrl>

#include "appeventfilter.h"
#include "backend.h"
#include "cli.h"
#include "macfonts.h"
#include "systemtheme.h"
#include "windowmanager.h"

int main(int argc, char *argv[]) {
    // Answered before the application exists, so they need no display.
    if (const std::optional<int> exitCode =
            Cli::handleArguments(argc, argv, QStringLiteral(WRITE_VERSION)))
        return *exitCode;

    // AppKit adds an Enter Full Screen item of its own to a menu called View,
    // which would sit beside the one Write's View menu already has.
    CFPreferencesSetAppValue(CFSTR("NSFullScreenMenuItemEverywhere"), kCFBooleanFalse,
                             kCFPreferencesCurrentApplication);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("write"));
    app.setApplicationVersion(QStringLiteral(WRITE_VERSION));

    // Write's own font, from the bundle.
    const QDir fonts(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/Fonts"));
    for (const QString &file : fonts.entryList({QStringLiteral("*.ttf")}, QDir::Files))
        MacFonts::addApplicationFont(fonts.filePath(file));

    // Settings land in the bundle's own preferences, com.mukhortov.write, and
    // drafts under Application Support/mukhortov/write, clear of any other
    // app called Write.
    app.setOrganizationName(QStringLiteral("mukhortov"));
    app.setOrganizationDomain(QStringLiteral("mukhortov.com"));

    // A Mac app keeps running with every window closed, until it is quit.
    app.setQuitOnLastWindowClosed(false);

    WindowManager windows;
    SystemTheme systemTheme(&app);
    windows.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &windows,
                     &WindowManager::setDarkMode);

    windows.openStartupWindows();

    const QStringList args = app.arguments();
    if (args.size() > 1)
        windows.openFile(QUrl::fromLocalFile(args.at(1)));
    AppEventFilter appEvents(&windows);
    app.installEventFilter(&appEvents);

    return app.exec();
}
