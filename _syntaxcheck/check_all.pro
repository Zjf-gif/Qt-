# 全量语法检查（用 Creator SDK 桩头文件替代真实 SDK，仅验证插件自身代码一致性）
TEMPLATE = lib
CONFIG += staticlib c++14
CONFIG -= app_bundle
QT += core gui widgets xml uitools network designer

INCLUDEPATH += $$PWD/stubs $$PWD/../src

HEADERS += \
    ../src/aiassistantplugin.h \
    ../src/core/agentcontroller.h \
    ../src/core/agenttypes.h \
    ../src/core/approvalinterceptor.h \
    ../src/core/conversationstore.h \
    ../src/core/promptbuilder.h \
    ../src/core/snapshotmanager.h \
    ../src/creator/buildservice.h \
    ../src/creator/contextmenuhook.h \
    ../src/creator/creatorcompat.h \
    ../src/creator/designerbridge.h \
    ../src/creator/projectservice.h \
    ../src/llm/llmclient.h \
    ../src/llm/sseparser.h \
    ../src/llm/toolcallparser.h \
    ../src/settings/agentsettings.h \
    ../src/tools/buildtools.h \
    ../src/tools/codetools.h \
    ../src/tools/proeditor.h \
    ../src/tools/readtools.h \
    ../src/tools/tooldispatcher.h \
    ../src/tools/toolregistry.h \
    ../src/tools/toolruntime.h \
    ../src/tools/uitools.h \
    ../src/ui/aichatdock.h \
    ../src/ui/approvaldialog.h \
    ../src/ui/diffview.h \
    ../src/ui/settingsdialog.h \
    ../src/ui/uipreviewwidget.h \
    ../src/uidom/uidocument.h

SOURCES += \
    ../src/aiassistantplugin.cpp \
    ../src/core/agentcontroller.cpp \
    ../src/core/agenttypes.cpp \
    ../src/core/approvalinterceptor.cpp \
    ../src/core/conversationstore.cpp \
    ../src/core/promptbuilder.cpp \
    ../src/core/snapshotmanager.cpp \
    ../src/creator/buildservice.cpp \
    ../src/creator/contextmenuhook.cpp \
    ../src/creator/creatorcompat.cpp \
    ../src/creator/designerbridge.cpp \
    ../src/creator/projectservice.cpp \
    ../src/llm/llmclient.cpp \
    ../src/llm/sseparser.cpp \
    ../src/llm/toolcallparser.cpp \
    ../src/settings/agentsettings.cpp \
    ../src/tools/buildtools.cpp \
    ../src/tools/codetools.cpp \
    ../src/tools/proeditor.cpp \
    ../src/tools/readtools.cpp \
    ../src/tools/tooldispatcher.cpp \
    ../src/tools/toolregistry.cpp \
    ../src/tools/uitools.cpp \
    ../src/ui/aichatdock.cpp \
    ../src/ui/approvaldialog.cpp \
    ../src/ui/diffview.cpp \
    ../src/ui/settingsdialog.cpp \
    ../src/ui/uipreviewwidget.cpp \
    ../src/uidom/uidocument.cpp
