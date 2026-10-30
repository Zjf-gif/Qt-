/****************************************************************************
** 代码卡片控件
**
** 结构：
**   ┌─────────────────────────────┐
**   │ cpp                    [复制] │  ← 顶部条：语言 + 复制按钮
**   ├─────────────────────────────┤
**   │ void foo() { ... }           │  ← 只读等宽编辑区，不自动换行
**   └─────────────────────────────┘
**
** 特性：
**   * 高度随代码行数自适应，超过上限（约 16 行）后内部滚动
**   * 不自动换行，超长行横向滚动（保持代码可读性）
**   * 复制按钮把完整代码写入剪贴板，按钮短暂显示"已复制"
**   * 颜色全部走调色板，避免主题下 stylesheet 不生效的问题
****************************************************************************/

#pragma once

#include <QFrame>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace AiAssistant {

class CodeBlockWidget : public QFrame
{
    Q_OBJECT
public:
    explicit CodeBlockWidget(QWidget *parent = nullptr);

    void setCode(const QString &language, const QString &code);
    QString code() const;
    QString language() const;

signals:
    void copied();

private:
    void copyToClipboard();
    void updateHeight();

    QLabel *m_languageLabel = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPlainTextEdit *m_edit = nullptr;
    QString m_language;
};

} // namespace AiAssistant