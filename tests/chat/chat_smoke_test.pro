QT += core

CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = chat_smoke_test

INCLUDEPATH += ../..

SOURCES += \
    chat_smoke_test.cpp \
    ../../src/chat/acp_client.cpp

HEADERS += \
    ../../src/chat/acp_client.h
