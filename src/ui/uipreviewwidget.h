/****************************************************************************
** UI 预览组件
**
** 用 QUiLoader 把“修改后”的 .ui XML 直接渲染成真实控件（不落盘、不碰 Designer），
** 再叠加一层透明标记：
**   * 绿色实线框 = 新增控件
**   * 蓝色虚线框 = 位置/尺寸发生变化
**   * 被删除的控件无法渲染，列在图例里
****************************************************************************/

#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QScrollArea;

namespace AiAssistant {

class UiPreviewWidget : public QWidget
{
    Q_OBJECT
public:
    enum MarkKind {
        Added = 0,
        Moved,
        Changed
    };

    struct Mark
    {
        QString objectName;
        MarkKind kind = Added;
        QString note;
    };

    explicit UiPreviewWidget(QWidget *parent = nullptr);

    bool loadXml(const QByteArray &xml, QString *error);
    void setMarks(const QVector<Mark> &marks);
    void setRemovedWidgets(const QStringList &names);
    void clear();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    class Overlay;

    void syncOverlayGeometry();
    void updateLegend();

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_canvas = nullptr;
    QWidget *m_form = nullptr;
    Overlay *m_overlay = nullptr;
    QLabel *m_legend = nullptr;
    QVector<Mark> m_marks;
    QStringList m_removed;
};

} // namespace AiAssistant
