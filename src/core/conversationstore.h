/****************************************************************************
** 对话历史持久化（按项目隔离，可关闭）
**
** 存放位置：<Creator 设置目录>/aiassistant/history/<项目路径哈希>.json
** 关闭后不读不写，不影响任何审批逻辑。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace AiAssistant {

class ConversationStore : public QObject
{
    Q_OBJECT
public:
    explicit ConversationStore(QObject *parent = nullptr);

    void setSettingsFileName(const QString &fileName);   // ICore::settings()->fileName()
    void setProjectDirectory(const QString &directory);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    QString storageFilePath() const;
    bool hasStoredHistory() const;

    bool load(QVector<ChatMessage> *messages) const;
    bool save(const QVector<ChatMessage> &messages) const;
    void clear() const;

private:
    QString m_settingsFileName;
    QString m_projectDirectory;
    bool m_enabled = true;
};

} // namespace AiAssistant
