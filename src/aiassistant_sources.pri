# AI Assistant 插件 —— 源文件清单（被 src/aiassistant.pro 与根 aiassistant.pro 共用）
#
# $$PWD 指本文件所在目录（src/），所以插件内部统一使用
#   #include "core/xxx.h" / #include "creator/xxx.h" / #include "ui/xxx.h"
# 这类相对 src/ 的包含路径。

QT += core gui widgets network xml uitools designer

CONFIG -= app_bundle

# 本插件的源码含中文注释/字符串，统一按 UTF-8 处理。
# 不加这个开关时，MSVC 在中文 Windows 上会按 GBK(936) 解释源码，
# 多字节中文会吃掉换行/引号，导致大量莫名其妙的语法错误（C2079/C2146 等）。
msvc: QMAKE_CXXFLAGS += /utf-8

# Creator 的插件默认复用共享 PCH（src/shared/qtcreator_gui_pch.h），而该 PCH 是在
# 不带 /utf-8 的前提下生成的；与本插件的 /utf-8 冲突会报 C2855
# （命令行选项 "/source-charset" 与预编译头不一致），因此本插件关闭 PCH。
PRECOMPILED_HEADER =
CONFIG -= precompile_header

INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/aiassistantplugin.h \
    $$PWD/constants.h \
    $$PWD/core/agentcontroller.h \
    $$PWD/core/agenttypes.h \
    $$PWD/core/approvalinterceptor.h \
    $$PWD/core/conversationstore.h \
    $$PWD/core/promptbuilder.h \
    $$PWD/core/snapshotmanager.h \
    $$PWD/creator/buildservice.h \
    $$PWD/creator/contextmenuhook.h \
    $$PWD/creator/creatorcompat.h \
    $$PWD/creator/designerbridge.h \
    $$PWD/creator/projectservice.h \
    $$PWD/llm/llmclient.h \
    $$PWD/llm/sseparser.h \
    $$PWD/llm/toolcallparser.h \
    $$PWD/settings/agentsettings.h \
    $$PWD/tools/buildtools.h \
    $$PWD/tools/codetools.h \
    $$PWD/tools/proeditor.h \
    $$PWD/tools/readtools.h \
    $$PWD/tools/tooldispatcher.h \
    $$PWD/tools/toolregistry.h \
    $$PWD/tools/toolruntime.h \
    $$PWD/tools/toolschema.h \
    $$PWD/tools/uitools.h \
    $$PWD/ui/aichatdock.h \
    $$PWD/ui/approvaldialog.h \
    $$PWD/ui/diffview.h \
    $$PWD/ui/settingsdialog.h \
    $$PWD/ui/uipreviewwidget.h \
    $$PWD/uidom/uidocument.h

SOURCES += \
    $$PWD/aiassistantplugin.cpp \
    $$PWD/core/agentcontroller.cpp \
    $$PWD/core/agenttypes.cpp \
    $$PWD/core/approvalinterceptor.cpp \
    $$PWD/core/conversationstore.cpp \
    $$PWD/core/promptbuilder.cpp \
    $$PWD/core/snapshotmanager.cpp \
    $$PWD/creator/buildservice.cpp \
    $$PWD/creator/contextmenuhook.cpp \
    $$PWD/creator/creatorcompat.cpp \
    $$PWD/creator/designerbridge.cpp \
    $$PWD/creator/projectservice.cpp \
    $$PWD/llm/llmclient.cpp \
    $$PWD/llm/sseparser.cpp \
    $$PWD/llm/toolcallparser.cpp \
    $$PWD/settings/agentsettings.cpp \
    $$PWD/tools/buildtools.cpp \
    $$PWD/tools/codetools.cpp \
    $$PWD/tools/proeditor.cpp \
    $$PWD/tools/readtools.cpp \
    $$PWD/tools/tooldispatcher.cpp \
    $$PWD/tools/toolregistry.cpp \
    $$PWD/tools/uitools.cpp \
    $$PWD/ui/aichatdock.cpp \
    $$PWD/ui/approvaldialog.cpp \
    $$PWD/ui/diffview.cpp \
    $$PWD/ui/settingsdialog.cpp \
    $$PWD/ui/uipreviewwidget.cpp \
    $$PWD/uidom/uidocument.cpp

# 插件描述文件（AiAssistant.json.in）由 qtcreatorplugin.pri 负责替换与登记，这里无需列出
