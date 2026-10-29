#include "llm/llmclient.h"

#include "llm/toolcallparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>

namespace AiAssistant {

LlmClient::LlmClient(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    m_timeoutTimer.setSingleShot(true);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &LlmClient::onTimeout);
}

LlmClient::~LlmClient()
{
    cleanupReply();
}

void LlmClient::setSettings(const AgentSettings &settings)
{
    m_settings = settings;
}

void LlmClient::applyProxy()
{
    QNetworkProxy proxy;
    const QString proxyUrl = m_settings.proxyUrl.trimmed();
    if (proxyUrl.isEmpty()) {
        proxy.setType(QNetworkProxy::DefaultProxy);
    } else {
        const QUrl url(proxyUrl);
        proxy.setType(QNetworkProxy::HttpProxy);
        proxy.setHostName(url.host());
        proxy.setPort(static_cast<quint16>(url.port(8080)));
        if (!url.userName().isEmpty()) {
            proxy.setUser(url.userName());
            proxy.setPassword(url.password());
        }
    }
    m_manager->setProxy(proxy);
}

QJsonArray LlmClient::buildMessages(const QVector<ChatMessage> &messages, bool ollamaStyle) const
{
    QJsonArray array;
    for (const ChatMessage &message : messages) {
        QJsonObject object;
        switch (message.role) {
        case ChatRole::System:
            object.insert(QStringLiteral("role"), QStringLiteral("system"));
            object.insert(QStringLiteral("content"), message.content);
            break;
        case ChatRole::User:
            object.insert(QStringLiteral("role"), QStringLiteral("user"));
            object.insert(QStringLiteral("content"), message.content);
            break;
        case ChatRole::Assistant: {
            object.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            object.insert(QStringLiteral("content"), message.content);
            if (!message.toolCalls.isEmpty()) {
                QJsonArray toolCalls;
                for (const ToolCall &call : message.toolCalls) {
                    QJsonObject function;
                    function.insert(QStringLiteral("name"), call.name);
                    if (ollamaStyle) {
                        if (!call.arguments.isEmpty())
                            function.insert(QStringLiteral("arguments"), call.arguments);
                        else
                            function.insert(QStringLiteral("arguments"), QJsonObject());
                    } else {
                        QString argumentsText = call.rawArguments;
                        if (argumentsText.trimmed().isEmpty()) {
                            argumentsText = QString::fromUtf8(
                                QJsonDocument(call.arguments).toJson(QJsonDocument::Compact));
                        }
                        function.insert(QStringLiteral("arguments"), argumentsText);
                    }
                    QJsonObject entry;
                    if (!ollamaStyle) {
                        entry.insert(QStringLiteral("id"), call.id);
                        entry.insert(QStringLiteral("type"), QStringLiteral("function"));
                    }
                    entry.insert(QStringLiteral("function"), function);
                    toolCalls.append(entry);
                }
                object.insert(QStringLiteral("tool_calls"), toolCalls);
            }
            break;
        }
        case ChatRole::Tool: {
            object.insert(QStringLiteral("role"), QStringLiteral("tool"));
            if (!ollamaStyle && !message.toolCallId.isEmpty())
                object.insert(QStringLiteral("tool_call_id"), message.toolCallId);
            object.insert(QStringLiteral("content"), message.content);
            break;
        }
        }
        array.append(object);
    }
    return array;
}

QByteArray LlmClient::buildRequestBody(const QVector<ChatMessage> &messages,
                                       const QVector<ToolSpec> &tools) const
{
    QJsonObject body;
    body.insert(QStringLiteral("model"), m_settings.model);
    body.insert(QStringLiteral("messages"), buildMessages(messages, m_settings.isOllamaNative()));
    body.insert(QStringLiteral("stream"), true);

    if (m_settings.isOllamaNative()) {
        QJsonObject options;
        options.insert(QStringLiteral("temperature"), m_settings.temperature);
        body.insert(QStringLiteral("options"), options);
    } else {
        body.insert(QStringLiteral("temperature"), m_settings.temperature);
    }

    if (!tools.isEmpty()) {
        QJsonArray toolArray;
        for (const ToolSpec &spec : tools)
            toolArray.append(spec.toJson());
        body.insert(QStringLiteral("tools"), toolArray);
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void LlmClient::send(const QVector<ChatMessage> &messages, const QVector<ToolSpec> &tools)
{
    // 静默丢弃上一次未完成的请求（不能走 cancel()：那会 emit canceled，
    // 让 Agent 误以为用户按了“停止”而回到 Idle）
    if (m_reply) {
        m_timeoutTimer.stop();
        cleanupReply();
    }

    m_text.clear();
    m_toolCalls.clear();
    m_finishReason.clear();
    m_error.clear();
    m_bodyBuffer.clear();
    m_parser.reset();
    m_streaming = true;
    m_ollamaStyle = m_settings.isOllamaNative();

    const QString endpoint = m_settings.endpoint();
    const QUrl url(endpoint);
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        finishWithError(tr("LLM 地址无效：%1（请在设置中填写完整 URL，例如 "
                           "http://127.0.0.1:11434/v1/chat/completions）")
                            .arg(endpoint));
        return;
    }

    applyProxy();

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "text/event-stream");
    if (!m_settings.apiKey.trimmed().isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_settings.apiKey.trimmed().toUtf8());

    m_reply = m_manager->post(request, buildRequestBody(messages, tools));
    if (!m_reply) {
        finishWithError(tr("无法创建网络请求"));
        return;
    }

    connect(m_reply, &QNetworkReply::readyRead, this, &LlmClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &LlmClient::onFinished);

    m_timeoutTimer.start(m_settings.requestTimeoutSec * 1000);
    emit requestStarted();
}

void LlmClient::onReadyRead()
{
    if (!m_reply)
        return;

    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty())
        return;
    m_bodyBuffer += chunk;

    const QString contentType =
        m_reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    if (m_streaming && !contentType.isEmpty() && contentType.contains(QLatin1String("json"))
        && !contentType.contains(QLatin1String("event-stream"))
        && !contentType.contains(QLatin1String("ndjson"))) {
        m_streaming = false;   // 服务端一次性返回完整 JSON
    }

    if (!m_streaming)
        return;

    m_parser.feed(chunk);
    const QVector<SseParser::Event> events = m_parser.takeEvents();
    for (const SseParser::Event &event : events)
        processEvent(event.data);
}

void LlmClient::onFinished()
{
    if (!m_reply)
        return;

    m_timeoutTimer.stop();

    const QNetworkReply::NetworkError networkError = m_reply->error();
    const QString networkErrorString = m_reply->errorString();
    const QByteArray rest = m_reply->readAll();
    if (!rest.isEmpty())
        m_bodyBuffer += rest;
    cleanupReply();

    if (m_streaming) {
        m_parser.feed(rest);
        const QVector<SseParser::Event> events = m_parser.takeEvents();
        for (const SseParser::Event &event : events)
            processEvent(event.data);
        const QVector<SseParser::Event> remaining = m_parser.flushRemaining();
        for (const SseParser::Event &event : remaining)
            processEvent(event.data);
    }

    if (!m_error.isEmpty()) {
        finishWithError(m_error);
        return;
    }

    // 流式解析一无所获时，尝试把原始响应体当作整包 JSON 再解析一次
    if (m_text.trimmed().isEmpty() && m_toolCalls.isEmpty() && !m_bodyBuffer.trimmed().isEmpty())
        parseWholeResponse(m_bodyBuffer);

    if (networkError != QNetworkReply::NoError
        && networkError != QNetworkReply::OperationCanceledError) {
        if (m_text.trimmed().isEmpty() && m_toolCalls.isEmpty()) {
            finishWithError(networkErrorString);
            return;
        }
        m_finishReason = QStringLiteral("partial");
    }

    finishSuccessfully();
}

void LlmClient::onTimeout()
{
    if (!m_reply)
        return;
    cleanupReply();
    m_timeoutTimer.stop();

    if (!m_text.trimmed().isEmpty() || !m_toolCalls.isEmpty()) {
        m_finishReason = QStringLiteral("timeout_partial");
        finishSuccessfully();
        return;
    }
    finishWithError(tr("LLM 请求超时（%1 秒），已中断。请检查服务是否在运行，或调大设置中的超时时间")
                        .arg(m_settings.requestTimeoutSec));
}

void LlmClient::processEvent(const QByteArray &data)
{
    const QByteArray trimmed = data.trimmed();
    if (trimmed.isEmpty())
        return;
    if (trimmed == "[DONE]") {
        if (m_finishReason.isEmpty())
            m_finishReason = QStringLiteral("done");
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        // 单个坏块不足以中断整个流（容错要求）
        return;
    }

    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("error"))) {
        const QJsonValue errorValue = object.value(QStringLiteral("error"));
        QString message = errorValue.toObject().value(QStringLiteral("message")).toString();
        if (message.isEmpty())
            message = errorValue.toString();
        m_error = message.isEmpty() ? tr("服务端返回未知错误") : message;
        return;
    }

    if (object.contains(QStringLiteral("choices")))
        handleOpenAiChunk(object);
    else if (object.contains(QStringLiteral("message")))
        handleOllamaChunk(object);
    else if (object.contains(QStringLiteral("response")))   // /api/generate 兼容
        {
            const QString piece = object.value(QStringLiteral("response")).toString();
            if (!piece.isEmpty()) {
                m_text += piece;
                emit streamDelta(piece);
            }
        }
}

void LlmClient::appendToolCallChunk(const QJsonObject &callObject, int fallbackIndex)
{
    const int index = callObject.contains(QStringLiteral("index"))
                          ? callObject.value(QStringLiteral("index")).toInt()
                          : fallbackIndex;
    ToolCall &call = m_toolCalls[index];   // QMap 默认构造

    const QString id = callObject.value(QStringLiteral("id")).toString();
    if (!id.isEmpty())
        call.id = id;

    const QJsonObject function = callObject.value(QStringLiteral("function")).toObject();
    const QString namePiece = function.value(QStringLiteral("name")).toString();
    if (!namePiece.isEmpty())
        call.name += namePiece;

    const QJsonValue arguments = function.value(QStringLiteral("arguments"));
    if (arguments.isString())
        call.rawArguments += arguments.toString();
    else if (arguments.isObject())
        call.arguments = arguments.toObject();
}

void LlmClient::handleOpenAiChunk(const QJsonObject &object)
{
    const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return;

    const QJsonObject choice = choices.first().toObject();
    const QString finish = choice.value(QStringLiteral("finish_reason")).toString();
    if (!finish.isEmpty())
        m_finishReason = finish;

    QJsonObject payload = choice.value(QStringLiteral("delta")).toObject();
    if (payload.isEmpty() && choice.value(QStringLiteral("message")).isObject()) {
        payload = choice.value(QStringLiteral("message")).toObject();
        const QString text = payload.value(QStringLiteral("content")).toString();
        if (!text.isEmpty()) {
            m_text += text;
            emit streamDelta(text);
        }
    } else {
        const QJsonValue content = payload.value(QStringLiteral("content"));
        const QString text = content.isString() ? content.toString() : QString();
        if (!text.isEmpty()) {
            m_text += text;
            emit streamDelta(text);
        }
    }

    const QJsonArray toolCalls = payload.value(QStringLiteral("tool_calls")).toArray();
    for (int i = 0; i < toolCalls.size(); ++i)
        appendToolCallChunk(toolCalls.at(i).toObject(), i);
}

void LlmClient::handleOllamaChunk(const QJsonObject &object)
{
    const QJsonObject message = object.value(QStringLiteral("message")).toObject();
    const QString text = message.value(QStringLiteral("content")).toString();
    if (!text.isEmpty()) {
        m_text += text;
        emit streamDelta(text);
    }

    const QJsonArray toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
    for (int i = 0; i < toolCalls.size(); ++i) {
        const QJsonObject entry = toolCalls.at(i).toObject();
        ToolCall call;
        call.id = entry.value(QStringLiteral("id")).toString();
        const QJsonObject function = entry.value(QStringLiteral("function")).toObject();
        call.name = function.value(QStringLiteral("name")).toString().trimmed();
        const QJsonValue arguments = function.value(QStringLiteral("arguments"));
        if (arguments.isObject())
            call.arguments = arguments.toObject();
        else if (arguments.isString())
            call.rawArguments = arguments.toString();

        if (!call.name.isEmpty())
            m_toolCalls[m_toolCalls.size()] = call;
    }

    if (object.value(QStringLiteral("done")).toBool() && m_finishReason.isEmpty())
        m_finishReason = QStringLiteral("done");
}

void LlmClient::parseWholeResponse(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        // 不是 JSON：当纯文本处理（降级方案）
        const QString text = QString::fromUtf8(body).trimmed();
        if (!text.isEmpty()) {
            m_text = text;
            if (m_finishReason.isEmpty())
                m_finishReason = QStringLiteral("raw_text");
        }
        return;
    }

    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("error"))) {
        const QJsonValue errorValue = object.value(QStringLiteral("error"));
        QString message = errorValue.toObject().value(QStringLiteral("message")).toString();
        if (message.isEmpty())
            message = errorValue.toString();
        m_error = message.isEmpty() ? tr("服务端返回未知错误") : message;
        return;
    }

    if (object.contains(QStringLiteral("choices"))) {
        const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty())
            return;
        const QJsonObject choice = choices.first().toObject();
        const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
        const QString text = message.value(QStringLiteral("content")).toString();
        if (!text.isEmpty()) {
            m_text = text;
            emit streamDelta(text);
        }
        const QJsonArray toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
        for (int i = 0; i < toolCalls.size(); ++i)
            appendToolCallChunk(toolCalls.at(i).toObject(), i);
        const QString finish = choice.value(QStringLiteral("finish_reason")).toString();
        if (!finish.isEmpty())
            m_finishReason = finish;
        return;
    }

    if (object.contains(QStringLiteral("message")))
        handleOllamaChunk(object);
}

void LlmClient::finishSuccessfully()
{
    QVector<ToolCall> calls;
    QList<int> keys = m_toolCalls.keys();
    std::sort(keys.begin(), keys.end());
    for (int key : keys) {
        ToolCall call = m_toolCalls.value(key);
        if (!call.rawArguments.trimmed().isEmpty() && call.arguments.isEmpty()) {
            QJsonObject parsed;
            if (ToolCallParser::parseArguments(call.rawArguments, &parsed))
                call.arguments = parsed;
        }
        if (call.id.isEmpty())
            call.id = QStringLiteral("call_%1").arg(calls.size() + 1);
        call.name = call.name.trimmed();
        if (!call.name.isEmpty())
            calls.append(call);
    }
    m_toolCalls.clear();

    QString text = m_text;
    if (calls.isEmpty() && !text.trimmed().isEmpty()) {
        // 模型没走 tool_calls：从文本里恢复（恢复出来的调用同样必须审批）
        QString residual;
        const QVector<ToolCall> recovered = ToolCallParser::extractFromText(text, &residual);
        if (!recovered.isEmpty()) {
            calls = recovered;
            for (int i = 0; i < calls.size(); ++i) {
                if (calls[i].id.isEmpty())
                    calls[i].id = QStringLiteral("call_text_%1").arg(i + 1);
            }
            text = residual;
            if (m_finishReason.isEmpty() || m_finishReason == QLatin1String("stop"))
                m_finishReason = QStringLiteral("text_fallback");
        }
    }

    const QString finishReason = m_finishReason;
    m_finishReason.clear();
    emit completed(text.trimmed(), calls, finishReason);
}

void LlmClient::finishWithError(const QString &error)
{
    m_timeoutTimer.stop();
    cleanupReply();
    const QString message = error.trimmed().isEmpty() ? tr("未知错误") : error;
    emit failed(message);
}

void LlmClient::cleanupReply()
{
    if (!m_reply)
        return;
    disconnect(m_reply, nullptr, this, nullptr);
    m_reply->deleteLater();
    m_reply = nullptr;
}

void LlmClient::cancel(const QString &reason)
{
    Q_UNUSED(reason)
    m_timeoutTimer.stop();
    cleanupReply();
    m_parser.reset();
    m_bodyBuffer.clear();
    m_toolCalls.clear();
    m_text.clear();
    emit canceled();
}

} // namespace AiAssistant
