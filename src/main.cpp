#include <CoreFoundation/CoreFoundation.h>
#include <QFont>
#include <QFontDatabase>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>
#include <QWindow>
#include <QFile>

#include "appeventfilter.h"
#include "backend.h"
#include "cli.h"
#include "systemtheme.h"

int main(int argc, char *argv[]) {
    // Answered before the application exists, so they need no display.
    if (const std::optional<int> exitCode =
            Cli::handleArguments(argc, argv, QStringLiteral(MACWRITE_VERSION)))
        return *exitCode;

    // AppKit adds an Enter Full Screen item of its own to a menu called View,
    // which would sit beside the one Macwrite's View menu already has.
    CFPreferencesSetAppValue(CFSTR("NSFullScreenMenuItemEverywhere"), kCFBooleanFalse,
                             kCFPreferencesCurrentApplication);

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

    Backend backend(&app);
    SystemTheme systemTheme(&app);
    backend.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &backend,
                     &Backend::setDarkMode);

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

    applyInterfaceFont(backend.textScale());
    QObject::connect(&backend, &Backend::textScaleChanged, &app,
                     [&backend, applyInterfaceFont]() {
        applyInterfaceFont(backend.textScale());
    });

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the Macwrite interface; resource available:"
                    << QFile::exists(QStringLiteral(":/Main.qml"));
        return -1;
    }

    auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
    backend.setParentWindow(window);

    const QStringList args = app.arguments();
    if (args.size() > 1)
        backend.openOrStartFile(QUrl::fromLocalFile(args.at(1)));
    AppEventFilter appEvents(&backend, window);
    app.installEventFilter(&appEvents);

    return app.exec();
}
