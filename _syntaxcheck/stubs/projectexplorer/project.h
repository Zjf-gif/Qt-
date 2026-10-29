#pragma once
#include "utils/filepath.h"

#include <QString>

namespace ProjectExplorer {

class Project
{
public:
    QString displayName() const;
    Utils::FilePath projectDirectory() const;
};

} // namespace ProjectExplorer
