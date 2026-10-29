/****************************************************************************
** LLM 通信适配层（对外只暴露 发送 / 取消）
**
** * QNetworkAccessManager POST + SSE 流式；readyRead 分片后交给 SseParser
** * 同时兼容 OpenAI（DeepSeek / vLLM / Ollama 的 /v1 兼容模式）与 Ollama 原生 /api/chat
** * 超时 / 断连 / JSON 解析失败一律降级，不抛异常、不卡死
**
** Agent 不感知任何 HTTP/SSE 细节，只接收 completed(text, toolCalls)。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"
#include "llm/sseparser.h"
#include "settings/agentsettings.h"

#include <QMap>
#include <QObject>
#include <QTimer>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

namespace AiAssistant {

class LlmClient : public QObject
{
    Q_OBJECT
public:
    explicit LlmClient(QObject *parent = nullptr);
    ~LlmClient() override;

    void setSettings(const AgentSettings &settings);
    AgentSettings settings() const { return m_settings; }

    bool isBusy() const { return m_reply != nullptr; }

    void send(const QVector<ChatMessage> &messages, const QVector<ToolSpec> &tools);
    void cancel(const QString &reason = QString());

signals:
    void requestStarted();
    void streamDelta(const QString &text);
    void completed(const QString &assistantText,
                   const QVector<AiAssistant::ToolCall> &toolCalls,
                   const QString &finishReason);
    void failed(const QString &error);
    void canceled();

private slots:
    void onReadyRead();
    void onFinished();
    void onTimeout();

private:
    void applyProxy();
    QByteArray buildRequestBody(const QVector<ChatMessage> &messages, const QVector<ToolSpec> &tools) const;
    QJsonArray buildMessages(const QVector<ChatMessage> &messages, bool ollamaStyle) const;
    void processEvent(const QByteArray &data);
    void handleOpenAiChunk(const QJsonObject &object);
    void handleOllamaChunk(const QJsonObject &object);
    void parseWholeResponse(const QByteArray &body);
    void appendToolCallChunk(const QJsonObject &callObject, int fallbackIndex);
    void finishSuccessfully();
    void finishWithError(const QString &error);
    void cleanupReply();

    AgentSettings m_settings;
    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer m_timeoutTimer;
    SseParser m_parser;

    QString m_text;
    QMap<int, ToolCall> m_toolCalls;
    QString m_finishReason;
    QString m_error;
    QByteArray m_bodyBuffer;
    bool m_streaming = true;
    bool m_ollamaStyle = false;
};

} // namespace AiAssistant
