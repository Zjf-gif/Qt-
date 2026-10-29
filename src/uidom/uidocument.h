/****************************************************************************
** Qt5 标准 .ui XML（version="4.0"）的读写引擎。
**
** 铁律：所有 UI 操作 = 修改 .ui XML（QDomDocument），绝不触碰 Designer 画布私有 API；
** “拖拽控件” = 修改 <rect> 节点；“新增控件” = 插入 <widget>/<item> 节点。
**
** 本文件只依赖 Qt（Core/Xml），不依赖 Creator，方便单独做单元测试。
****************************************************************************/

#pragma once

#include <QByteArray>
#include <QDomDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

namespace AiAssistant {
namespace UiDom {

struct WidgetSpec
{
    QString type;                 // 例如 QPushButton
    QString objectName;           // 必须唯一
    QRect geometry;               // 父容器无布局时生效
    QJsonObject properties;       // 普通 JSON 值，或 {"type": "rect", "value": {...}}
    QString parentObjectName;     // 为空则挂在窗体顶层控件下
};

struct WidgetNode
{
    QString objectName;
    QString className;
    QString parentObjectName;
    QRect geometry;
    bool insideLayout = false;
    QStringList children;
    QJsonObject properties;
};

class UiDocument
{
public:
    UiDocument();

    bool parse(const QByteArray &xml, QString *error);
    bool isValid() const { return m_valid; }
    QByteArray toXml(QString *error = nullptr) const;

    QString formClass() const;
    QString formObjectName() const;
    QVector<WidgetNode> widgets() const;
    bool contains(const QString &objectName) const;
    bool widgetInfo(const QString &objectName, WidgetNode *out) const;
    bool isInsideLayout(const QString &objectName) const;
    QStringList layoutSummary() const;
    QStringList connections() const;

    // --- 结构编辑 -----------------------------------------------------------
    bool addWidget(const WidgetSpec &spec, QString *error);
    bool moveWidget(const QString &objectName, const QRect &rect, QString *error);
    bool removeWidget(const QString &objectName, QString *error);
    bool setProperty(const QString &objectName, const QString &key, const QJsonValue &value, QString *error);
    bool bindSignalSlot(const QString &senderObject, const QString &signal,
                        const QString &receiverObject, const QString &slot, QString *error);

    // --- 新建窗体 -----------------------------------------------------------
    static QByteArray createForm(const QString &uiName, const QString &baseClass,
                                 const QVector<WidgetSpec> &widgets, QString *error);

private:
    QDomElement formWidgetElement() const;
    QDomElement findWidgetElement(const QString &objectName) const;

    QDomDocument m_doc;
    bool m_valid = false;
};

} // namespace UiDom
} // namespace AiAssistant
