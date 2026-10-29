#include "tools/proeditor.h"

#include <QDir>
#include <QFileInfo>

namespace AiAssistant {

QByteArray ProFileEditor::appendEntry(const QByteArray &content, const QString &section,
                                      const QString &value, bool *alreadyPresent)
{
    if (alreadyPresent)
        *alreadyPresent = false;

    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty() || section.trimmed().isEmpty())
        return content;

    // 简单去重：已经写过同名条目就不再追加
    if (content.contains(trimmed.toUtf8())) {
        if (alreadyPresent)
            *alreadyPresent = true;
        return content;
    }

    QByteArray result = content;
    if (!result.isEmpty() && !result.endsWith('\n'))
        result.append('\n');
    result.append(section.toUtf8());
    result.append(" += ");
    result.append(trimmed.toUtf8());
    result.append('\n');
    return result;
}

QString ProFileEditor::relativeForPro(const QString &proPath, const QString &filePath)
{
    const QString baseDir = QFileInfo(proPath).absolutePath();
    QString relative = QDir(baseDir).relativeFilePath(filePath);
    relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (relative.isEmpty())
        relative = filePath;
    return relative;
}

} // namespace AiAssistant
