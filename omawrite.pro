QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2
# The desktop portal that reports the colour scheme and text size is Linux-only.
!macx: QT += dbus

CONFIG += c++17 release
TARGET = omawrite
TEMPLATE = app

macx {
    TARGET = Omawrite
    VERSION = 0.7.0
    QMAKE_INFO_PLIST = macos/Info.plist
    ICON = macos/omawrite.icns
}

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/markdownhighlighter.h \
    src/systemtheme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/markdownhighlighter.cpp \
    src/systemtheme.cpp

RESOURCES += src/resources.qrc
