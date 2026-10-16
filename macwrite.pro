!macx: error("Macwrite builds for macOS only.")

QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2

CONFIG += c++17 release
TARGET = Macwrite
TEMPLATE = app

# The one place the version lives: the app reports it and the bundle carries it.
VERSION = 0.7.0
DEFINES += MACWRITE_VERSION=\\\"$$VERSION\\\"

QMAKE_INFO_PLIST = macos/Info.plist
ICON = macos/macwrite.icns

HEADERS += \
    src/appeventfilter.h \
    src/backend.h \
    src/cli.h \
    src/markdownhighlighter.h \
    src/systemtheme.h \
    src/windowmanager.h

SOURCES += \
    src/main.cpp \
    src/appeventfilter.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/markdownhighlighter.cpp \
    src/systemtheme.cpp \
    src/windowmanager.cpp

RESOURCES += src/resources.qrc
