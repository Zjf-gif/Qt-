/****************************************************************************
** 代码操作工具（全部必须人工审批）
**   write_file           全量写入（兜底）
**   patch_file           增量补丁（unified diff 或 edits 数组），优先使用
**   add_qt_class         生成 h/cpp 并自动追加 .pro 的 HEADERS/SOURCES
**   insert_code_at_cursor 在 TextEditor 光标处插入代码
****************************************************************************/

#pragma once

#include "tools/toolruntime.h"

#include <QObject>

namespace AiAssistant {

class ToolRegistry;

class CodeTools : public QObject
{
    Q_OBJECT
public:
    explicit CodeTools(ToolRuntime *runtime, QObject *parent = nullptr);

    void registerTools(ToolRegistry *registry);

private:
    ToolRuntime *m_runtime = nullptr;
};

} // namespace AiAssistant
