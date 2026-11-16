QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release
TARGET = pingmon
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/hostmodel.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/hostmodel.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
