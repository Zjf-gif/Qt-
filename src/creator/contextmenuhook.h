/****************************************************************************
** 右键菜单注入（编辑器 / Designer 画布）
**
** Creator 各版本给编辑器右键菜单起的 ActionContainer id 并不稳定，
** 这里采用“事件过滤器 + 活动弹出菜单注入”的方式，不依赖任何内部 id：
**   1) 监听 QEvent::ContextMenu，判断事件源属于代码编辑器还是 Designer 窗体；
**   2) QTimer::singleShot(0) 在菜单已经弹出后，把自定义 QAction 插到最前面。
**
** 只读语义：本类只“读取”选区/选中控件信息，不修改任何文件。
****************************************************************************/

#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QPlainTextEdit;
class QDesignerFormWindowInterface;

namespace AiAssistant {

class ProjectService;

class ContextMenuHook : public QObject
{
    Q_OBJECT
public:
    explicit ContextMenuHook(ProjectService *project, QObject *parent = nullptr);

    // 供 Tools 菜单里的同名命令复用
    static const char *editorActionText();
    static const char *designerActionText();

signals:
    void refactorSelectionRequested(const QString &filePath, const QString &selection);
    void designerWidgetRequested(const QString &uiFile, const QString &objectName);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void injectIntoActiveMenu();

    ProjectService *m_project = nullptr;
    QPointer<QPlainTextEdit> m_editor;
    QPointer<QDesignerFormWindowInterface> m_formWindow;
    QPointer<QWidget> m_selectedWidget;
    bool m_injectionScheduled = false;
};

} // namespace AiAssistant
