QT += core gui quick quickcontrols2 testlib network
CONFIG += c++17 testcase
TARGET = pingmon_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += SOURCE_DIR=\\\"$$PWD/..\\\"

HEADERS += ../src/backend.h ../src/hostmodel.h ../src/theme.h ../src/translator.h
SOURCES += pingmon_tests.cpp ../src/backend.cpp ../src/hostmodel.cpp ../src/theme.cpp ../src/translator.cpp
