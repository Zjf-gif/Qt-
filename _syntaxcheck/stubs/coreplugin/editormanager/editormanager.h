#pragma once
#include "coreplugin/editormanager/ieditor.h"

namespace Core {

class EditorManager
{
public:
    static IEditor *currentEditor();
};

} // namespace Core
