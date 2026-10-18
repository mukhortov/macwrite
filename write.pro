!macx: error("Write builds for macOS only.")

QT = core gui widgets printsupport concurrent

CONFIG += c++17 release
TARGET = Write
TEMPLATE = app

# The one place the version lives: the app reports it and the bundle carries it.
VERSION = 0.7.0
DEFINES += WRITE_VERSION=\\\"$$VERSION\\\"

QMAKE_INFO_PLIST = macos/Info.plist
ICON = macos/write.icns
include(macos/deployment-target.pri)

HEADERS += \
    src/appeventfilter.h \
    src/backend.h \
    src/cli.h \
    src/findbar.h \
    src/macwindow.h \
    src/mainwindow.h \
    src/markdowneditor.h \
    src/markdownedits.h \
    src/markdownhighlighter.h \
    src/plaintextlayout.h \
    src/systemtheme.h \
    src/windowmanager.h

SOURCES += \
    src/main.cpp \
    src/appeventfilter.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/findbar.cpp \
    src/mainwindow.cpp \
    src/markdowneditor.cpp \
    src/markdownedits.cpp \
    src/markdownhighlighter.cpp \
    src/plaintextlayout.cpp \
    src/systemtheme.cpp \
    src/windowmanager.cpp

OBJECTIVE_SOURCES += src/macwindow.mm
LIBS += -framework AppKit

RESOURCES += src/resources.qrc
