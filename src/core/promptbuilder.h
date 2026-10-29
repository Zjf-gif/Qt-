/****************************************************************************
** 系统提示词 + 上下文压缩
**
** 系统提示词随插件下发（规格五），并追加运行期工程上下文摘要，
** 避免每轮对话都让模型重新问“项目里有什么”。
** 压缩策略：先截断超长工具输出，再按“轮次”丢弃最老的对话，始终保留最后一轮。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"
#include "creator/projectservice.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace AiAssistant {

class PromptBuilder
{
public:
    static QString systemPrompt();
    static QString toolGuide();

    QString buildSystemMessage(const ProjectSnapshot &project,
                               const QJsonObject &designerSelection) const;

    static QVector<ChatMessage> compact(const QVector<ChatMessage> &history, int charBudget);
    static int charCount(const QVector<ChatMessage> &history);
};

} // namespace AiAssistant
