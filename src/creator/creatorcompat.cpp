#include "creator/creatorcompat.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <utils/id.h>

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>

namespace AiAssistant {
namespace Compat {

QObject *findObjectByClassName(const QStringList &classNames)
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return nullptr;

    const QList<QObject *> children = app->findChildren<QObject *>();
    for (QObject *child : children) {
        if (!child)
            continue;
        const QString className = QString::fromLatin1(child->metaObject()->className());
        if (classNames.contains(className))
            return child;
    }
    // 退化：允许 "ProjectExplorer::BuildManager" 与 "BuildManager" 互相匹配
    for (QObject *child : children) {
        if (!child)
            continue;
        const QString className = QString::fromLatin1(child->metaObject()->className());
        const QString simple = className.mid(className.lastIndexOf(QLatin1String("::")) + 2);
        for (const QString &candidate : classNames) {
            if (simple == candidate)
                return child;
        }
    }
    return nullptr;
}

bool commandExists(const QString &commandId)
{
    Core::ActionManager *actionManager = Core::ActionManager::instance();
    if (!actionManager)
        return false;
    // Utils::Id 只有 const char* 构造（QLatin1String 被 delete，QString 需显式 fromString）
    return actionManager->command(Utils::Id::fromString(commandId)) != nullptr;
}

bool triggerCommand(const QStringList &commandIds)
{
    Core::ActionManager *actionManager = Core::ActionManager::instance();
    if (!actionManager)
        return false;

    for (const QString &id : commandIds) {
        Core::Command *command = actionManager->command(Utils::Id::fromString(id));
        if (!command || !command->action())
            continue;
        if (!command->action()->isEnabled())
            continue;
        command->action()->trigger();
        return true;
    }
    return false;
}

QStringList buildCommandCandidates()
{
    return {QStringLiteral("ProjectExplorer.Build"),
            QStringLiteral("ProjectExplorer.BuildSession"),
            QStringLiteral("ProjectExplorer.BuildProject"),
            QStringLiteral("ProjectExplorer.Build.StartupProject")};
}

QStringList runCommandCandidates()
{
    return {QStringLiteral("ProjectExplorer.Run"),
            QStringLiteral("ProjectExplorer.RunProject"),
            QStringLiteral("ProjectExplorer.Run.StartupProject")};
}

QStringList stopCommandCandidates()
{
    return {QStringLiteral("ProjectExplorer.Stop"),
            QStringLiteral("ProjectExplorer.StopRun"),
            QStringLiteral("ProjectExplorer.Stop.Project")};
}

} // namespace Compat
} // namespace AiAssistant
