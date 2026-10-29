/****************************************************************************
** 纯文本工具调用恢复（容错降级）
**
** 场景：模型/网关不支持 tool_calls，或 SSE 解析失败后模型只吐出文本。
** 我们仍然从文本里尽量恢复出结构化调用 —— 恢复出来的调用 **同样要过审批**，
** 绝不是“直接执行”。完全无法恢复时才作为普通文本展示给用户。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace AiAssistant {

class ToolCallParser
{
public:
    // 从文本中提取 ```json {..} ```、<tool_call>{..}</tool_call>、裸 JSON 对象
    static QVector<ToolCall> extractFromText(const QString &text, QString *residualText);

    // 容错解析参数 JSON（去围栏、去尾随逗号）
    static bool parseArguments(const QString &raw, QJsonObject *out);

    static QString sanitizeJson(QString raw);
};

} // namespace AiAssistant
