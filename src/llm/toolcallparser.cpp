#include "llm/toolcallparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>

namespace AiAssistant {

namespace {

QString pickToolName(const QJsonObject &object)
{
    static const QStringList keys = {QStringLiteral("tool"), QStringLiteral("tool_name"),
                                     QStringLiteral("name"), QStringLiteral("function")};
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString() && !value.toString().trimmed().isEmpty())
            return value.toString().trimmed();
        if (value.isObject()) {
            const QString nested = value.toObject().value(QStringLiteral("name")).toString().trimmed();
            if (!nested.isEmpty())
                return nested;
        }
    }
    return QString();
}

QJsonObject pickArguments(const QJsonObject &object)
{
    static const QStringList keys = {QStringLiteral("arguments"), QStringLiteral("parameters"),
                                     QStringLiteral("input"), QStringLiteral("args")};
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isObject())
            return value.toObject();
        if (value.isString()) {
            QJsonObject parsed;
            if (ToolCallParser::parseArguments(value.toString(), &parsed))
                return parsed;
        }
    }
    const QJsonValue functionValue = object.value(QStringLiteral("function"));
    if (functionValue.isObject())
        return pickArguments(functionValue.toObject());
    return QJsonObject();
}

void collectFromValue(const QJsonValue &value, QVector<ToolCall> *calls)
{
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &entry : array)
            collectFromValue(entry, calls);
        return;
    }
    if (!value.isObject())
        return;

    const QJsonObject object = value.toObject();
    const QString name = pickToolName(object);
    if (name.isEmpty())
        return;

    ToolCall call;
    call.name = name;
    call.arguments = pickArguments(object);
    calls->append(call);
}

// 找出从 start 开始的一个平衡大括号块（跳过字符串内的括号）
int balancedObjectEnd(const QString &text, int start)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (int i = start; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (ch == QLatin1Char('{'))
            ++depth;
        else if (ch == QLatin1Char('}')) {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

} // namespace

QString ToolCallParser::sanitizeJson(QString raw)
{
    QString text = raw.trimmed();

    // 去掉 ```json ... ``` 围栏
    if (text.startsWith(QLatin1String("```"))) {
        const int firstNewline = text.indexOf(QLatin1Char('\n'));
        if (firstNewline > 0)
            text = text.mid(firstNewline + 1);
        if (text.endsWith(QLatin1String("```")))
            text.chop(3);
        text = text.trimmed();
    }

    // 去掉尾随逗号：  {"a":1,}  /  [1,2,]
    static const QRegularExpression trailingComma(QStringLiteral(",\\s*([}\\]])"));
    for (int i = 0; i < 8; ++i) {
        const QString replaced = text;
        text.replace(trailingComma, QStringLiteral("\\1"));
        if (replaced == text)
            break;
    }
    return text;
}

bool ToolCallParser::parseArguments(const QString &raw, QJsonObject *out)
{
    if (!out)
        return false;
    const QString sanitized = sanitizeJson(raw);
    if (sanitized.isEmpty()) {
        *out = QJsonObject();
        return true;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(sanitized.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    *out = document.object();
    return true;
}

QVector<ToolCall> ToolCallParser::extractFromText(const QString &text, QString *residualText)
{
    QVector<ToolCall> calls;
    QString remaining = text;

    auto consume = [&remaining](int start, int length) {
        if (start < 0 || length <= 0)
            return;
        remaining.replace(start, length, QString());
    };

    // 1) ```json ... ``` 代码围栏
    static const QRegularExpression fence(
        QStringLiteral("```(?:json|JSON|javascript|js)?\\s*([\\s\\S]*?)```"));
    QRegularExpressionMatchIterator it = fence.globalMatch(remaining);
    QVector<QPair<int, int>> fencedRanges;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString body = sanitizeJson(match.captured(1));
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(body.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError) {
            collectFromValue(document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object()),
                             &calls);
            fencedRanges.append(qMakePair(match.capturedStart(0), match.capturedLength(0)));
        }
    }
    for (int i = fencedRanges.size() - 1; i >= 0; --i)
        consume(fencedRanges.at(i).first, fencedRanges.at(i).second);

    // 2) <tool_call>...</tool_call>
    static const QRegularExpression tag(
        QStringLiteral("<tool_call>\\s*([\\s\\S]*?)\\s*</tool_call>"));
    it = tag.globalMatch(remaining);
    QVector<QPair<int, int>> tagRanges;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        QJsonObject parsed;
        if (parseArguments(match.captured(1), &parsed)) {
            collectFromValue(parsed, &calls);
            tagRanges.append(qMakePair(match.capturedStart(0), match.capturedLength(0)));
        }
    }
    for (int i = tagRanges.size() - 1; i >= 0; --i)
        consume(tagRanges.at(i).first, tagRanges.at(i).second);

    // 3) 裸 JSON 对象（限制扫描长度与候选数量）
    const int scanLimit = qMin(remaining.size(), 40000);
    int index = remaining.indexOf(QLatin1Char('{'));
    int candidates = 0;
    while (index >= 0 && index < scanLimit && candidates < 12) {
        const int end = balancedObjectEnd(remaining, index);
        if (end < 0)
            break;
        ++candidates;
        const QString body = remaining.mid(index, end - index + 1);
        QJsonObject parsed;
        if (parseArguments(body, &parsed)) {
            const QString name = pickToolName(parsed);
            if (!name.isEmpty()) {
                ToolCall call;
                call.name = name;
                call.arguments = pickArguments(parsed);
                calls.append(call);
                remaining.replace(index, end - index + 1, QString());
                index = remaining.indexOf(QLatin1Char('{'), index);
                continue;
            }
        }
        index = remaining.indexOf(QLatin1Char('{'), end + 1);
    }

    if (residualText)
        *residualText = remaining.trimmed();
    return calls;
}

} // namespace AiAssistant
