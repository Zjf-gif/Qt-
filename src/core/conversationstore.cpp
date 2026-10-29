#include "core/conversationstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace AiAssistant {

namespace {

const int kMaxStoredMessages = 240;
const int kMaxStoredContent = 20000;

QJsonObject messageToJson(const ChatMessage &message)
{
    QJsonObject object;
    object.insert(QStringLiteral("role"), chatRoleName(message.role));
    QString content = message.content;
    if (content.size() > kMaxStoredContent)
        content = content.left(kMaxStoredContent) + QStringLiteral("\n…（历史记录已截断）");
    object.insert(QStringLiteral("content"), content);
    object.insert(QStringLiteral("time"), message.timeStamp.toString(Qt::ISODate));
    if (!message.toolCalls.isEmpty()) {
        QJsonArray calls;
        for (const ToolCall &call : message.toolCalls) {
            QJsonObject entry;
            entry.insert(QStringLiteral("id"), call.id);
            entry.insert(QStringLiteral("name"), call.name);
            entry.insert(QStringLiteral("arguments"), call.arguments);
            calls.append(entry);
        }
        object.insert(QStringLiteral("toolCalls"), calls);
    }
    if (!message.toolCallId.isEmpty())
        object.insert(QStringLiteral("toolCallId"), message.toolCallId);
    if (!message.toolName.isEmpty())
        object.insert(QStringLiteral("toolName"), message.toolName);
    if (message.isError)
        object.insert(QStringLiteral("isError"), true);
    if (message.isApprovalNote)
        object.insert(QStringLiteral("isApprovalNote"), true);
    return object;
}

ChatMessage messageFromJson(const QJsonObject &object)
{
    ChatMessage message;
    const QString role = object.value(QStringLiteral("role")).toString();
    if (role == QLatin1String("system"))
        message.role = ChatRole::System;
    else if (role == QLatin1String("assistant"))
        message.role = ChatRole::Assistant;
    else if (role == QLatin1String("tool"))
        message.role = ChatRole::Tool;
    else
        message.role = ChatRole::User;

    message.content = object.value(QStringLiteral("content")).toString();
    message.timeStamp = QDateTime::fromString(object.value(QStringLiteral("time")).toString(),
                                              Qt::ISODate);
    if (!message.timeStamp.isValid())
        message.timeStamp = QDateTime::currentDateTime();

    const QJsonArray calls = object.value(QStringLiteral("toolCalls")).toArray();
    for (const QJsonValue &entry : calls) {
        const QJsonObject callObject = entry.toObject();
        ToolCall call;
        call.id = callObject.value(QStringLiteral("id")).toString();
        call.name = callObject.value(QStringLiteral("name")).toString();
        call.arguments = callObject.value(QStringLiteral("arguments")).toObject();
        if (!call.name.isEmpty())
            message.toolCalls.append(call);
    }

    message.toolCallId = object.value(QStringLiteral("toolCallId")).toString();
    message.toolName = object.value(QStringLiteral("toolName")).toString();
    message.isError = object.value(QStringLiteral("isError")).toBool();
    message.isApprovalNote = object.value(QStringLiteral("isApprovalNote")).toBool();
    return message;
}

} // namespace

ConversationStore::ConversationStore(QObject *parent)
    : QObject(parent)
{
}

void ConversationStore::setSettingsFileName(const QString &fileName)
{
    m_settingsFileName = fileName;
}

void ConversationStore::setProjectDirectory(const QString &directory)
{
    m_projectDirectory = directory;
}

void ConversationStore::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

QString ConversationStore::storageFilePath() const
{
    QString baseDir;
    if (!m_settingsFileName.isEmpty())
        baseDir = QFileInfo(m_settingsFileName).absolutePath();
    else
        baseDir = QDir::homePath() + QStringLiteral("/.config/qtcreator");
    baseDir += QStringLiteral("/aiassistant/history");

    QString key = m_projectDirectory;
    if (key.isEmpty())
        key = QStringLiteral("no-project");
    const QByteArray hash =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return baseDir + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".json");
}

bool ConversationStore::hasStoredHistory() const
{
    return QFileInfo::exists(storageFilePath());
}

bool ConversationStore::load(QVector<ChatMessage> *messages) const
{
    if (messages)
        messages->clear();
    if (!m_enabled)
        return false;

    QFile file(storageFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonArray array = document.object().value(QStringLiteral("messages")).toArray();
    for (const QJsonValue &entry : array) {
        if (!entry.isObject())
            continue;
        const ChatMessage message = messageFromJson(entry.toObject());
        if (!message.isEmpty() && messages)
            messages->append(message);
    }
    return messages && !messages->isEmpty();
}

bool ConversationStore::save(const QVector<ChatMessage> &messages) const
{
    if (!m_enabled)
        return false;

    const QString path = storageFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    const int start = qMax(0, messages.size() - kMaxStoredMessages);
    QJsonArray array;
    for (int i = start; i < messages.size(); ++i)
        array.append(messageToJson(messages.at(i)));

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("projectDirectory"), m_projectDirectory);
    root.insert(QStringLiteral("savedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("messages"), array);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    const bool ok = file.write(data) == data.size();
    file.close();
    return ok;
}

void ConversationStore::clear() const
{
    QFile::remove(storageFilePath());
}

} // namespace AiAssistant
