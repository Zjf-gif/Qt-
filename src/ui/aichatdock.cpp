#include "ui/aichatdock.h"

#include <QColor>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

namespace AiAssistant {

namespace {

const QColor kUserColor(65, 130, 210);
const QColor kToolColor(135, 135, 135);
const QColor kApprovalColor(200, 140, 20);
const QColor kErrorColor(200, 70, 70);

QString firstLine(const QString &text, int maxChars = 220)
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

AiChatDock::AiChatDock(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(5);

    // ---- 顶部状态 ----
    auto *header = new QHBoxLayout;
    m_stateLabel = new QLabel(this);
    m_stateLabel->setTextFormat(Qt::RichText);
    m_infoLabel = new QLabel(this);
    m_infoLabel->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    m_infoLabel->setWordWrap(true);
    header->addWidget(m_stateLabel);
    header->addSpacing(8);
    header->addWidget(m_infoLabel, 1);
    layout->addLayout(header);

    // ---- 消息流 ----
    m_view = new QTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setAcceptRichText(true);
    m_view->setLineWrapMode(QTextEdit::WidgetWidth);
    layout->addWidget(m_view, 1);

    // ---- 输入 ----
    auto *inputRow = new QHBoxLayout;
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("描述你要做的事，例如：把登录按钮移到右下角并改成“登录”…"));
    m_sendButton = new QPushButton(tr("发送"), this);
    m_sendButton->setAutoDefault(false);
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_sendButton);
    layout->addLayout(inputRow);

    // ---- 操作按钮 ----
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(4);
    m_stopButton = new QPushButton(tr("停止Agent"), this);
    m_snapshotButton = new QPushButton(tr("手动快照"), this);
    m_rollbackButton = new QPushButton(tr("回滚"), this);
    m_refreshButton = new QPushButton(tr("刷新项目上下文"), this);
    m_settingsButton = new QPushButton(tr("打开设置"), this);
    m_clearButton = new QPushButton(tr("清空对话"), this);
    const QList<QPushButton *> all = {m_stopButton, m_snapshotButton, m_rollbackButton,
                                      m_refreshButton, m_settingsButton, m_clearButton};
    for (QPushButton *button : all) {
        button->setAutoDefault(false);
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(m_sendButton, &QPushButton::clicked, this, &AiChatDock::submitInput);
    connect(m_input, &QLineEdit::returnPressed, this, &AiChatDock::submitInput);
    connect(m_stopButton, &QPushButton::clicked, this, &AiChatDock::stopRequested);
    connect(m_snapshotButton, &QPushButton::clicked, this, &AiChatDock::snapshotRequested);
    connect(m_rollbackButton, &QPushButton::clicked, this, &AiChatDock::rollbackRequested);
    connect(m_refreshButton, &QPushButton::clicked, this, &AiChatDock::refreshContextRequested);
    connect(m_settingsButton, &QPushButton::clicked, this, &AiChatDock::settingsRequested);
    connect(m_clearButton, &QPushButton::clicked, this, &AiChatDock::clearHistoryRequested);

    setAgentState(AgentState::Idle);
    updateInfoLabel();

    appendSystemNote(tr("AI 助手已就绪。所有修改类操作（UI / 代码 / 项目 / 构建 / 运行）都会弹出审批窗口，"
                       "审批开关不可关闭；拒绝后 AI 会收到反馈并重新规划。"));
}

void AiChatDock::submitInput()
{
    const QString text = m_input->text().trimmed();
    if (text.isEmpty())
        return;
    m_input->clear();
    emit sendRequested(text);
}

void AiChatDock::setInputText(const QString &text)
{
    m_input->setText(text);
    m_input->setFocus();
    m_input->setCursorPosition(m_input->text().size());
}

void AiChatDock::focusInput()
{
    m_input->setFocus();
}

void AiChatDock::setAgentState(AgentState state)
{
    m_state = state;
    const QString name = agentStateDisplayName(state);
    QColor color(120, 120, 120);
    switch (state) {
    case AgentState::Idle:
        color = QColor(110, 110, 110);
        break;
    case AgentState::Thinking:
    case AgentState::Continue:
        color = QColor(60, 130, 210);
        break;
    case AgentState::WaitUserApprove:
        color = QColor(200, 140, 20);
        break;
    case AgentState::ExecTool:
        color = QColor(70, 170, 110);
        break;
    }
    m_stateLabel->setText(QStringLiteral("<span style=\"color:%1; font-weight:600;\">● %2</span>")
                              .arg(color.name(), name.toHtmlEscaped()));
    updateButtons();
}

void AiChatDock::setSnapshotCount(int count)
{
    m_snapshotCount = count;
    updateInfoLabel();
}

void AiChatDock::setModelInfo(const QString &model, const QString &endpoint)
{
    m_modelInfo = model;
    m_endpointInfo = endpoint;
    updateInfoLabel();
}

void AiChatDock::updateInfoLabel()
{
    if (!m_infoLabel)
        return;
    QStringList parts;
    if (!m_modelInfo.isEmpty())
        parts.append(tr("模型 %1").arg(m_modelInfo));
    if (!m_endpointInfo.isEmpty())
        parts.append(m_endpointInfo);
    parts.append(tr("快照 %1").arg(m_snapshotCount));
    parts.append(tr("审批：强制"));
    m_infoLabel->setText(parts.join(QStringLiteral(" ｜ ")));
}

void AiChatDock::updateButtons()
{
    if (!m_stopButton)
        return;
    m_stopButton->setEnabled(m_state != AgentState::Idle);
}

void AiChatDock::appendLine(const QString &prefix, const QString &text, const QColor &color,
                            bool italic, bool monospace)
{
    QTextCursor cursor(m_view->document());
    cursor.movePosition(QTextCursor::End);

    if (!m_view->document()->isEmpty())
        cursor.insertBlock();

    QTextCharFormat format;
    format.setForeground(color);
    if (italic)
        format.setFontItalic(true);
    if (monospace)
        format.setFontFamily(QFontDatabase::systemFont(QFontDatabase::FixedFont).family());

    if (!prefix.isEmpty())
        cursor.insertText(prefix, format);
    cursor.insertText(text, format);

    m_view->setTextCursor(cursor);
    m_view->ensureCursorVisible();
}

void AiChatDock::appendChatMessage(const AiAssistant::ChatMessage &message)
{
    switch (message.role) {
    case ChatRole::User:
        appendLine(tr("你："), message.content, kUserColor);
        break;
    case ChatRole::Assistant: {
        const QColor textColor = palette().color(QPalette::Text);
        if (message.streamed) {
            // 正文已经通过流式增量显示过，这里只补工具调用清单
            if (!message.toolCalls.isEmpty()) {
                appendLine(QStringLiteral("    · "),
                           tr("请求执行 %1 个工具").arg(message.toolCalls.size()), kToolColor, true);
            }
        } else if (message.content.trimmed().isEmpty() && !message.toolCalls.isEmpty()) {
            appendLine(tr("AI："), tr("（请求执行 %1 个工具）").arg(message.toolCalls.size()), textColor);
        } else {
            appendLine(tr("AI："), message.content, textColor);
        }
        for (const ToolCall &call : message.toolCalls) {
            appendLine(QStringLiteral("    · "),
                       QStringLiteral("%1(%2)").arg(call.name, firstLine(call.argumentsText(), 160)),
                       kToolColor, true, true);
        }
        break;
    }
    case ChatRole::Tool:
        appendLine(tr("工具 ") + (message.toolName.isEmpty() ? QString() : message.toolName + QStringLiteral(" ")),
                   firstLine(message.content, 400),
                   message.isError ? kErrorColor : kToolColor, false, true);
        break;
    case ChatRole::System:
        if (message.isError)
            appendLine(tr("错误："), message.content, kErrorColor, true);
        else
            appendLine(tr("系统："), message.content, kToolColor, true);
        break;
    }
}

void AiChatDock::appendToolLog(const QString &text, const QString &level)
{
    const QColor color = (level == QLatin1String("warn") || level == QLatin1String("error"))
                             ? kErrorColor
                             : kToolColor;
    appendLine(QStringLiteral("[工具] "), text, color, true);
}

void AiChatDock::appendApprovalRecord(const QString &text)
{
    appendLine(QStringLiteral("[审批] "), text, kApprovalColor, true);
}

void AiChatDock::appendError(const QString &text)
{
    appendLine(tr("错误："), text, kErrorColor);
}

void AiChatDock::appendSystemNote(const QString &text)
{
    appendLine(QString(), text, kToolColor, true);
}

void AiChatDock::showStatus(const QString &text)
{
    m_infoLabel->setText(text);
}

void AiChatDock::beginStream()
{
    m_streamCursor = QTextCursor(m_view->document());
    m_streamCursor.movePosition(QTextCursor::End);
    if (!m_view->document()->isEmpty())
        m_streamCursor.insertBlock();

    QTextCharFormat headerFormat;
    headerFormat.setForeground(palette().color(QPalette::Text));
    headerFormat.setFontWeight(QFont::Bold);
    m_streamCursor.insertText(tr("AI："), headerFormat);

    QTextCharFormat plainFormat;
    plainFormat.setFontWeight(QFont::Normal);
    plainFormat.setFontItalic(false);
    plainFormat.setForeground(palette().color(QPalette::Text));
    m_streamCursor.insertText(QStringLiteral(" "), plainFormat);

    m_streaming = true;
}

void AiChatDock::appendStreamText(const QString &text)
{
    if (!m_streaming)
        beginStream();
    QTextCharFormat plainFormat;
    plainFormat.setFontWeight(QFont::Normal);
    plainFormat.setFontItalic(false);
    plainFormat.setForeground(palette().color(QPalette::Text));
    m_streamCursor.insertText(text, plainFormat);
    m_view->setTextCursor(m_streamCursor);
    m_view->ensureCursorVisible();
}

void AiChatDock::endStream()
{
    m_streaming = false;
}

void AiChatDock::clearView()
{
    m_view->clear();
    m_streaming = false;
}

} // namespace AiAssistant
