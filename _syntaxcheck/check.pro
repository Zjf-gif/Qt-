# 仅用于本地“能编译的部分”语法检查（不属于交付工程）
TEMPLATE = lib
CONFIG += staticlib c++14
CONFIG -= app_bundle
QT += core gui widgets xml uitools network

INCLUDEPATH += $$PWD/../src

SOURCES += \
    ../src/core/agenttypes.cpp \
    ../src/core/approvalinterceptor.cpp \
    ../src/core/conversationstore.cpp \
    ../src/core/snapshotmanager.cpp \
    ../src/llm/llmclient.cpp \
    ../src/llm/sseparser.cpp \
    ../src/llm/toolcallparser.cpp \
    ../src/settings/agentsettings.cpp \
    ../src/ui/aichatdock.cpp \
    ../src/ui/approvaldialog.cpp \
    ../src/ui/diffview.cpp \
    ../src/ui/settingsdialog.cpp \
    ../src/ui/uipreviewwidget.cpp \
    ../src/uidom/uidocument.cpp

HEADERS += \
    ../src/core/agenttypes.h
