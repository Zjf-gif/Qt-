/****************************************************************************
** AI 对话 Dock 面板
**
** 消息流区分：用户消息 / AI 回复（含流式）/ 工具日志 / 审批记录 / 错误。
** 按钮：停止Agent、手动快照、回滚、刷新项目上下文、打开设置、清空对话。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"

#include <QTextCursor>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

namespace AiAssistant {

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

private:
    void appendLine(const QString &prefix, const QString &text, const QColor &color,
                    bool italic = false, bool monospace = false);
    void submitInput();
    void updateButtons();
    void updateInfoLabel();

    QTextEdit *m_view = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_snapshotButton = nullptr;
    QPushButton *m_rollbackButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_settingsButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_infoLabel = nullptr;

    QTextCursor m_streamCursor;
    bool m_streaming = false;
    AgentState m_state = AgentState::Idle;
    int m_snapshotCount = 0;
    QString m_modelInfo;
    QString m_endpointInfo;
};

} // namespace AiAssistant
