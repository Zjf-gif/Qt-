/****************************************************************************
** .pro 文件的行级追加工具
**
** 只做“追加一条 += ”这种最安全的修改，不试图重写原有语句，
** 避免把用户精心排版的 .pro 弄乱。
****************************************************************************/

#pragma once

#include <QByteArray>
#include <QString>

namespace AiAssistant {

class ProFileEditor
{
public:
    // 在 content 末尾追加 "SECTION += value"；若 value 已出现则原样返回并置 alreadyPresent
    static QByteArray appendEntry(const QByteArray &content, const QString &section,
                                  const QString &value, bool *alreadyPresent = nullptr);

    // 生成相对 .pro 所在目录的路径（正斜杠）
    static QString relativeForPro(const QString &proPath, const QString &filePath);
};

} // namespace AiAssistant
