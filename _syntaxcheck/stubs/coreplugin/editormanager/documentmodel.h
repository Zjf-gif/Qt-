#pragma once
#include "coreplugin/idocument.h"
#include "utils/filepath.h"

#include <QList>

namespace Core {

// Qt Creator 4.15：class DocumentModel（不是 namespace），位于
// coreplugin/editormanager/documentmodel.h
class DocumentModel
{
public:
    static QList<IDocument *> openedDocuments();
    static IDocument *documentForFilePath(const Utils::FilePath &filePath);
};

} // namespace Core
