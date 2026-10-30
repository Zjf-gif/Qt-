/****************************************************************************
** 多行输入框
**
** - 自动换行（WidgetWidth）
** - 高度随内容自动增长（3~10 行，超过后出现滚动条）
** - Enter 发送；Ctrl+Enter / Shift+Enter 手动换行
****************************************************************************/

#pragma once

#include <QPlainTextEdit>

namespace AiAssistant {

class ChatInput : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit ChatInput(QWidget *parent = nullptr);

    int minLines() const { return m_minLines; }
    int maxLines() const { return m_maxLines; }

signals:
    void submitted();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateHeight();
    int preferredLines() const;

    int m_minLines = 3;
    int m_maxLines = 10;
};

} // namespace AiAssistant