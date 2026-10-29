/****************************************************************************
** UI 操作工具（全部通过修改 .ui XML 实现，全部必须人工审批）
**   ui_add_widget / ui_move_widget / ui_modify_widget_property
**   ui_bind_signal_slot / ui_delete_widget / create_ui_form
**
** “拖拽控件”的语义 = ui_move_widget 修改 <rect> 节点；
** 写入 .ui 后由 Creator 自动感知文件变化并刷新（Designer 侧需要重新打开窗体）。
****************************************************************************/

#pragma once

#include "tools/toolruntime.h"

#include <QObject>

namespace AiAssistant {

class ToolRegistry;

class UiTools : public QObject
{
    Q_OBJECT
public:
    explicit UiTools(ToolRuntime *runtime, QObject *parent = nullptr);

    void registerTools(ToolRegistry *registry);

private:
    ToolRuntime *m_runtime = nullptr;
};

} // namespace AiAssistant
