/****************************************************************************
** AI 对话 Dock 面板（左右气泡式）
**
** 布局：
**   AI 消息   → 左对齐，浅色气泡
**   用户消息 → 右对齐，主题色气泡
**   工具日志 / 审批 / 系统提示 → 居中小字
**
** 输入区：多行输入框（自动换行、随内容长高），Enter 发送，
**         Ctrl+Enter / Shift+Enter 手动换行。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"

#include <QPointer>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace AiAssistant {

class ChatInput;
class CodeBlockWidget;

class AiChatDock : public QWidget
{
    Q_OBJECT
public:
    explicit AiChatDock(QWidget *parent = nullptr);

    void setAgentState(AgentState state);
    void setSnapshotCount(int count);
    void setModelInfo(const QString &model, const QString &endpoint);

public slots:
    void appendChatMessage(const AiAssistant::ChatMessage &message);
    void appendToolLog(const QString &text, const QString &level = QStringLiteral("info"));
    void appendApprovalRecord(const QString &text);
    void appendError(const QString &text);
    void appendSystemNote(const QString &text);
    void beginStream();
    void appendStreamText(const QString &text);
    void endStream();
    void clearView();
    void focusInput();
    void setInputText(const QString &text);
    void showStatus(const QString &text);

signals:
    void sendRequested(const QString &text);
    void stopRequested();
    void snapshotRequested();
    void rollbackRequested();
    void refreshContextRequested();
    void settingsRequested();
    void clearHistoryRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class BubbleKind { Assistant, User };

    // 在末尾插入一行控件（保持底部弹性空白）
    void appendRow(QWidget *row);
    // 创建一个气泡行（左/右对齐）
    QWidget *createBubbleRow(BubbleKind kind, const QString &html, QLabel **outLabel = nullptr);
    // 居中小字行（工具日志 / 审批 / 错误 / 系统提示）
    void appendNotice(const QString &text, const QString &level);
    // 代码卡片行（左对齐，宽度铺满）
    QWidget *createCodeRow(const QString &language, const QString &code,
                           AiAssistant::CodeBlockWidget **outCard = nullptr);
    // 把 AI 回复按"文本 / 代码"分段渲染
    void appendAssistantContent(const QString &rawText);
    // 流式渲染：复用已有控件，避免闪烁
    void renderStreamSegments();
    void scrollToBottom();
    void updateBubbleWidths();
    void fitBubble(QLabel *label);
    int bubbleMaxWidth() const;
    void clearRows();

    void submitInput();
    void updateButtons();
    void updateInfoLabel();

    QScrollArea *m_scroll = nullptr;
    QWidget *m_bubbleHost = nullptr;
    QVBoxLayout *m_bubbleLayout = nullptr;
    // 最后一个气泡行，用于滚动定位（比直接设 scroll 最大值可靠）
    QPointer<QWidget> m_lastRow;

    ChatInput *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_snapshotButton = nullptr;
    QPushButton *m_rollbackButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_settingsButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_infoLabel = nullptr;

    // 流式输出
    QLabel *m_streamLabel = nullptr;
    QString m_streamRaw;
    bool m_streaming = false;
    // 流式过程中已渲染的分段控件（文本段存 QLabel，代码段存 CodeBlockWidget）
    QVector<QWidget *> m_streamWidgets;
    QVector<bool> m_streamIsCode;

    AgentState m_state = AgentState::Idle;
    int m_snapshotCount = 0;
    QString m_modelInfo;
    QString m_endpointInfo;
};

} // namespace AiAssistant