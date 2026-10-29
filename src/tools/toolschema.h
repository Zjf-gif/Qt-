/****************************************************************************
** JSON Schema 小工具（只用于拼 tool 参数描述，不做校验）
****************************************************************************/

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace AiAssistant {
namespace Schema {

inline QJsonObject prop(const QString &type, const QString &description)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("description"), description);
    return object;
}

inline QJsonObject prop(const QString &type, const QString &description, const QJsonObject &extra)
{
    QJsonObject object = prop(type, description);
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        object.insert(it.key(), it.value());
    return object;
}

inline QJsonObject object(const QJsonObject &properties, const QStringList &required = {})
{
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    if (!required.isEmpty()) {
        QJsonArray array;
        for (const QString &name : required)
            array.append(name);
        schema.insert(QStringLiteral("required"), array);
    }
    return schema;
}

inline QJsonObject emptyObject()
{
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), QJsonObject());
    return schema;
}

} // namespace Schema
} // namespace AiAssistant
