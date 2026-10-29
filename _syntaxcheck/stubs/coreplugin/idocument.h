#pragma once
#include "utils/filepath.h"

#include <QByteArray>
#include <QString>

namespace Core {

class IDocument
{
public:
    virtual ~IDocument() = default;
    virtual const Utils::FilePath &filePath() const = 0;
    virtual QByteArray contents() const = 0;
    virtual bool setContents(const QByteArray &contents) = 0;
    // 4.15 实际签名：errorString 没有默认值
    virtual bool save(QString *errorString, const QString &fileName = QString(),
                      bool autoSave = false) = 0;
    virtual bool isModified() const = 0;
};

} // namespace Core
