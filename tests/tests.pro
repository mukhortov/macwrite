QT = core gui widgets printsupport concurrent testlib
CONFIG += testcase c++17
CONFIG -= app_bundle
include(../macos/deployment-target.pri)
TEMPLATE = app
TARGET = tst_write

INCLUDEPATH += ../src
SOURCES += \
    tst_write.cpp \
    ../src/appeventfilter.cpp \
    ../src/backend.cpp \
    ../src/cli.cpp \
    ../src/findbar.cpp \
    ../src/mainwindow.cpp \
    ../src/markdowneditor.cpp \
    ../src/markdownedits.cpp \
    ../src/markdownhighlighter.cpp \
    ../src/plaintextlayout.cpp \
    ../src/windowmanager.cpp
HEADERS += \
    ../src/appeventfilter.h \
    ../src/backend.h \
    ../src/cli.h \
    ../src/findbar.h \
    ../src/macwindow.h \
    ../src/mainwindow.h \
    ../src/markdowneditor.h \
    ../src/markdownedits.h \
    ../src/markdownhighlighter.h \
    ../src/plaintextlayout.h \
    ../src/windowmanager.h


OBJECTIVE_SOURCES += ../src/macwindow.mm
LIBS += -framework AppKit
