#pragma once
#include "coreplugin/actionmanager/actioncontainer.h"
#include "coreplugin/actionmanager/command.h"
#include "coreplugin/context.h"
#include "utils/id.h"

class QAction;

namespace Core {

class ActionManager
{
public:
    static ActionManager *instance();

    Command *registerAction(QAction *action, Utils::Id id, const Context &context,
                            bool addToMenu = false);
    Command *command(Utils::Id id) const;
    ActionContainer *createMenu(Utils::Id id);
    ActionContainer *actionContainer(Utils::Id id);
};

} // namespace Core
