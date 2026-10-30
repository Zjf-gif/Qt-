#include "ui/chatinput.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextOption>

namespace AiAssistant {

ChatInput::ChatInput(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTabChangesFocus(true);
    setProperty("aiassistantParamEditor", true);

    connect(this, &QPlainTextEdit::textChanged, this, &ChatInput::updateHeight);
    updateHeight();
}

int ChatInput::preferredLines() const
{
    // 可见行数 = 光标所在块的换行数 + 折行后的行数总和
    const QFontMetrics metrics(font());
    const int lineHeight = qMax(1, metrics.lineSpacing());
    const int contentHeight = int(document()->size().height());

    // document()->size() 不含内边距，这里用 viewport 高度换算更稳
    Q_UNUSED(lineHeight)
    const int textLines = qMax(1, qRound(double(contentHeight) / double(qMax(1, metrics.lineSpacing()))));
    return textLines;
}

void ChatInput::updateHeight()
{
    const QFontMetrics metrics(font());
    const int lineHeight = metrics.lineSpacing();
    const int frame = 2 * frameWidth() + 2 * int(document()->documentMargin());

    const int lines = qBound(m_minLines, preferredLines(), m_maxLines);
    const int height = lines * lineHeight + frame + 6;
    if (height != minimumHeight() || height != maximumHeight()) {
        setFixedHeight(height);
    }
}

void ChatInput::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    updateHeight();
}

void ChatInput::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers();

    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        const bool withModifier = mods.testFlag(Qt::ControlModifier)
                || mods.testFlag(Qt::ShiftModifier)
                || mods.testFlag(Qt::AltModifier);
        if (!withModifier) {
            emit submitted();
            event->accept();
            return;
        }
        // Ctrl+Enter / Shift+Enter：插入换行（默认行为）
    }

    QPlainTextEdit::keyPressEvent(event);
}

} // namespace AiAssistant