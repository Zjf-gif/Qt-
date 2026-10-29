#include "llm/sseparser.h"

namespace AiAssistant {

void SseParser::reset()
{
    m_buffer.clear();
    m_raw.clear();
    m_data.clear();
    m_eventName.clear();
    m_events.clear();
}

void SseParser::feed(const QByteArray &chunk)
{
    m_raw += chunk;
    m_buffer += chunk;

    int iterations = 0;
    while (iterations++ < 20000) {   // 防御：异常数据不至于死循环
        const int newline = m_buffer.indexOf('\n');
        if (newline < 0)
            break;
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        processLine(line);
    }
}

void SseParser::processLine(const QByteArray &line)
{
    if (line.isEmpty()) {   // 事件分隔
        flushData();
        return;
    }

    if (line.startsWith(':'))   // 注释 / 心跳
        return;

    if (line.startsWith("data:")) {
        // 若上一段 data 还没被空行结束，先按完整事件处理（兼容省略空行的服务端）
        if (!m_data.isEmpty())
            flushData();
        QByteArray payload = line.mid(5);
        if (payload.startsWith(' '))
            payload.remove(0, 1);
        m_data = payload;
        return;
    }

    if (line.startsWith("event:")) {
        m_eventName = line.mid(6).trimmed();
        return;
    }

    if (line.startsWith("id:"))
        return;

    if (line.startsWith('{') || line.startsWith('[')) {
        // NDJSON（Ollama）：整行就是一个 JSON 对象
        if (!m_data.isEmpty())
            flushData();
        m_data = line;
        flushData();
        return;
    }

    // 其它字段忽略
}

void SseParser::flushData()
{
    if (m_data.isEmpty()) {
        m_eventName.clear();
        return;
    }
    Event event;
    event.data = m_data;
    event.eventName = m_eventName;
    m_events.append(event);
    m_data.clear();
    m_eventName.clear();
}

QVector<SseParser::Event> SseParser::takeEvents()
{
    QVector<Event> events;
    events.swap(m_events);
    return events;
}

QVector<SseParser::Event> SseParser::flushRemaining()
{
    if (!m_buffer.isEmpty()) {
        QByteArray rest = m_buffer;
        m_buffer.clear();
        if (rest.endsWith('\r'))
            rest.chop(1);
        processLine(rest);
    }
    flushData();

    QVector<Event> events;
    events.swap(m_events);
    return events;
}

} // namespace AiAssistant
