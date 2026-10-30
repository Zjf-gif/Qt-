#include "ui/aichatdock.h"
#include "ui/chatinput.h"
#include "ui/codeblockwidget.h"

#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

namespace AiAssistant {

namespace {

const QColor kToolColor(140, 140, 140);
const QColor kApprovalColor(200, 145, 30);
const QColor kErrorColor(200, 70, 70);

QString firstLine(const QString &text, int maxChars = 200)
{
    QString flat = text.trimmed();
    const int newline = flat.indexOf(QLatin1Char('\n'));
    if (newline > 0)
        flat = flat.left(newline);
    if (flat.size() > maxChars)
        flat = flat.left(maxChars) + QStringLiteral("…");
    return flat;
}

// 行内格式：`code` 与 **粗体**
QString inlineFormat(const QString &escaped, const QString &codeStyle)
{
    static const QRegularExpression codeRe(QStringLiteral("`([^`]+)`"));
    static const QRegularExpression boldRe(QStringLiteral("\\*\\*([^*]+)\\*\\*"));

    QString out = escaped;
    out.replace(codeRe, QStringLiteral("<code style=\"%1\">\\1</code>").arg(codeStyle));
    out.replace(boldRe, QStringLiteral("<b>\\1</b>"));
    return out;
}

// 轻量 markdown：``` 代码块 / 行内 code / **粗体** / 换行
QString assistantHtml(const QString &raw, const QString &textColor, const QString &codeStyle)
{
    QStringList out;
    QStringList codeLines;
    bool inCode = false;

    const QStringList lines = raw.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("```"))) {
            if (inCode) {
                out << QStringLiteral("<pre style=\"margin:6px 0; padding:8px; border-radius:6px; "
                                      "background:%1; color:%2; font-family:'%3',monospace;\">%4</pre>")
                           .arg(codeStyle, textColor,
                                QFontDatabase::systemFont(QFontDatabase::FixedFont).family(),
                                codeLines.join(QLatin1Char('\n')).toHtmlEscaped());
                codeLines.clear();
                inCode = false;
            } else {
                inCode = true;
            }
            continue;
        }
        if (inCode) {
            codeLines << line;
            continue;
        }
        out << inlineFormat(line.toHtmlEscaped(), codeStyle);
    }
    if (inCode && !codeLines.isEmpty()) {
        out << QStringLiteral("<pre style=\"margin:6px 0; padding:8px; border-radius:6px; "
                              "background:%1; color:%2; font-family:'%3',monospace;\">%4</pre>")
                   .arg(codeStyle, textColor,
                        QFontDatabase::systemFont(QFontDatabase::FixedFont).family(),
                        codeLines.join(QLatin1Char('\n')).toHtmlEscaped());
    }

    QString html = out.join(QStringLiteral("<br>"));
    if (html.trimmed().isEmpty())
        return QString();
    return QStringLiteral("<div style=\"line-height:155%%; color:%1;\">%2</div>").arg(textColor, html);
}

QString plainHtml(const QString &raw, const QString &textColor)
{
    return QStringLiteral("<div style=\"line-height:155%%; color:%1;\">%2</div>")
            .arg(textColor, raw.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br>")));
}

// 把 AI 回复切成「文本段 / 代码段」
struct Segment
{
    bool isCode = false;
    QString language;
    QString text;
};

QVector<Segment> splitSegments(const QString &raw)
{
    QVector<Segment> segments;
    QString codeLines;
    bool inCode = false;
    QString language;

    const QStringList lines = raw.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("```"))) {
            if (inCode) {
                Segment code;
                code.isCode = true;
                code.language = language;
                // 去掉末尾多余空行
                while (codeLines.endsWith(QLatin1Char('\n')))
                    codeLines.chop(1);
                code.text = codeLines;
                segments.append(code);
                codeLines.clear();
                inCode = false;
                language.clear();
            } else {
                inCode = true;
                language = trimmed.mid(3).trimmed();
            }
            continue;
        }

        if (inCode) {
            codeLines += line;
            codeLines += QLatin1Char('\n');
        } else {
            // 连续文本行合并到一个段
            if (segments.isEmpty() || segments.last().isCode) {
                segments.append(Segment());
                segments.last().isCode = false;
            }
            if (!segments.last().text.isEmpty())
                segments.last().text += QLatin1Char('\n');
            segments.last().text += line;
        }
    }

    // 流式时可能出现"还没闭合的代码块"，照样当成代码段渲染
    if (inCode) {
        Segment code;
        code.isCode = true;
        code.language = language;
        while (codeLines.endsWith(QLatin1Char('\n')))
            codeLines.chop(1);
        code.text = codeLines;
        segments.append(code);
    }

    return segments;
}

} // namespace

AiChatDock::AiChatDock(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

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

    // ---- 气泡消息区 ----
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_bubbleHost = new QWidget(m_scroll);
    m_bubbleLayout = new QVBoxLayout(m_bubbleHost);
    m_bubbleLayout->setContentsMargins(4, 4, 4, 4);
    m_bubbleLayout->setSpacing(10);
    m_bubbleLayout->addStretch(1);
    m_scroll->setWidget(m_bubbleHost);
    layout->addWidget(m_scroll, 1);

    // ---- 输入区（多行、自动换行、随内容长高）----
    auto *inputRow = new QHBoxLayout;
    inputRow->setSpacing(6);
    m_input = new ChatInput(this);
    m_input->setPlaceholderText(
        tr("描述你要做的事，例如：把登录按钮移到右下角并改成“登录”…\n"
           "Enter 发送，Ctrl+Enter 换行"));
    m_sendButton = new QPushButton(tr("发送"), this);
    m_sendButton->setAutoDefault(false);
    m_sendButton->setMinimumHeight(56);
    m_sendButton->setMinimumWidth(64);
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_sendButton, 0, Qt::AlignBottom);
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

    // ---- 气泡配色（跟随主题）----
    const QColor highlight = palette().color(QPalette::Highlight);
    const QColor highlightText = palette().color(QPalette::HighlightedText);
    const QColor base = palette().color(QPalette::Base);
    const QColor text = palette().color(QPalette::Text);
    const QColor mid = palette().color(QPalette::Mid);
    const QColor botBg = (base.lightness() > 128) ? base.darker(104) : base.lighter(125);

    setStyleSheet(QStringLiteral(
        "QLabel#aiUserBubble { background:%1; color:%2; border-radius:10px; padding:8px 12px; }"
        "QLabel#aiBotBubble { background:%3; color:%4; border:1px solid %5; border-radius:10px; "
        "padding:8px 12px; }")
        .arg(highlight.name(), highlightText.name(), botBg.name(), text.name(), mid.lighter(115).name()));

    connect(m_sendButton, &QPushButton::clicked, this, &AiChatDock::submitInput);
    connect(m_input, &ChatInput::submitted, this, &AiChatDock::submitInput);
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

// ---------------------------------------------------------------------------
// 气泡与排版
// ---------------------------------------------------------------------------
int AiChatDock::bubbleMaxWidth() const
{
    const int viewport = m_scroll ? m_scroll->viewport()->width() : 400;
    return qMax(200, int(viewport * 0.84));
}

void AiChatDock::appendRow(QWidget *row)
{
    if (!row || !m_bubbleLayout)
        return;
    // 保持最后一项是弹性空白
    m_bubbleLayout->insertWidget(qMax(0, m_bubbleLayout->count() - 1), row);
    m_lastRow = row;
    scrollToBottom();
}

QWidget *AiChatDock::createBubbleRow(BubbleKind kind, const QString &html, QLabel **outLabel)
{
    auto *row = new QWidget(m_bubbleHost);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    auto *column = new QWidget(row);
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(3);

    const bool isUser = (kind == BubbleKind::User);

    auto *name = new QLabel(isUser ? tr("你") : tr("AI"), column);
    name->setStyleSheet(QStringLiteral("color:#9a9a9a; font-size:11px;"));
    name->setAlignment(isUser ? Qt::AlignRight : Qt::AlignLeft);
    columnLayout->addWidget(name);

    auto *bubble = new QLabel(html.isEmpty() ? QStringLiteral("…") : html, column);
    bubble->setTextFormat(Qt::RichText);
    bubble->setWordWrap(true);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    bubble->setObjectName(isUser ? QStringLiteral("aiUserBubble") : QStringLiteral("aiBotBubble"));
    bubble->setProperty("aiHtml", html);

    // 双保险：stylesheet 在某些主题/样式下不生效，这里再用调色板兜一层背景色
    {
        const QColor highlight = palette().color(QPalette::Highlight);
        const QColor highlightText = palette().color(QPalette::HighlightedText);
        const QColor base = palette().color(QPalette::Base);
        const QColor text = palette().color(QPalette::Text);
        const QColor botBg = (base.lightness() > 128) ? base.darker(106) : base.lighter(130);

        QPalette pal = bubble->palette();
        pal.setColor(QPalette::Window, isUser ? highlight : botBg);
        pal.setColor(QPalette::WindowText, isUser ? highlightText : text);
        bubble->setAutoFillBackground(true);
        bubble->setPalette(pal);
    }

    bubble->setMaximumWidth(bubbleMaxWidth());
    bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    columnLayout->addWidget(bubble, 0, isUser ? Qt::AlignRight : Qt::AlignLeft);
    fitBubble(bubble);

    if (isUser) {
        rowLayout->addStretch(1);
        rowLayout->addWidget(column);
    } else {
        rowLayout->addWidget(column);
        rowLayout->addStretch(1);
    }

    if (outLabel)
        *outLabel = bubble;
    return row;
}

void AiChatDock::appendNotice(const QString &text, const QString &level)
{
    QColor color = kToolColor;
    if (level == QLatin1String("error"))
        color = kErrorColor;
    else if (level == QLatin1String("warn") || level == QLatin1String("approval"))
        color = kApprovalColor;

    auto *label = new QLabel(text.toHtmlEscaped(), m_bubbleHost);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("color:%1; font-size:11px;").arg(color.name()));
    label->setMaximumWidth(bubbleMaxWidth() + 40);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    appendRow(label);
}

// 代码卡片行：左对齐，宽度铺满消息区
QWidget *AiChatDock::createCodeRow(const QString &language, const QString &code,
                                   CodeBlockWidget **outCard)
{
    auto *row = new QWidget(m_bubbleHost);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);
    rowLayout->setSpacing(0);

    auto *card = new CodeBlockWidget(row);
    card->setCode(language, code);
    rowLayout->addWidget(card, 1);

    if (outCard)
        *outCard = card;
    return row;
}

// 把 AI 回复按「文本段 / 代码段」分别渲染
void AiChatDock::appendAssistantContent(const QString &rawText)
{
    const QString textColor = palette().color(QPalette::Text).name();
    const QColor base = palette().color(QPalette::Base);
    const QString codeBg = ((base.lightness() > 128) ? base.darker(108) : base.lighter(140)).name();

    const QVector<Segment> segments = splitSegments(rawText);
    for (const Segment &segment : segments) {
        if (segment.isCode) {
            if (segment.text.trimmed().isEmpty())
                continue;
            appendRow(createCodeRow(segment.language, segment.text));
            continue;
        }
        if (segment.text.trimmed().isEmpty())
            continue;
        const QString html = assistantHtml(segment.text, textColor, codeBg);
        appendRow(createBubbleRow(BubbleKind::Assistant, html));
    }
}

void AiChatDock::scrollToBottom()
{
    QTimer::singleShot(0, this, [this] {
        if (!m_scroll || !m_bubbleHost)
            return;
        m_bubbleHost->adjustSize();
        if (m_lastRow && m_lastRow->isVisible()) {
            // 让最后一行真正进入可视区；直接设 scrollbar 最大值会越过内容滚进空白区
            m_scroll->ensureWidgetVisible(m_lastRow.data(), 0, 24);
        }
        QScrollBar *bar = m_scroll->verticalScrollBar();
        const int contentBottom = m_lastRow ? (m_lastRow->y() + m_lastRow->height()) : 0;
        const int limit = qMax(0, contentBottom + 8 - m_scroll->viewport()->height());
        if (bar->value() > limit)
            bar->setValue(limit);
    });
}

void AiChatDock::updateBubbleWidths()
{
    const int maxWidth = bubbleMaxWidth();
    for (int i = 0; i < m_bubbleLayout->count(); ++i) {
        QLayoutItem *item = m_bubbleLayout->itemAt(i);
        QWidget *widget = item ? item->widget() : nullptr;
        if (!widget)
            continue;
        const QList<QLabel *> labels = widget->findChildren<QLabel *>();
        for (QLabel *label : labels) {
            if (label->objectName().endsWith(QLatin1String("Bubble"))) {
                label->setMaximumWidth(maxWidth);
                fitBubble(label);
            }
        }
    }
}

// 让气泡宽度贴合内容（上限 bubbleMaxWidth），避免窄窄一条
void AiChatDock::fitBubble(QLabel *label)
{
    if (!label)
        return;
    const QString html = label->property("aiHtml").toString();
    if (html.isEmpty()) {
        label->setFixedWidth(qMin(120, bubbleMaxWidth()));
        return;
    }

    QTextDocument doc;
    doc.setDefaultFont(label->font());
    doc.setDocumentMargin(0);
    doc.setHtml(html);
    doc.setTextWidth(-1);
    const int ideal = int(doc.idealWidth()) + 34;   // 加上气泡内边距
    label->setFixedWidth(qBound(120, ideal, bubbleMaxWidth()));
}

void AiChatDock::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateBubbleWidths();
}

void AiChatDock::clearRows()
{
    if (!m_bubbleLayout)
        return;
    while (m_bubbleLayout->count() > 1) {
        QLayoutItem *item = m_bubbleLayout->takeAt(0);
        if (!item)
            break;
        if (QWidget *widget = item->widget()) {
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
    m_streamLabel = nullptr;
    m_streamRaw.clear();
    m_streaming = false;
    m_streamWidgets.clear();
    m_streamIsCode.clear();
    m_lastRow = nullptr;
}

// ---------------------------------------------------------------------------
// 发送
// ---------------------------------------------------------------------------
void AiChatDock::submitInput()
{
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty())
        return;
    m_input->clear();
    emit sendRequested(text);
}

void AiChatDock::setInputText(const QString &text)
{
    m_input->setPlainText(text);
    m_input->moveCursor(QTextCursor::End);
    m_input->setFocus();
}

void AiChatDock::focusInput()
{
    m_input->setFocus();
    m_input->moveCursor(QTextCursor::End);
}

// ---------------------------------------------------------------------------
// 状态 / 信息
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// 消息
// ---------------------------------------------------------------------------
void AiChatDock::appendChatMessage(const AiAssistant::ChatMessage &message)
{
    const QString textColor = palette().color(QPalette::Text).name();
    const QColor base = palette().color(QPalette::Base);
    const QString codeBg = ((base.lightness() > 128) ? base.darker(108) : base.lighter(140)).name();

    switch (message.role) {
    case ChatRole::User: {
        const QString html = plainHtml(message.content, palette().color(QPalette::HighlightedText).name());
        appendRow(createBubbleRow(BubbleKind::User, html));
        break;
    }
    case ChatRole::Assistant: {
        if (!message.streamed) {
            if (message.content.trimmed().isEmpty()) {
                if (!message.toolCalls.isEmpty()) {
                    appendRow(createBubbleRow(
                        BubbleKind::Assistant,
                        QStringLiteral("<i>%1</i>").arg(
                            tr("（请求执行 %1 个工具）").arg(message.toolCalls.size()).toHtmlEscaped())));
                }
            } else {
                // 文本走气泡、代码走卡片
                appendAssistantContent(message.content);
            }
        } else if (!message.toolCalls.isEmpty()) {
            appendNotice(tr("（请求执行 %1 个工具）").arg(message.toolCalls.size()), QStringLiteral("info"));
        }
        for (const ToolCall &call : message.toolCalls) {
            appendNotice(QStringLiteral("%1(%2)").arg(call.name, firstLine(call.argumentsText(), 120)),
                         QStringLiteral("info"));
        }
        break;
    }
    case ChatRole::Tool:
        appendNotice(tr("工具 %1 → %2")
                         .arg(message.toolName.isEmpty() ? QStringLiteral("(未知)") : message.toolName,
                              firstLine(message.content, 300)),
                     message.isError ? QStringLiteral("error") : QStringLiteral("info"));
        break;
    case ChatRole::System:
        if (message.isError)
            appendNotice(tr("错误：%1").arg(message.content), QStringLiteral("error"));
        else
            appendNotice(message.content, QStringLiteral("info"));
        break;
    }
}

void AiChatDock::appendToolLog(const QString &text, const QString &level)
{
    appendNotice(QStringLiteral("[工具] ") + text, level);
}

void AiChatDock::appendApprovalRecord(const QString &text)
{
    appendNotice(QStringLiteral("[审批] ") + text, QStringLiteral("approval"));
}

void AiChatDock::appendError(const QString &text)
{
    appendNotice(QStringLiteral("错误：") + text, QStringLiteral("error"));
}

void AiChatDock::appendSystemNote(const QString &text)
{
    appendNotice(text, QStringLiteral("info"));
}

void AiChatDock::showStatus(const QString &text)
{
    m_infoLabel->setText(text);
}

// ---------------------------------------------------------------------------
// 流式输出
// ---------------------------------------------------------------------------
void AiChatDock::beginStream()
{
    m_streamRaw.clear();
    m_streaming = true;
    m_streamLabel = nullptr;
    m_streamWidgets.clear();
    m_streamIsCode.clear();
    QLabel *label = nullptr;
    appendRow(createBubbleRow(BubbleKind::Assistant, QString(), &label));
    m_streamLabel = label;
    m_streamWidgets.append(label);
    m_streamIsCode.append(false);
    scrollToBottom();
}

// 流式增量渲染：已有分段就地更新，新出现的分段才新建控件（避免闪烁）
void AiChatDock::renderStreamSegments()
{
    const QString textColor = palette().color(QPalette::Text).name();
    const QColor base = palette().color(QPalette::Base);
    const QString codeBg = ((base.lightness() > 128) ? base.darker(108) : base.lighter(140)).name();

    const QVector<Segment> segments = splitSegments(m_streamRaw);
    for (int i = 0; i < segments.size(); ++i) {
        const Segment &segment = segments.at(i);

        // 分段类型发生变化（例如文本后面开始写代码块），整段重建
        if (i < m_streamIsCode.size() && m_streamIsCode.at(i) != segment.isCode) {
            for (int j = m_streamWidgets.size() - 1; j >= i; --j) {
                QWidget *widget = m_streamWidgets.at(j);
                if (widget) {
                    if (QWidget *row = widget->parentWidget()) {
                        row->setParent(nullptr);
                        row->deleteLater();
                    }
                }
            }
            m_streamWidgets.resize(i);
            m_streamIsCode.resize(i);
        }

        if (i >= m_streamWidgets.size()) {
            // 新增分段
            if (segment.isCode) {
                CodeBlockWidget *card = nullptr;
                appendRow(createCodeRow(segment.language, segment.text, &card));
                m_streamWidgets.append(card);
                m_streamIsCode.append(true);
            } else {
                QLabel *label = nullptr;
                appendRow(createBubbleRow(BubbleKind::Assistant, QString(), &label));
                m_streamWidgets.append(label);
                m_streamIsCode.append(false);
            }
        }

        // 更新该分段的内容
        if (i >= m_streamWidgets.size())
            continue;
        QWidget *widget = m_streamWidgets.at(i);
        if (segment.isCode) {
            if (auto *card = qobject_cast<CodeBlockWidget *>(widget))
                card->setCode(segment.language, segment.text);
        } else if (auto *label = qobject_cast<QLabel *>(widget)) {
            const QString html = assistantHtml(segment.text, textColor, codeBg);
            label->setProperty("aiHtml", html);
            label->setText(html.isEmpty() ? QStringLiteral("…") : html);
            fitBubble(label);
        }
    }

    // 最后一段是文本时，把它当作后续 appendStreamText 的目标
    if (!m_streamWidgets.isEmpty() && !m_streamIsCode.last())
        m_streamLabel = qobject_cast<QLabel *>(m_streamWidgets.last());
    else
        m_streamLabel = nullptr;

    scrollToBottom();
}

void AiChatDock::appendStreamText(const QString &text)
{
    if (!m_streaming)
        beginStream();

    m_streamRaw += text;
    renderStreamSegments();
}

void AiChatDock::endStream()
{
    m_streaming = false;
    m_streamLabel = nullptr;
    m_streamWidgets.clear();
    m_streamIsCode.clear();
    m_streamRaw.clear();
}

void AiChatDock::clearView()
{
    clearRows();
    appendSystemNote(tr("对话已清空。"));
}

} // namespace AiAssistant