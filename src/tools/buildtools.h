/****************************************************************************
** 构建 / 运行工具（必须人工审批）
**   build_project : 异步触发 Creator 构建，构建结束由 BuildService 回填结果
**   run_project   : 触发运行并采集应用输出
****************************************************************************/

#pragma once

#include "tools/toolruntime.h"

#include <QObject>

namespace AiAssistant {

class ToolRegistry;

class BuildTools : public QObject
{
    Q_OBJECT
public:
    explicit BuildTools(ToolRuntime *runtime, QObject *parent = nullptr);

    void registerTools(ToolRegistry *registry);

private:
    ToolRuntime *m_runtime = nullptr;
};

} // namespace AiAssistant
