#include "core/agentcontroller.h"

#include "core/approvalinterceptor.h"
#include "core/conversationstore.h"
#include "core/promptbuilder.h"
#include "creator/designerbridge.h"
#include "creator/projectservice.h"
#include "llm/llmclient.h"
#include "settings/agentsettings.h"
#include "tools/tooldispatcher.h"
#include "tools/toolregistry.h"

#include <QJsonObject>
#include <QMetaObject>

namespace AiAssistant {

namespace {

QString shortSummary(const QString &text, int maxChars = 140)
{
    QString flat = text.trimmed();
    const int newline = flat.indexOf(QLatin1Char('\n'));
    if (newline > 0)
        flat = flat.left(newline);
    if (flat.size() > maxChars)
        flat = flat.left(maxChars) + QStringLiteral("…");
    return flat;
}

} // namespace

AgentController::AgentController(QObject *parent)
    : QObject(parent)
{
}

void AgentController::setServices(LlmClient *llm, ToolDispatcher *dispatcher, ToolRegistry *registry,
                                  ApprovalInterceptor *interceptor, PromptBuilder *prompt,
                                  ConversationStore *store, ProjectService *project,
                                  DesignerBridge *designer, AgentSettings *settings)
{
    m_llm = llm;
    m_dispatcher = dispatcher;
    m_registry = registry;
    m_interceptor = interceptor;
    m_prompt = prompt;
    m_store = store;
    m_project = project;
    m_designer = designer;
    m_settings = settings;

    if (m_llm) {
        connect(m_llm, &LlmClient::streamDelta, this, &AgentController::onStreamDelta);
        connect(m_llm, &LlmClient::completed, this, &AgentController::onCompleted);
        connect(m_llm, &LlmClient::failed, this, &AgentController::onFailed);
        connect(m_llm, &LlmClient::canceled, this, &AgentController::onCanceled);
    }
    if (m_dispatcher) {
        connect(m_dispatcher, &ToolDispatcher::asyncToolResult, this,
                &AgentController::onAsyncToolResult);
    }
}

void AgentController::setState(AgentState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void AgentController::appendMessage(const ChatMessage &message)
{
    m_history.append(message);
    emit messageAdded(message);
}

void AgentController::saveHistory()
{
    if (m_store)
        m_store->save(m_history);
}

void AgentController::sendUserMessage(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;

    if (m_state != AgentState::Idle) {
        emit toolLog(QStringLiteral("warn"),
                     tr("上一个任务还没结束（当前状态：%1），请先点击“停止Agent”")
                         .arg(agentStateDisplayName(m_state)));
        return;
    }

    appendMessage(ChatMessage::make(ChatRole::User, trimmed));
    m_pendingCalls.clear();
    m_toolRounds = 0;
    m_streaming = false;
    setState(AgentState::Thinking);
    requestLlm();
}

void AgentController::requestLlm()
{
    if (!m_llm || !m_prompt || !m_settings) {
        emit errorOccurred(tr("Agent 未正确初始化（LLM/提示词为空）"));
        setState(AgentState::Idle);
        return;
    }

    const ProjectSnapshot project = m_project ? m_project->snapshot() : ProjectSnapshot();
    const QJsonObject designerInfo = m_designer ? m_designer->selectedWidget() : QJsonObject();

    QVector<ChatMessage> messages;
    messages.append(ChatMessage::make(ChatRole::System,
                                      m_prompt->buildSystemMessage(project, designerInfo)));
    messages += PromptBuilder::compact(m_history, m_settings->contextCharBudget);

    m_llm->send(messages, m_registry ? m_registry->specs() : QVector<ToolSpec>());
}

void AgentController::onStreamDelta(const QString &text)
{
    if (text.isEmpty())
        return;
    if (!m_streaming) {
        m_streaming = true;
        emit streamStarted();
    }
    emit streamText(text);
}

void AgentController::onCompleted(const QString &assistantText,
                                  const QVector<AiAssistant::ToolCall> &toolCalls,
                                  const QString &finishReason)
{
    const bool streamed = m_streaming;
    if (m_streaming) {
        m_streaming = false;
        emit streamFinished();
    }
    if (m_state == AgentState::Idle)
        return;   // 已被取消，迟到的响应直接丢弃

    if (!assistantText.trimmed().isEmpty() || !toolCalls.isEmpty()) {
        ChatMessage assistant = ChatMessage::make(ChatRole::Assistant, assistantText);
        assistant.toolCalls = toolCalls;
        assistant.streamed = streamed && !assistantText.trimmed().isEmpty();
        appendMessage(assistant);
    } else {
        emit toolLog(QStringLiteral("warn"), tr("模型返回了空响应"));
    }

    if (finishReason == QLatin1String("text_fallback")) {
        emit toolLog(QStringLiteral("warn"),
                     tr("模型没有返回结构化 tool_calls，已从文本中恢复 %1 个工具调用（同样需要审批）")
                         .arg(toolCalls.size()));
    } else if (finishReason == QLatin1String("timeout_partial")) {
        emit toolLog(QStringLiteral("warn"), tr("请求超时，已按收到的部分结果继续"));
    } else if (finishReason == QLatin1String("partial")) {
        emit toolLog(QStringLiteral("warn"), tr("连接中断，已按收到的部分结果继续"));
    }

    if (toolCalls.isEmpty()) {
        setState(AgentState::Idle);
        saveHistory();
        return;
    }

    m_pendingCalls = toolCalls;
    setState(AgentState::Continue);
    QMetaObject::invokeMethod(this, "step", Qt::QueuedConnection);
}

void AgentController::onFailed(const QString &error)
{
    if (m_streaming) {
        m_streaming = false;
        emit streamFinished();
    }

    ChatMessage message = ChatMessage::make(ChatRole::System, tr("LLM 请求失败：%1").arg(error));
    message.isError = true;
    appendMessage(message);
    emit errorOccurred(error);

    m_pendingCalls.clear();
    setState(AgentState::Idle);
    saveHistory();
}

void AgentController::onCanceled()
{
    if (m_streaming) {
        m_streaming = false;
        emit streamFinished();
    }
    m_pendingCalls.clear();
    setState(AgentState::Idle);
}

void AgentController::onAsyncToolResult(const AiAssistant::ToolResult &result)
{
    if (m_state == AgentState::Idle)
        return;   // 已取消：异步结果直接丢弃

    ToolCall call;
    call.id = result.callId;
    call.name = result.toolName;
    reportToolResult(call, result);

    setState(AgentState::Continue);
    QMetaObject::invokeMethod(this, "step", Qt::QueuedConnection);
}

void AgentController::reportToolResult(const ToolCall &call, const ToolResult &result)
{
    ChatMessage message = ChatMessage::make(ChatRole::Tool, result.toContextText());
    message.toolCallId = call.id;
    message.toolName = call.name.isEmpty() ? result.toolName : call.name;
    message.isError = !result.success;
    message.isToolLog = true;
    appendMessage(message);   // Dock 会把它渲染成一行“工具 xxx → …”，无需再额外发 toolLog
}

void AgentController::step()
{
    if (!m_settings || !m_dispatcher || !m_registry)
        return;
    if (m_state != AgentState::Continue && m_state != AgentState::ExecTool)
        return;   // Thinking / WaitUserApprove / Idle：等待外部事件

    // 构建/运行是异步的：结果回来之前不要继续 drive，否则会提前发起下一轮 LLM 请求
    if (m_state == AgentState::ExecTool && m_dispatcher->hasPendingAsync())
        return;

    if (m_toolRounds > m_settings->maxToolIterations) {
        ChatMessage message = ChatMessage::make(
            ChatRole::System,
            tr("工具调用轮次超过上限（%1），已停止以避免无限循环。可以让 AI 换个思路再试。")
                .arg(m_settings->maxToolIterations));
        message.isError = true;
        appendMessage(message);
        m_pendingCalls.clear();
        setState(AgentState::Idle);
        saveHistory();
        return;
    }

    if (m_pendingCalls.isEmpty()) {
        ++m_toolRounds;
        setState(AgentState::Thinking);
        requestLlm();
        return;
    }

    // ---- 1) 只读工具：直接执行 ----
    const ToolCall head = m_pendingCalls.first();
    if (!ApprovalInterceptor::requiresApproval(head.name, m_registry->isReadOnly(head.name))) {
        m_pendingCalls.removeFirst();
        setState(AgentState::ExecTool);
        const ToolResult result = m_dispatcher->executeReadOnly(head);
        reportToolResult(head, result);
        setState(AgentState::Continue);
        QMetaObject::invokeMethod(this, "step", Qt::QueuedConnection);
        return;
    }

    // ---- 2) 修改类工具：合并连续的修改类调用成一张审批单 ----
    QVector<ToolCall> batch;
    while (!m_pendingCalls.isEmpty()) {
        const ToolCall next = m_pendingCalls.first();
        if (!ApprovalInterceptor::requiresApproval(next.name, m_registry->isReadOnly(next.name)))
            break;
        batch.append(next);
        m_pendingCalls.removeFirst();
    }
    if (batch.isEmpty()) {
        QMetaObject::invokeMethod(this, "step", Qt::QueuedConnection);
        return;
    }

    setState(AgentState::WaitUserApprove);
    emit toolLog(QStringLiteral("info"),
                 tr("有 %1 个修改类操作需要人工审批…").arg(batch.size()));

    ApprovalRequest request;
    QString prepareError;
    if (!m_dispatcher->prepareWrites(batch, &request, &prepareError)) {
        // 准备失败（参数非法/文件不存在等）：不弹审批，直接把错误回填给模型
        for (const ToolCall &call : batch)
            reportToolResult(call, ToolResult::fail(prepareError, false));
        setState(AgentState::Continue);
        QMetaObject::invokeMethod(this, "step", Qt::QueuedConnection);
        return;
    }

    if (!m_interceptor) {
        // 理论不可达：没有拦截器就绝不执行
        for (const ToolCall &call : batch)
            reportToolResult(call, ToolResult::fail(tr("审批拦截器缺失，操作已取消"), false));
        setState(AgentState::Continue);
        QMetaObject::invokeMethod(this, "step", Qt::QueuedConnection);
        return;
    }

    const ApprovalDecision decision = m_interceptor->review(request);

    if (!decision.execute()) {
        QString reason = tr("用户拒绝执行该操作，请更换方案或询问其他思路");
        if (!decision.comment.trimmed().isEmpty())
            reason += tr("（用户备注：%1）").arg(decision.comment.trimmed());

        emit approvalRecord(tr("已拒绝：%1").arg(request.title));
        for (const ToolCall &call : batch)
            reportToolResult(call, ToolResult::fail(reason, false));

        setState(AgentState::Continue);
        QMetaObject::invokeMethod(this, "step", Qt::QueuedConnection);
        return;
    }

    // ---- 3) 审批通过（可能带用户修改后的参数）----
    ApprovalRequest approved = request;
    if (decision.kind == ApprovalDecision::Modified) {
        ApprovalRequest rePrepared;
        QString rePrepareError;
        if (!m_dispatcher->prepareWrites(decision.calls, &rePrepared, &rePrepareError)) {
            for (const ToolCall &call : decision.calls) {
                reportToolResult(call,
                                 ToolResult::fail(tr("按修改后的参数重新准备失败：%1").arg(rePrepareError),
                                                  false));
            }
            setState(AgentState::Continue);
            QMetaObject::invokeMethod(this, "step", Qt::QueuedConnection);
            return;
        }
        approved = rePrepared;
        emit approvalRecord(tr("已按修改后的参数执行：%1").arg(approved.title));
    } else {
        emit approvalRecord(tr("已批准：%1").arg(approved.title));
    }

    setState(AgentState::ExecTool);
    const ToolDispatcher::ApplyOutcome outcome = m_dispatcher->apply(approved);

    for (int i = 0; i < outcome.results.size(); ++i) {
        const ToolResult &result = outcome.results.at(i);
        if (result.pending) {
            emit toolLog(QStringLiteral("info"),
                         tr("%1 已启动，等待异步结果…").arg(result.toolName));
            continue;
        }
        ToolCall call = (i < approved.calls.size()) ? approved.calls.at(i) : ToolCall();
        if (call.id.isEmpty())
            call.id = result.callId;
        if (call.name.isEmpty())
            call.name = result.toolName;
        reportToolResult(call, result);
    }

    if (outcome.pending)
        return;   // 保持 ExecTool，等待 asyncToolResult

    if (!outcome.ok && !outcome.error.isEmpty())
        emit errorOccurred(outcome.error);

    setState(AgentState::Continue);
    QMetaObject::invokeMethod(this, "step", Qt::QueuedConnection);
}

void AgentController::cancel()
{
    const bool wasBusy = isBusy();

    m_pendingCalls.clear();
    if (m_dispatcher)
        m_dispatcher->clearPendingAsync();
    if (m_llm && m_llm->isBusy())
        m_llm->cancel();
    if (m_streaming) {
        m_streaming = false;
        emit streamFinished();
    }

    if (wasBusy) {
        ChatMessage message = ChatMessage::make(ChatRole::System, tr("已停止当前任务"));
        appendMessage(message);
    }

    setState(AgentState::Idle);
    saveHistory();
}

void AgentController::clearHistory()
{
    m_history.clear();
    m_pendingCalls.clear();
    if (m_store)
        m_store->clear();
    emit historyCleared();
}

void AgentController::loadHistory()
{
    if (!m_store)
        return;
    QVector<ChatMessage> loaded;
    if (!m_store->load(&loaded))
        return;

    m_history = loaded;
    emit historyCleared();
    for (const ChatMessage &message : m_history)
        emit messageAdded(message);
}

void AgentController::refreshContext()
{
    if (!m_project) {
        emit toolLog(QStringLiteral("warn"), tr("项目服务不可用"));
        return;
    }
    const ProjectSnapshot snapshot = m_project->refresh();
    emit toolLog(QStringLiteral("info"),
                 tr("项目上下文已刷新：%1").arg(shortSummary(snapshot.toSummary(), 200)));
}

} // namespace AiAssistant
