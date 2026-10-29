#include "core/agenttypes.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonValue>

namespace AiAssistant {

QString ToolCall::argumentsText() const
{
    if (!arguments.isEmpty())
        return QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Indented)).trimmed();
    if (!rawArguments.isEmpty()) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(rawArguments.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).trimmed();
        return rawArguments;
    }
    return QStringLiteral("{}");
}

ToolResult ToolResult::ok(const QString &text, bool ro)
{
    ToolResult r;
    r.success = true;
    r.readOnly = ro;
    r.content = text;
    return r;
}

ToolResult ToolResult::fail(const QString &error, bool ro)
{
    ToolResult r;
    r.success = false;
    r.readOnly = ro;
    r.error = error;
    r.content = error;
    return r;
}

QString ToolResult::toContextText() const
{
    QString text;
    if (!success)
        text = QStringLiteral("[工具执行失败] ") + (error.isEmpty() ? QStringLiteral("未知错误") : error);
    else
        text = content;
    if (pending)
        text = QStringLiteral("[异步动作已启动] ") + text;
    return text;
}

QString chatRoleName(ChatRole role)
{
    switch (role) {
    case ChatRole::System:
        return QStringLiteral("system");
    case ChatRole::User:
        return QStringLiteral("user");
    case ChatRole::Assistant:
        return QStringLiteral("assistant");
    case ChatRole::Tool:
        return QStringLiteral("tool");
    }
    return QStringLiteral("user");
}

bool ChatMessage::isEmpty() const
{
    return content.trimmed().isEmpty() && toolCalls.isEmpty();
}

ChatMessage ChatMessage::make(ChatRole role, const QString &text)
{
    ChatMessage msg;
    msg.role = role;
    msg.content = text;
    msg.timeStamp = QDateTime::currentDateTime();
    return msg;
}

bool FileChange::contentsDiffer() const
{
    if (deleteFile)
        return existedBefore || !before.isEmpty();
    return before != after;
}

QJsonObject ToolSpec::toJson() const
{
    QJsonObject params = parameters;
    if (params.isEmpty()) {
        params.insert(QStringLiteral("type"), QStringLiteral("object"));
        params.insert(QStringLiteral("properties"), QJsonObject());
    }

    QJsonObject function;
    function.insert(QStringLiteral("name"), name);
    function.insert(QStringLiteral("description"), description);
    function.insert(QStringLiteral("parameters"), params);

    QJsonObject tool;
    tool.insert(QStringLiteral("type"), QStringLiteral("function"));
    tool.insert(QStringLiteral("function"), function);
    return tool;
}

QString agentStateDisplayName(AgentState state)
{
    switch (state) {
    case AgentState::Idle:
        return QCoreApplication::translate("AiAssistant", "空闲");
    case AgentState::Thinking:
        return QCoreApplication::translate("AiAssistant", "思考中");
    case AgentState::WaitUserApprove:
        return QCoreApplication::translate("AiAssistant", "等待人工审批");
    case AgentState::ExecTool:
        return QCoreApplication::translate("AiAssistant", "执行工具中");
    case AgentState::Continue:
        return QCoreApplication::translate("AiAssistant", "继续推理");
    }
    return QStringLiteral("?");
}

void registerMetaTypes()
{
    qRegisterMetaType<AiAssistant::ToolCall>("AiAssistant::ToolCall");
    qRegisterMetaType<AiAssistant::ToolResult>("AiAssistant::ToolResult");
    qRegisterMetaType<AiAssistant::ChatMessage>("AiAssistant::ChatMessage");
    qRegisterMetaType<AiAssistant::ToolPlan>("AiAssistant::ToolPlan");
    qRegisterMetaType<AiAssistant::ApprovalRequest>("AiAssistant::ApprovalRequest");
    qRegisterMetaType<AiAssistant::ApprovalDecision>("AiAssistant::ApprovalDecision");
    qRegisterMetaType<AiAssistant::AgentState>("AiAssistant::AgentState");
    qRegisterMetaType<QVector<AiAssistant::ToolCall>>("QVector<AiAssistant::ToolCall>");
    qRegisterMetaType<QVector<AiAssistant::ChatMessage>>("QVector<AiAssistant::ChatMessage>");
}

} // namespace AiAssistant
