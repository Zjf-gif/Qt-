/****************************************************************************
** Agent 任务调度（显式状态机，无嵌套回调）
**
**   Idle → Thinking → (收到 tool_calls) → Continue
**        Continue → 只读工具 → ExecTool → Continue → Thinking …
**        Continue → 修改类工具 → WaitUserApprove → ExecTool → Continue
**        WaitUserApprove（拒绝）→ Continue（把“用户拒绝”回填给模型重新规划）
**
** 关键约束：
**   * 只有本类能调用 ToolDispatcher::apply()，且只在审批通过的分支里调用；
**   * 每一步都通过 Qt::QueuedConnection 重新进入 step()，避免递归过深；
**   * 工具轮次有上限，防止模型死循环。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"

#include <QObject>
#include <QVector>

namespace AiAssistant {

struct AgentSettings;
class ApprovalInterceptor;
class ConversationStore;
class DesignerBridge;
class LlmClient;
class ProjectService;
class PromptBuilder;
class ToolDispatcher;
class ToolRegistry;

class AgentController : public QObject
{
    Q_OBJECT
public:
    explicit AgentController(QObject *parent = nullptr);

    void setServices(LlmClient *llm, ToolDispatcher *dispatcher, ToolRegistry *registry,
                     ApprovalInterceptor *interceptor, PromptBuilder *prompt,
                     ConversationStore *store, ProjectService *project,
                     DesignerBridge *designer, AgentSettings *settings);

    AgentState state() const { return m_state; }
    bool isBusy() const { return m_state != AgentState::Idle; }
    QVector<ChatMessage> history() const { return m_history; }

public slots:
    void sendUserMessage(const QString &text);
    void cancel();
    void clearHistory();
    void loadHistory();
    void refreshContext();
    void step();

signals:
    void stateChanged(AiAssistant::AgentState state);
    void messageAdded(const AiAssistant::ChatMessage &message);
    void streamStarted();
    void streamText(const QString &text);
    void streamFinished();
    void toolLog(const QString &level, const QString &text);
    void errorOccurred(const QString &text);
    void approvalRecord(const QString &text);
    void historyCleared();

private slots:
    void onStreamDelta(const QString &text);
    void onCompleted(const QString &assistantText,
                     const QVector<AiAssistant::ToolCall> &toolCalls,
                     const QString &finishReason);
    void onFailed(const QString &error);
    void onCanceled();
    void onAsyncToolResult(const AiAssistant::ToolResult &result);

private:
    void setState(AgentState state);
    void appendMessage(const ChatMessage &message);
    void requestLlm();
    void reportToolResult(const ToolCall &call, const ToolResult &result);
    void saveHistory();

    LlmClient *m_llm = nullptr;
    ToolDispatcher *m_dispatcher = nullptr;
    ToolRegistry *m_registry = nullptr;
    ApprovalInterceptor *m_interceptor = nullptr;
    PromptBuilder *m_prompt = nullptr;
    ConversationStore *m_store = nullptr;
    ProjectService *m_project = nullptr;
    DesignerBridge *m_designer = nullptr;
    AgentSettings *m_settings = nullptr;

    AgentState m_state = AgentState::Idle;
    QVector<ChatMessage> m_history;
    QVector<ToolCall> m_pendingCalls;
    int m_toolRounds = 0;
    bool m_streaming = false;
};

} // namespace AiAssistant
