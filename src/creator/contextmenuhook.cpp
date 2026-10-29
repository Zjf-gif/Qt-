#include "creator/contextmenuhook.h"

#include "creator/designerbridge.h"
#include "creator/projectservice.h"

#include <QAction>
#include <QApplication>
#include <QDesignerFormWindowCursorInterface>
#include <QDesignerFormWindowInterface>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTimer>
#include <QWidget>

namespace AiAssistant {

namespace {

QString selectionOf(QPlainTextEdit *editor)
{
    if (!editor)
        return QString();
    QString text = editor->textCursor().selectedText();
    text.replace(QChar(0x2029), QLatin1Char('\n'));
    return text;
}

} // namespace

const char *ContextMenuHook::editorActionText()
{
    return QT_TRANSLATE_NOOP("AiAssistant::ContextMenuHook", "让AI重构选中代码");
}

const char *ContextMenuHook::designerActionText()
{
    return QT_TRANSLATE_NOOP("AiAssistant::ContextMenuHook", "让AI操作当前选中控件");
}

ContextMenuHook::ContextMenuHook(ProjectService *project, QObject *parent)
    : QObject(parent)
    , m_project(project)
{
}

bool ContextMenuHook::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::ContextMenu)
        return QObject::eventFilter(watched, event);

    QWidget *widget = qobject_cast<QWidget *>(watched);
    if (!widget)
        return QObject::eventFilter(watched, event);

    m_editor = nullptr;
    m_formWindow = nullptr;
    m_selectedWidget = nullptr;

    // 向上找 Designer 窗体
    for (QWidget *w = widget; w; w = w->parentWidget()) {
        if (QDesignerFormWindowInterface *window = qobject_cast<QDesignerFormWindowInterface *>(w)) {
            m_formWindow = window;
            if (window->cursor())
                m_selectedWidget = window->cursor()->current();
            break;
        }
    }

    if (!m_formWindow) {
        // 向上找代码编辑器（TextEditorWidget 派生自 QPlainTextEdit）
        for (QWidget *w = widget; w; w = w->parentWidget()) {
            if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit *>(w)) {
                m_editor = plain;
                break;
            }
        }
    }

    if (!m_editor && !m_formWindow)
        return QObject::eventFilter(watched, event);

    if (!m_injectionScheduled) {
        m_injectionScheduled = true;
        QTimer::singleShot(0, this, &ContextMenuHook::injectIntoActiveMenu);
    }
    return QObject::eventFilter(watched, event);
}

void ContextMenuHook::injectIntoActiveMenu()
{
    m_injectionScheduled = false;

    QMenu *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (!menu) {
        // 没有弹出菜单（例如某些平台用原生菜单）——静默放弃，命令仍可从 Tools 菜单触发
        m_editor = nullptr;
        m_formWindow = nullptr;
        m_selectedWidget = nullptr;
        return;
    }

    // 清理上一次注入的动作，避免菜单复用时重复堆积
    const QList<QAction *> existing = menu->actions();
    for (QAction *action : existing) {
        if (!action->property("aiassistant").toString().isEmpty())
            menu->removeAction(action);
    }

    if (m_editor) {
        const QString selection = selectionOf(m_editor);
        QPointer<QPlainTextEdit> editor = m_editor;
        const QString hint = selection.isEmpty()
                                 ? tr("让AI重构选中代码（请先在编辑器中选中代码）")
                                 : tr("让AI重构选中代码（%1 个字符）").arg(selection.size());

        QAction *action = new QAction(hint, menu);
        action->setProperty("aiassistant", QStringLiteral("editor"));
        action->setObjectName(QStringLiteral("AiAssistant.RefactorSelectionMenu"));
        action->setEnabled(!selection.isEmpty());
        connect(action, &QAction::triggered, this, [this, editor, selection]() {
            QString filePath;
            if (m_project)
                filePath = m_project->currentEditorFilePath();
            emit refactorSelectionRequested(filePath, selection);
            Q_UNUSED(editor)
        });

        QAction *first = menu->actions().isEmpty() ? nullptr : menu->actions().first();
        if (first)
            menu->insertAction(first, action);
        else
            menu->addAction(action);
        if (first)
            menu->insertSeparator(first);
    } else if (m_formWindow) {
        const QString uiFile = m_formWindow->fileName();
        const QString objectName = m_selectedWidget ? m_selectedWidget->objectName() : QString();

        QAction *action = new QAction(objectName.isEmpty()
                                          ? tr("让AI操作当前选中控件（请先在画布中选中控件）")
                                          : tr("让AI操作控件 %1").arg(objectName),
                                      menu);
        action->setProperty("aiassistant", QStringLiteral("designer"));
        action->setObjectName(QStringLiteral("AiAssistant.DesignerWidgetMenu"));
        action->setEnabled(!objectName.isEmpty() && !uiFile.isEmpty());
        connect(action, &QAction::triggered, this, [this, uiFile, objectName]() {
            emit designerWidgetRequested(uiFile, objectName);
        });

        QAction *first = menu->actions().isEmpty() ? nullptr : menu->actions().first();
        if (first)
            menu->insertAction(first, action);
        else
            menu->addAction(action);
        if (first)
            menu->insertSeparator(first);
    }

    m_editor = nullptr;
    m_formWindow = nullptr;
    m_selectedWidget = nullptr;
}

} // namespace AiAssistant
