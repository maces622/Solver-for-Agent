QT += core gui widgets

CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = chat_panel_smoke_test

INCLUDEPATH += ../..

SOURCES += \
    chat_panel_smoke_test.cpp \
    ../../src/chat/acp_client.cpp \
    ../../src/chat/chat_panel.cpp

HEADERS += \
    ../../src/chat/acp_client.h \
    ../../src/chat/chat_panel.h
