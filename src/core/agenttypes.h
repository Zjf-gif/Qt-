/****************************************************************************
**
** AI Assistant Plugin for Qt Creator 4.15.0
** 核心数据类型：工具调用、工具结果、对话消息、审批请求/结果、Agent 状态。
**
** 设计要点：
**  1) ToolPlan 是“修改类操作”的唯一中间表示：先 prepare（纯计算，不落盘），
**     经审批拦截器人工确认后才允许 apply，从结构上保证“无审批不写盘”。
**  2) FileChange 同时保存修改前/后内容，审批弹窗的 Diff 与写入时的快照都基于它。
**
****************************************************************************/

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace AiAssistant {

// ---------------------------------------------------------------------------
// 工具调用
// ---------------------------------------------------------------------------
struct ToolCall
{
    QString id;              // LLM 侧 call id（OpenAI 协议必需）
    QString name;            // 工具名
    QJsonObject arguments;   // 参数
    QString rawArguments;    // 原始 JSON 文本（容错解析用）

    bool isValid() const { return !name.isEmpty(); }
    QString argumentsText() const;   // 美化后的 JSON，用于审批弹窗
};

// ---------------------------------------------------------------------------
// 工具结果
// ---------------------------------------------------------------------------
struct ToolResult
{
    bool success = true;
    bool readOnly = true;    // 只读工具直接回填，修改类工具必须经审批
    QString content;         // 给 LLM 看的正文
    QString error;           // 失败原因
    QString callId;          // 对应 ToolCall::id
    QString toolName;
    bool pending = false;    // true: 异步动作已启动（构建/运行），稍后回填

    static ToolResult ok(const QString &content, bool readOnly = true);
    static ToolResult fail(const QString &error, bool readOnly = true);
    QString toContextText() const;
};

// ---------------------------------------------------------------------------
// 对话消息
// ---------------------------------------------------------------------------
enum class ChatRole {
    System = 0,
    User,
    Assistant,
    Tool
};

QString chatRoleName(ChatRole role);

struct ChatMessage
{
    ChatRole role = ChatRole::User;
    QString content;
    QVector<ToolCall> toolCalls;   // Assistant 消息携带
    QString toolCallId;            // Tool 消息对应
    QString toolName;
    QDateTime timeStamp;
    bool isError = false;
    bool isApprovalNote = false;
    bool isToolLog = false;
    bool streamed = false;   // 已经通过流式增量显示过，Dock 不要重复渲染正文

    bool isEmpty() const;
    static ChatMessage make(ChatRole role, const QString &text);
};

// ---------------------------------------------------------------------------
// 文件变更（审批 + 快照 + 写入 三者共用的原子单位）
// ---------------------------------------------------------------------------
struct FileChange
{
    QString path;
    QByteArray before;            // 磁盘上的原始内容
    QByteArray after;             // 审批通过后写入的内容
    bool existedBefore = true;
    bool deleteFile = false;

    bool contentsDiffer() const;
};

// ---------------------------------------------------------------------------
// 工具描述（下发给 LLM 的 function schema）
// ---------------------------------------------------------------------------
struct ToolSpec
{
    QString name;
    QString description;
    QJsonObject parameters;   // JSON Schema
    bool readOnly = false;    // 硬编码于注册处，任何地方都不能改

    QJsonObject toJson() const;
};

// ---------------------------------------------------------------------------
// 工具执行计划（修改类工具 prepare 阶段产物）
// ---------------------------------------------------------------------------
struct ToolPlan
{
    QString toolName;
    ToolCall call;
    bool ok = true;
    QString error;
    QString title;             // 审批弹窗标题
    QString summary;           // 一句话描述
    QString kind;              // ui / code / project / editor / build / run
    QStringList changedFiles;  // 本次涉及的路径（可能是叠加层里的虚拟内容）
    bool externalAction = false;  // 构建/运行/编辑器插入：无文件写入，但有副作用
    QJsonObject payload;       // 动作附加参数（构建目标、编辑器插入文本等）
};

// ---------------------------------------------------------------------------
// 审批请求 / 审批结果
// ---------------------------------------------------------------------------
struct ApprovalRequest
{
    QString title;
    QString kind;
    QString summary;
    QVector<ToolCall> calls;
    QVector<ToolPlan> plans;
    QVector<FileChange> changes;   // 聚合后的最终变更（按路径去重）

    bool isValid() const { return !calls.isEmpty(); }
};

struct ApprovalDecision
{
    enum Kind {
        Rejected = 0,   // 拒绝本次操作
        Approved = 1,   // 同意执行
        Modified = 2    // 修改指令（参数）后执行
    };

    Kind kind = Rejected;
    QVector<ToolCall> calls;   // 可能是用户改过参数后的调用
    QString comment;           // 反馈给 LLM 的备注

    bool execute() const { return kind == Approved || kind == Modified; }
};

// ---------------------------------------------------------------------------
// Agent 状态机
// ---------------------------------------------------------------------------
enum class AgentState {
    Idle = 0,
    Thinking,
    WaitUserApprove,
    ExecTool,
    Continue
};

QString agentStateDisplayName(AgentState state);

void registerMetaTypes();

} // namespace AiAssistant

Q_DECLARE_METATYPE(AiAssistant::ToolCall)
Q_DECLARE_METATYPE(AiAssistant::ToolResult)
Q_DECLARE_METATYPE(AiAssistant::ChatMessage)
Q_DECLARE_METATYPE(AiAssistant::ToolPlan)
Q_DECLARE_METATYPE(AiAssistant::ApprovalRequest)
Q_DECLARE_METATYPE(AiAssistant::ApprovalDecision)
Q_DECLARE_METATYPE(AiAssistant::AgentState)
Q_DECLARE_METATYPE(QVector<AiAssistant::ToolCall>)
Q_DECLARE_METATYPE(QVector<AiAssistant::ChatMessage>)
