/****************************************************************************
** 工具执行器（两阶段：prepare → apply）
**
**   prepareWrites()：在内存叠加层里把所有修改算出来，产出 ApprovalRequest
**                    （含每个文件的 before/after，用于 Diff 与 UI 预览）
**   apply()        ：只有拿到审批结果（Approved/Modified）后才会被调用：
**                    自动打快照 → 写盘 → 通知 Creator 刷新 → 执行构建/运行
**
** 全插件只有 AgentController 调用 apply()，且只会在审批通过分支里调用；
** 不存在任何“跳过审批直接 apply”的代码路径。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"
#include "tools/toolruntime.h"

#include <QObject>
#include <QStringList>
#include <QVector>

namespace AiAssistant {

class ToolRegistry;

class ToolDispatcher : public QObject
{
    Q_OBJECT
public:
    struct ApplyOutcome
    {
        bool ok = true;
        QString error;
        QVector<ToolResult> results;      // 与 request.plans 一一对应
        bool pending = false;             // 构建/运行：等待异步回填
        QString pendingCallId;
        QString pendingToolName;
        QStringList changedFiles;
        QString snapshotBatchId;
    };

    explicit ToolDispatcher(QObject *parent = nullptr);

    void setRuntime(ToolRuntime *runtime) { m_runtime = runtime; }
    void setRegistry(ToolRegistry *registry) { m_registry = registry; }

    ToolResult executeReadOnly(const ToolCall &call);
    bool prepareWrites(const QVector<ToolCall> &calls, ApprovalRequest *request, QString *error);
    ApplyOutcome apply(const ApprovalRequest &request);

    // 异步动作（构建/运行）结束后由插件调用，把结果回填给 Agent
    bool hasPendingAsync() const { return !m_pendingCallId.isEmpty(); }
    void completeAsyncTool(ToolResult result);
    void clearPendingAsync();

signals:
    void filesChanged(const QStringList &paths);
    void asyncToolResult(const AiAssistant::ToolResult &result);
    void log(const QString &level, const QString &text);

private:
    void logMessage(const QString &level, const QString &text);

    ToolRuntime *m_runtime = nullptr;
    ToolRegistry *m_registry = nullptr;
    QString m_pendingCallId;
    QString m_pendingToolName;
};

} // namespace AiAssistant
