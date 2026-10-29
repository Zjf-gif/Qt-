/****************************************************************************
** 行级 Diff 视图
**
** 代码变更：行级高亮（删除行 / 新增行 / 上下文行），等宽字体。
** 算法：先裁掉公共前后缀，再对中间部分做 LCS；超过阈值时退化为“整段替换”，
** 保证再大的文件也不会卡住 UI。
****************************************************************************/

#pragma once

#include <QTextEdit>

namespace AiAssistant {

class DiffView : public QTextEdit
{
    Q_OBJECT
public:
    explicit DiffView(QWidget *parent = nullptr);

    void showDiff(const QString &before, const QString &after, const QString &title = QString());
    void showMessage(const QString &text, bool isError = false);

    static QString buildDiffHtml(const QString &before, const QString &after,
                                 int *addedLines = nullptr, int *removedLines = nullptr);
};

} // namespace AiAssistant
