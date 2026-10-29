/****************************************************************************
** 审批弹窗（核心 UI）
**
** * 模态阻塞（exec()）；
** * 顶部：操作标题 + 类型标签 + 涉及文件；
** * 每个工具调用一个页签：代码用行级 Diff 高亮；UI 用 QUiLoader 渲染“修改后”预览
**   并用标记区分新增/移动/删除控件；
** * 底部：可编辑 JSON 参数（按这里的内容执行），以及备注；
** * 三个按钮：同意执行 / 拒绝本次操作 / 修改指令后执行。
**
** 安全约定：
**   * 关闭窗口（Esc / 右上角 ×）等于“拒绝”；
**   * 不设置默认按钮，避免误按回车直接同意；
**   * 本对话框是唯一的审批入口，插件中没有其他执行路径。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"
#include "ui/uipreviewwidget.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;
class QPushButton;

namespace AiAssistant {

class DiffView;

class ApprovalDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ApprovalDialog(const ApprovalRequest &request, QWidget *parent = nullptr);

    ApprovalDecision decision() const { return m_decision; }

private:
    QWidget *createPage(int index);
    void fillHeader();
    void computeUiMarks(const QByteArray &before, const QByteArray &after,
                        QVector<UiPreviewWidget::Mark> *marks, QStringList *removed) const;
    QVector<FileChange> changesForPlan(const ToolPlan &plan) const;
    bool collectCalls(QVector<ToolCall> *calls, bool *modified);
    void acceptApproved();
    void acceptModified();
    void rejectAll();

    ApprovalRequest m_request;
    ApprovalDecision m_decision;

    QTabWidget *m_tabs = nullptr;
    QVector<QPlainTextEdit *> m_editors;
    QLineEdit *m_comment = nullptr;
    QPushButton *m_approveButton = nullptr;
    QPushButton *m_rejectButton = nullptr;
    QPushButton *m_modifyButton = nullptr;
};

} // namespace AiAssistant
