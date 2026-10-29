/****************************************************************************
** 工具注册表
**
** 每个工具在这里同时登记：
**   * ToolSpec（下发给 LLM 的 JSON Schema，含“是否只读”）
**   * 执行体（只读：ReadHandler；修改类：PrepareHandler，产出 ToolPlan）
**
** 注意：registry.isReadOnly() 只是辅助信息，最终是否审批由
** ApprovalInterceptor::requiresApproval() 决定（它默认拒绝未知工具）。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"
#include "tools/toolruntime.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace AiAssistant {

class ToolRegistry
{
public:
    using ReadHandler = std::function<ToolResult(const ToolCall &, PrepareContext &)>;
    using PrepareHandler = std::function<ToolPlan(const ToolCall &, PrepareContext &)>;

    bool addReadTool(const ToolSpec &spec, ReadHandler handler);
    bool addWriteTool(const ToolSpec &spec, PrepareHandler handler);

    QVector<ToolSpec> specs() const;
    QStringList names() const;
    bool contains(const QString &name) const;
    bool isReadOnly(const QString &name) const;   // 未注册 → false（保守）
    const ToolSpec *spec(const QString &name) const;

    ToolResult executeRead(const ToolCall &call, PrepareContext &context) const;
    ToolPlan prepareWrite(const ToolCall &call, PrepareContext &context) const;

    static ToolSpec makeSpec(const QString &name, const QString &description,
                             const QJsonObject &parameters, bool readOnly);

private:
    struct Entry
    {
        ToolSpec spec;
        ReadHandler read;
        PrepareHandler prepare;
    };

    QHash<QString, Entry> m_entries;
    QStringList m_order;
};

} // namespace AiAssistant
