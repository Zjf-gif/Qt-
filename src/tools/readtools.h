/****************************************************************************
** 只读工具（直接执行，不弹审批、不改任何文件）
**   get_designer_current_ui_info / get_selected_widget / get_project_structure
**   read_file / get_build_errors
****************************************************************************/

#pragma once

#include <QObject>

namespace AiAssistant {

class ToolRegistry;
struct ToolRuntime;

class ReadTools : public QObject
{
    Q_OBJECT
public:
    explicit ReadTools(ToolRuntime *runtime, QObject *parent = nullptr);

    void registerTools(ToolRegistry *registry);

private:
    ToolRuntime *m_runtime = nullptr;
};

} // namespace AiAssistant
