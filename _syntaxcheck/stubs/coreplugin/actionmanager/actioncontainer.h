#pragma once
#include "coreplugin/actionmanager/command.h"

class QMenu;

namespace Core {

class Command;

class ActionContainer
{
public:
    virtual ~ActionContainer() = default;
    virtual QMenu *menu() const = 0;
    virtual void addAction(Command *command) = 0;
    virtual void addMenu(ActionContainer *menu) = 0;
};

} // namespace Core
