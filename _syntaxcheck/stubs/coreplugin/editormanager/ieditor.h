#pragma once
#include "coreplugin/idocument.h"

class QWidget;

namespace Core {

// 4.15 的 IEditor 只有 toolBar()，没有 widget()
class IEditor
{
public:
    virtual ~IEditor() = default;
    virtual QWidget *toolBar() = 0;
    virtual IDocument *document() const = 0;
};

} // namespace Core
