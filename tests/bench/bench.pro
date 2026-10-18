# Times Write's editor on a long document. Not part of bin/test; build and
# run it by hand: qmake && make && ./editorbench FILE [KEYS]
QT += core gui widgets printsupport concurrent quick testlib
CONFIG += c++17 console release
CONFIG -= app_bundle
include(../../macos/deployment-target.pri)
LIBS += -framework CoreText
TEMPLATE = app
TARGET = editorbench

INCLUDEPATH += ../../src
DEFINES += SOURCE_ROOT=\\\"$$clean_path($$PWD/../..)\\\"
SOURCES += \
    editorbench.cpp \
    ../../src/backend.cpp \
    ../../src/markdowneditor.cpp \
    ../../src/markdownhighlighter.cpp \
    ../../src/plaintextlayout.cpp
HEADERS += \
    ../../src/backend.h \
    ../../src/markdowneditor.h \
    ../../src/markdownhighlighter.h \
    ../../src/plaintextlayout.h
