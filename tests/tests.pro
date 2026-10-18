QT += core gui quick testlib
CONFIG += testcase c++17
CONFIG -= app_bundle
include(../macos/deployment-target.pri)
LIBS += -framework CoreText
TEMPLATE = app
TARGET = tst_write

INCLUDEPATH += ../src
SOURCES += \
    tst_write.cpp \
    ../src/appeventfilter.cpp \
    ../src/backend.cpp \
    ../src/cli.cpp \
    ../src/findbar.cpp \
    ../src/markdownedits.cpp \
    ../src/markdownhighlighter.cpp \
    ../src/plaintextlayout.cpp \
    ../src/windowmanager.cpp
HEADERS += \
    ../src/appeventfilter.h \
    ../src/backend.h \
    ../src/cli.h \
    ../src/findbar.h \
    ../src/markdownedits.h \
    ../src/markdownhighlighter.h \
    ../src/plaintextlayout.h \
    ../src/windowmanager.h

QT += widgets printsupport quickcontrols2 quickdialogs2 concurrent
