/****************************************************************************
** SSE / NDJSON 增量解析器
**
** 兼容三种服务端行为：
**   1) OpenAI 标准 SSE： "data: {...}\n\n"、结尾 "data: [DONE]"
**   2) 省略空行分隔的 SSE（部分代理）：收到新的 data: 行即视为上一事件结束
**   3) Ollama 原生 NDJSON：每行一个 JSON 对象，没有 data: 前缀
**
** 注意：takeEvents() 只返回“已完整”的事件；流结束时必须调用 flushRemaining()
** 才能取到最后一段没有换行结尾的数据。
****************************************************************************/

#pragma once

#include <QByteArray>
#include <QVector>

namespace AiAssistant {

class SseParser
{
public:
    struct Event
    {
        QByteArray data;
        QByteArray eventName;
    };

    void reset();
    void feed(const QByteArray &chunk);
    QVector<Event> takeEvents();
    QVector<Event> flushRemaining();

    QByteArray rawBody() const { return m_raw; }

private:
    void flushData();
    void processLine(const QByteArray &line);

    QByteArray m_buffer;
    QByteArray m_raw;
    QByteArray m_data;
    QByteArray m_eventName;
    QVector<Event> m_events;
};

} // namespace AiAssistant
