QT += core gui quick testlib
CONFIG += testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_macwrite

INCLUDEPATH += ../src
SOURCES += \
    tst_macwrite.cpp \
    ../src/appeventfilter.cpp \
    ../src/backend.cpp \
    ../src/cli.cpp \
    ../src/markdownhighlighter.cpp \
    ../src/windowmanager.cpp
HEADERS += \
    ../src/appeventfilter.h \
    ../src/backend.h \
    ../src/cli.h \
    ../src/markdownhighlighter.h \
    ../src/windowmanager.h

QT += widgets printsupport quickcontrols2 quickdialogs2
