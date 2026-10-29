/****************************************************************************
** Designer 画布“只读”采集桥
**
** 双采集方案中的“辅助方案”：
**   * 通过 QtDesigner 模块的公开接口（QDesignerFormWindowInterface / cursor）
**     读取当前激活窗体、选中控件、实时几何信息；
**   * 绝不调用画布私有 API，绝不通过画布做修改；
**   * 任何修改都走 .ui XML（UiDom::UiDocument）。
**
** Designer 未打开窗体时，退化为读取 .ui 文件（主方案）。
****************************************************************************/

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace AiAssistant {

class ProjectService;

class DesignerBridge : public QObject
{
    Q_OBJECT
public:
    struct FormInfo
    {
        bool available = false;
        QString source;             // "designer" / "file" / "none"
        QString uiFile;
        QString formClass;
        QString formObjectName;
        QString selectedObjectName;
        QString note;
        QJsonObject data;

        QJsonObject toJson() const;
    };

    explicit DesignerBridge(ProjectService *project, QObject *parent = nullptr);

    FormInfo currentForm() const;
    QJsonObject selectedWidget() const;
    QString currentUiFile() const;
    bool designerAttached() const;

private:
    ProjectService *m_project = nullptr;
};

} // namespace AiAssistant
