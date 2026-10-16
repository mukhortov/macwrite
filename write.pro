!macx: error("Write builds for macOS only.")

QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2

CONFIG += c++17 release
TARGET = Write
TEMPLATE = app

# The one place the version lives: the app reports it and the bundle carries it.
VERSION = 0.7.0
DEFINES += WRITE_VERSION=\\\"$$VERSION\\\"

QMAKE_INFO_PLIST = macos/Info.plist
ICON = macos/write.icns
include(macos/deployment-target.pri)
LIBS += -framework CoreText

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
