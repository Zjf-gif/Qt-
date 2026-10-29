#include "tools/toolregistry.h"

namespace AiAssistant {

ToolSpec ToolRegistry::makeSpec(const QString &name, const QString &description,
                                const QJsonObject &parameters, bool readOnly)
{
    ToolSpec spec;
    spec.name = name;
    spec.description = description;
    spec.parameters = parameters;
    spec.readOnly = readOnly;
    return spec;
}

bool ToolRegistry::addReadTool(const ToolSpec &spec, ReadHandler handler)
{
    if (spec.name.isEmpty() || !handler)
        return false;
    Entry entry;
    entry.spec = spec;
    entry.spec.readOnly = true;   // 强制：只读注册入口不能登记可写工具
    entry.read = std::move(handler);
    if (!m_entries.contains(spec.name))
        m_order.append(spec.name);
    m_entries.insert(spec.name, entry);
    return true;
}

bool ToolRegistry::addWriteTool(const ToolSpec &spec, PrepareHandler handler)
{
    if (spec.name.isEmpty() || !handler)
        return false;
    Entry entry;
    entry.spec = spec;
    entry.spec.readOnly = false;
    entry.prepare = std::move(handler);
    if (!m_entries.contains(spec.name))
        m_order.append(spec.name);
    m_entries.insert(spec.name, entry);
    return true;
}

QVector<ToolSpec> ToolRegistry::specs() const
{
    QVector<ToolSpec> result;
    for (const QString &name : m_order)
        result.append(m_entries.value(name).spec);
    return result;
}

QStringList ToolRegistry::names() const
{
    return m_order;
}

bool ToolRegistry::contains(const QString &name) const
{
    return m_entries.contains(name);
}

bool ToolRegistry::isReadOnly(const QString &name) const
{
    auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd())
        return false;
    return it.value().spec.readOnly;
}

const ToolSpec *ToolRegistry::spec(const QString &name) const
{
    auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd())
        return nullptr;
    return &it.value().spec;
}

ToolResult ToolRegistry::executeRead(const ToolCall &call, PrepareContext &context) const
{
    auto it = m_entries.constFind(call.name);
    if (it == m_entries.constEnd()) {
        ToolResult result = ToolResult::fail(QStringLiteral("未知工具：%1").arg(call.name), true);
        result.callId = call.id;
        result.toolName = call.name;
        return result;
    }
    if (!it.value().read) {
        ToolResult result = ToolResult::fail(QStringLiteral("工具 %1 不是只读工具").arg(call.name), false);
        result.callId = call.id;
        result.toolName = call.name;
        return result;
    }

    ToolResult result = it.value().read(call, context);
    result.callId = call.id;
    result.toolName = call.name;
    return result;
}

ToolPlan ToolRegistry::prepareWrite(const ToolCall &call, PrepareContext &context) const
{
    ToolPlan plan;
    plan.toolName = call.name;
    plan.call = call;

    auto it = m_entries.constFind(call.name);
    if (it == m_entries.constEnd()) {
        plan.ok = false;
        plan.error = QStringLiteral("未知工具：%1").arg(call.name);
        return plan;
    }
    if (!it.value().prepare) {
        plan.ok = false;
        plan.error = QStringLiteral("工具 %1 不是修改类工具").arg(call.name);
        return plan;
    }

    plan = it.value().prepare(call, context);
    plan.toolName = call.name;
    plan.call = call;
    return plan;
}

} // namespace AiAssistant
