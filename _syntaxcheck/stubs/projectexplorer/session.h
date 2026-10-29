#pragma once
#include "projectexplorer/project.h"

namespace ProjectExplorer {

class SessionManager
{
public:
    static Project *startupProject();
};

} // namespace ProjectExplorer
