#include "uidom/uidocument.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSize>
#include <QStringList>

namespace AiAssistant {
namespace UiDom {

namespace {

const char kWidgetTag[] = "widget";
const char kLayoutTag[] = "layout";
const char kItemTag[] = "item";
const char kPropertyTag[] = "property";
const char kObjectName[] = "objectName";

QDomElement propertyElement(const QDomElement &widget, const QString &name)
{
    for (QDomElement p = widget.firstChildElement(QString::fromLatin1(kPropertyTag)); !p.isNull();
         p = p.nextSiblingElement(QString::fromLatin1(kPropertyTag))) {
        if (p.attribute(QStringLiteral("name")) == name)
            return p;
    }
    return QDomElement();
}

QDomElement ensureProperty(QDomElement &widget, const QString &name)
{
    QDomElement existing = propertyElement(widget, name);
    if (!existing.isNull())
        return existing;

    QDomDocument doc = widget.ownerDocument();
    QDomElement prop = doc.createElement(QString::fromLatin1(kPropertyTag));
    prop.setAttribute(QStringLiteral("name"), name);

    // 保持 Designer 习惯：property 节点排在其它结构节点之前
    QDomNode before;
    for (QDomNode n = widget.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (n.isElement() && n.toElement().tagName() != QString::fromLatin1(kPropertyTag)) {
            before = n;
            break;
        }
    }
    if (before.isNull())
        widget.appendChild(prop);
    else
        widget.insertBefore(prop, before);
    return prop;
}

void removeChildren(QDomElement element)
{
    while (!element.firstChild().isNull())
        element.removeChild(element.firstChild());
}

void appendTextElement(QDomElement &parent, const QString &tag, const QString &text)
{
    QDomDocument doc = parent.ownerDocument();
    QDomElement e = doc.createElement(tag);
    e.appendChild(doc.createTextNode(text));
    parent.appendChild(e);
}

QString objectNameOf(const QDomElement &widgetElement)
{
    const QDomElement prop = propertyElement(widgetElement, QString::fromLatin1(kObjectName));
    if (prop.isNull())
        return widgetElement.attribute(QStringLiteral("name"));
    const QDomElement str = prop.firstChildElement(QStringLiteral("string"));
    return str.isNull() ? prop.text() : str.text();
}

QRect rectOf(const QDomElement &widgetElement)
{
    const QDomElement prop = propertyElement(widgetElement, QStringLiteral("geometry"));
    if (prop.isNull())
        return QRect();
    const QDomElement rect = prop.firstChildElement(QStringLiteral("rect"));
    if (rect.isNull())
        return QRect();

    int x = 0, y = 0, w = 0, h = 0;
    for (QDomElement c = rect.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        const QString tag = c.tagName();
        const int value = c.text().toInt();
        if (tag == QStringLiteral("x"))
            x = value;
        else if (tag == QStringLiteral("y"))
            y = value;
        else if (tag == QStringLiteral("width"))
            w = value;
        else if (tag == QStringLiteral("height"))
            h = value;
    }
    return QRect(x, y, w, h);
}

bool setGeometryProperty(QDomElement &widgetElement, const QRect &rect)
{
    QDomElement prop = ensureProperty(widgetElement, QStringLiteral("geometry"));
    removeChildren(prop);

    QDomDocument doc = widgetElement.ownerDocument();
    QDomElement rectEl = doc.createElement(QStringLiteral("rect"));
    const struct { const char *tag; int value; } fields[] = {
        {"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}};
    for (const auto &f : fields) {
        QDomElement e = doc.createElement(QString::fromLatin1(f.tag));
        e.appendChild(doc.createTextNode(QString::number(f.value)));
        rectEl.appendChild(e);
    }
    prop.appendChild(rectEl);
    return true;
}

bool writePropertyValue(QDomElement &prop, const QJsonValue &value, QString *error)
{
    QDomDocument doc = prop.ownerDocument();
    auto fail = [error](const QString &text) {
        if (error)
            *error = text;
        return false;
    };

    QString type;
    QJsonValue payload = value;
    if (value.isObject() && value.toObject().contains(QStringLiteral("type"))) {
        type = value.toObject().value(QStringLiteral("type")).toString().trimmed();
        payload = value.toObject().value(QStringLiteral("value"));
    }

    if (type.isEmpty()) {
        switch (value.type()) {
        case QJsonValue::Bool:
            type = QStringLiteral("bool");
            break;
        case QJsonValue::Double:
            type = QStringLiteral("number");
            break;
        case QJsonValue::Array:
            type = QStringLiteral("stringlist");
            break;
        case QJsonValue::String:
            type = QStringLiteral("string");
            break;
        default:
            return fail(QStringLiteral("属性值类型不受支持，请使用 {\"type\":\"...\",\"value\":...} 形式"));
        }
    }

    removeChildren(prop);

    if (type == QLatin1String("string") || type == QLatin1String("cstring")
        || type == QLatin1String("url") || type == QLatin1String("enum")) {
        appendTextElement(prop, type, payload.toString());
        return true;
    }
    if (type == QLatin1String("number")) {
        const double d = payload.toDouble();
        const double rounded = qRound64(d) == d ? static_cast<double>(qRound64(d)) : d;
        appendTextElement(prop, type, QString::number(rounded, 'g', 16));
        return true;
    }
    if (type == QLatin1String("bool")) {
        appendTextElement(prop, type, payload.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
        return true;
    }
    if (type == QLatin1String("rect") || type == QLatin1String("size") || type == QLatin1String("point")) {
        if (!payload.isObject())
            return fail(QStringLiteral("%1 类型的 value 必须是对象").arg(type));
        const QJsonObject o = payload.toObject();
        const int x = o.value(QStringLiteral("x")).toInt();
        const int y = o.value(QStringLiteral("y")).toInt();
        const int w = o.contains(QStringLiteral("width")) ? o.value(QStringLiteral("width")).toInt()
                                                         : o.value(QStringLiteral("w")).toInt();
        const int h = o.contains(QStringLiteral("height")) ? o.value(QStringLiteral("height")).toInt()
                                                          : o.value(QStringLiteral("h")).toInt();
        QDomElement el = doc.createElement(type);
        if (type != QLatin1String("size")) {
            appendTextElement(el, QStringLiteral("x"), QString::number(x));
            appendTextElement(el, QStringLiteral("y"), QString::number(y));
        }
        if (type != QLatin1String("point")) {
            appendTextElement(el, QStringLiteral("width"), QString::number(w));
            appendTextElement(el, QStringLiteral("height"), QString::number(h));
        }
        prop.appendChild(el);
        return true;
    }
    if (type == QLatin1String("color")) {
        int r = 0, g = 0, b = 0, a = 255;
        if (payload.isString()) {
            const QColor c(payload.toString());
            if (!c.isValid())
                return fail(QStringLiteral("非法颜色值: %1").arg(payload.toString()));
            r = c.red();
            g = c.green();
            b = c.blue();
            a = c.alpha();
        } else if (payload.isObject()) {
            const QJsonObject o = payload.toObject();
            r = o.value(QStringLiteral("r")).toInt();
            g = o.value(QStringLiteral("g")).toInt();
            b = o.value(QStringLiteral("b")).toInt();
            a = o.value(QStringLiteral("a")).toInt(255);
        } else {
            return fail(QStringLiteral("color 类型的 value 必须是 #RRGGBB 或 {r,g,b,a}"));
        }
        QDomElement el = doc.createElement(QStringLiteral("color"));
        appendTextElement(el, QStringLiteral("red"), QString::number(r));
        appendTextElement(el, QStringLiteral("green"), QString::number(g));
        appendTextElement(el, QStringLiteral("blue"), QString::number(b));
        appendTextElement(el, QStringLiteral("alpha"), QString::number(a));
        prop.appendChild(el);
        return true;
    }
    if (type == QLatin1String("set") || type == QLatin1String("stringlist")) {
        if (!payload.isArray())
            return fail(QStringLiteral("%1 类型的 value 必须是字符串数组").arg(type));
        QDomElement el = doc.createElement(type);
        const QJsonArray arr = payload.toArray();
        for (const QJsonValue &v : arr)
            appendTextElement(el, QStringLiteral("string"), v.toString());
        prop.appendChild(el);
        return true;
    }

    return fail(QStringLiteral("不支持的属性类型: %1").arg(type));
}

QJsonValue readPropertyValue(const QDomElement &prop)
{
    const QDomElement child = prop.firstChildElement();
    if (child.isNull())
        return prop.text();

    const QString tag = child.tagName();
    if (tag == QStringLiteral("string") || tag == QStringLiteral("cstring")
        || tag == QStringLiteral("url") || tag == QStringLiteral("enum"))
        return child.text();
    if (tag == QStringLiteral("number"))
        return child.text().toDouble();
    if (tag == QStringLiteral("bool"))
        return child.text().trimmed() == QLatin1String("true");
    if (tag == QStringLiteral("rect") || tag == QStringLiteral("size")
        || tag == QStringLiteral("point")) {
        QJsonObject o;
        for (QDomElement c = child.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
            o.insert(c.tagName(), c.text().toDouble());
        return o;
    }
    if (tag == QStringLiteral("set") || tag == QStringLiteral("stringlist")) {
        QJsonArray arr;
        for (QDomElement c = child.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
            arr.append(c.text());
        return arr;
    }
    if (tag == QStringLiteral("color")) {
        QJsonObject o;
        for (QDomElement c = child.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
            o.insert(c.tagName(), c.text().toInt());
        return o;
    }
    return child.text();
}

void collectFromItem(const QDomElement &item, QVector<QDomElement> *out);
void collectFromLayout(const QDomElement &layout, QVector<QDomElement> *out);

void collectFromItem(const QDomElement &item, QVector<QDomElement> *out)
{
    for (QDomElement c = item.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (c.tagName() == QLatin1String(kWidgetTag))
            out->append(c);
        else if (c.tagName() == QLatin1String(kLayoutTag))
            collectFromLayout(c, out);
        else if (c.tagName() == QLatin1String(kItemTag))
            collectFromItem(c, out);
    }
}

void collectFromLayout(const QDomElement &layout, QVector<QDomElement> *out)
{
    for (QDomElement item = layout.firstChildElement(QStringLiteral("item")); !item.isNull();
         item = item.nextSiblingElement(QStringLiteral("item"))) {
        collectFromItem(item, out);
    }
}

QVector<QDomElement> directChildWidgets(const QDomElement &parent)
{
    QVector<QDomElement> result;
    for (QDomElement c = parent.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (c.tagName() == QLatin1String(kWidgetTag))
            result.append(c);
        else if (c.tagName() == QLatin1String(kLayoutTag))
            collectFromLayout(c, &result);
    }
    return result;
}

QDomElement directLayoutElement(const QDomElement &parent)
{
    for (QDomElement c = parent.firstChildElement(QStringLiteral("layout")); !c.isNull();
         c = c.nextSiblingElement(QStringLiteral("layout"))) {
        return c;
    }
    return QDomElement();
}

int itemCount(const QDomElement &layout)
{
    int count = 0;
    for (QDomElement item = layout.firstChildElement(QStringLiteral("item")); !item.isNull();
         item = item.nextSiblingElement(QStringLiteral("item"))) {
        ++count;
    }
    return count;
}

QString layoutClassName(const QDomElement &layout)
{
    return layout.attribute(QStringLiteral("class"));
}

QByteArray serialize(const QDomDocument &doc)
{
    QString text = doc.toString(4);
    const int end = text.indexOf(QLatin1String("?>"));
    if (text.startsWith(QLatin1String("<?xml")) && end > 0)
        text = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") + text.mid(end + 2);
    else
        text.prepend(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));

    QByteArray bytes = text.toUtf8();
    if (!bytes.endsWith('\n'))
        bytes.append('\n');
    return bytes;
}

} // namespace

// ---------------------------------------------------------------------------

UiDocument::UiDocument() = default;

bool UiDocument::parse(const QByteArray &xml, QString *error)
{
    m_doc = QDomDocument();
    m_valid = false;

    if (xml.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("UI 文件内容为空");
        return false;
    }

    QString msg;
    int line = 0;
    int column = 0;
    if (!m_doc.setContent(xml, false, &msg, &line, &column)) {
        if (error) {
            *error = QStringLiteral("UI XML 解析失败（第 %1 行第 %2 列）：%3")
                         .arg(line)
                         .arg(column)
                         .arg(msg);
        }
        return false;
    }

    const QDomElement root = m_doc.documentElement();
    if (root.tagName() != QLatin1String("ui")) {
        if (error)
            *error = QStringLiteral("不是 Qt Designer 的 .ui 文件（根节点为 <%1>）").arg(root.tagName());
        return false;
    }
    if (formWidgetElement().isNull()) {
        if (error)
            *error = QStringLiteral("UI 文件中没有顶层 <widget> 节点");
        return false;
    }

    m_valid = true;
    return true;
}

QByteArray UiDocument::toXml(QString *error) const
{
    Q_UNUSED(error)
    if (!m_valid)
        return QByteArray();
    return serialize(m_doc);
}

QDomElement UiDocument::formWidgetElement() const
{
    return m_doc.documentElement().firstChildElement(QStringLiteral("widget"));
}

QString UiDocument::formClass() const
{
    const QDomElement w = formWidgetElement();
    return w.isNull() ? QString() : w.attribute(QStringLiteral("class"));
}

QString UiDocument::formObjectName() const
{
    const QDomElement w = formWidgetElement();
    return w.isNull() ? QString() : objectNameOf(w);
}

QDomElement UiDocument::findWidgetElement(const QString &objectName) const
{
    const QDomElement root = formWidgetElement();
    if (root.isNull())
        return QDomElement();
    if (objectNameOf(root) == objectName)
        return root;

    QVector<QDomElement> pending = directChildWidgets(root);
    while (!pending.isEmpty()) {
        const QDomElement current = pending.takeFirst();
        if (objectNameOf(current) == objectName)
            return current;
        pending += directChildWidgets(current);
    }
    return QDomElement();
}

bool UiDocument::contains(const QString &objectName) const
{
    return !findWidgetElement(objectName).isNull();
}

QVector<WidgetNode> UiDocument::widgets() const
{
    QVector<WidgetNode> result;
    const QDomElement root = formWidgetElement();
    if (root.isNull())
        return result;

    QVector<QPair<QDomElement, QString>> pending;
    pending.append(qMakePair(root, QString()));
    while (!pending.isEmpty()) {
        const QPair<QDomElement, QString> current = pending.takeFirst();
        const QDomElement element = current.first;

        WidgetNode node;
        node.objectName = objectNameOf(element);
        node.className = element.attribute(QStringLiteral("class"));
        node.parentObjectName = current.second;
        node.geometry = rectOf(element);
        node.insideLayout = isInsideLayout(node.objectName);

        for (QDomElement p = element.firstChildElement(QStringLiteral("property")); !p.isNull();
             p = p.nextSiblingElement(QStringLiteral("property"))) {
            const QString name = p.attribute(QStringLiteral("name"));
            if (name.isEmpty() || name == QLatin1String(kObjectName) || name == QStringLiteral("geometry"))
                continue;
            node.properties.insert(name, readPropertyValue(p));
        }

        const QVector<QDomElement> children = directChildWidgets(element);
        for (const QDomElement &child : children) {
            node.children.append(objectNameOf(child));
            pending.append(qMakePair(child, node.objectName));
        }

        result.append(node);
    }
    return result;
}

bool UiDocument::widgetInfo(const QString &objectName, WidgetNode *out) const
{
    const QVector<WidgetNode> all = widgets();
    for (const WidgetNode &node : all) {
        if (node.objectName == objectName) {
            if (out)
                *out = node;
            return true;
        }
    }
    return false;
}

bool UiDocument::isInsideLayout(const QString &objectName) const
{
    QDomElement element = findWidgetElement(objectName);
    if (element.isNull() || element == formWidgetElement())
        return false;

    for (QDomNode parent = element.parentNode(); !parent.isNull(); parent = parent.parentNode()) {
        if (!parent.isElement())
            continue;
        const QString tag = parent.toElement().tagName();
        if (tag == QLatin1String(kItemTag) || tag == QLatin1String(kLayoutTag))
            return true;
    }
    return false;
}

QStringList UiDocument::layoutSummary() const
{
    QStringList summary;
    const QDomElement root = formWidgetElement();
    if (root.isNull())
        return summary;

    QVector<QDomElement> stack;
    stack.append(root);
    while (!stack.isEmpty()) {
        const QDomElement current = stack.takeFirst();
        const QDomElement layout = directLayoutElement(current);
        if (!layout.isNull()) {
            summary.append(QStringLiteral("%1(%2) -> %3 项")
                               .arg(layoutClassName(layout))
                               .arg(objectNameOf(current))
                               .arg(itemCount(layout)));
        }
        const QVector<QDomElement> children = directChildWidgets(current);
        for (const QDomElement &child : children)
            stack.append(child);
    }
    return summary;
}

QStringList UiDocument::connections() const
{
    QStringList result;
    const QDomElement connections = m_doc.documentElement().firstChildElement(QStringLiteral("connections"));
    if (connections.isNull())
        return result;

    for (QDomElement c = connections.firstChildElement(QStringLiteral("connection")); !c.isNull();
         c = c.nextSiblingElement(QStringLiteral("connection"))) {
        const QString sender = c.firstChildElement(QStringLiteral("sender")).text();
        const QString signal = c.firstChildElement(QStringLiteral("signal")).text();
        const QString receiver = c.firstChildElement(QStringLiteral("receiver")).text();
        const QString slot = c.firstChildElement(QStringLiteral("slot")).text();
        result.append(QStringLiteral("%1.%2 -> %3.%4").arg(sender, signal, receiver, slot));
    }
    return result;
}

bool UiDocument::addWidget(const WidgetSpec &spec, QString *error)
{
    if (!m_valid) {
        if (error)
            *error = QStringLiteral("UI 文档尚未加载");
        return false;
    }
    if (spec.type.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("widget_type 不能为空");
        return false;
    }
    if (spec.objectName.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("objectName 不能为空");
        return false;
    }
    if (contains(spec.objectName)) {
        if (error)
            *error = QStringLiteral("objectName 已存在：%1").arg(spec.objectName);
        return false;
    }

    const QDomElement root = formWidgetElement();
    QDomElement parentElement = root;

    if (!spec.parentObjectName.isEmpty()) {
        parentElement = findWidgetElement(spec.parentObjectName);
        if (parentElement.isNull()) {
            if (error)
                *error = QStringLiteral("父控件不存在：%1").arg(spec.parentObjectName);
            return false;
        }
    } else if (root.attribute(QStringLiteral("class")) == QLatin1String("QMainWindow")) {
        // QMainWindow 的控件应挂在 centralwidget 下
        const QVector<QDomElement> children = directChildWidgets(root);
        for (const QDomElement &child : children) {
            if (objectNameOf(child) == QLatin1String("centralwidget")) {
                parentElement = child;
                break;
            }
        }
    }

    QDomDocument doc = m_doc;
    QDomElement widgetElement = doc.createElement(QStringLiteral("widget"));
    widgetElement.setAttribute(QStringLiteral("class"), spec.type.trimmed());
    widgetElement.setAttribute(QStringLiteral("name"), spec.objectName.trimmed());

    QDomElement nameProp = ensureProperty(widgetElement, QString::fromLatin1(kObjectName));
    appendTextElement(nameProp, QStringLiteral("string"), spec.objectName.trimmed());

    QDomElement layout = directLayoutElement(parentElement);
    if (!layout.isNull()) {
        // 已使用布局：控件必须通过 <item> 插入，geometry 由布局接管
        QDomElement item = doc.createElement(QStringLiteral("item"));
        const QString layoutClass = layoutClassName(layout);
        if (layoutClass == QLatin1String("QGridLayout") || layoutClass == QLatin1String("QFormLayout")) {
            const int row = itemCount(layout);
            item.setAttribute(QStringLiteral("row"), QString::number(row));
            item.setAttribute(QStringLiteral("column"),
                               layoutClass == QLatin1String("QFormLayout") ? QStringLiteral("1")
                                                                           : QStringLiteral("0"));
        }
        item.appendChild(widgetElement);
        layout.appendChild(item);
    } else {
        setGeometryProperty(widgetElement, spec.geometry);
        parentElement.appendChild(widgetElement);
    }

    // 额外属性
    for (auto it = spec.properties.constBegin(); it != spec.properties.constEnd(); ++it) {
        if (it.key() == QLatin1String(kObjectName) || it.key() == QStringLiteral("geometry"))
            continue;
        QDomElement prop = ensureProperty(widgetElement, it.key());
        QString valueError;
        if (!writePropertyValue(prop, it.value(), &valueError)) {
            if (error)
                *error = QStringLiteral("属性 %1 写入失败：%2").arg(it.key(), valueError);
            return false;
        }
    }

    return true;
}

bool UiDocument::moveWidget(const QString &objectName, const QRect &rect, QString *error)
{
    QDomElement element = findWidgetElement(objectName);
    if (element.isNull()) {
        if (error)
            *error = QStringLiteral("控件不存在：%1").arg(objectName);
        return false;
    }
    if (element == formWidgetElement()) {
        if (error)
            *error = QStringLiteral("不能移动窗体顶层控件");
        return false;
    }
    setGeometryProperty(element, rect);
    return true;
}

bool UiDocument::removeWidget(const QString &objectName, QString *error)
{
    QDomElement element = findWidgetElement(objectName);
    if (element.isNull()) {
        if (error)
            *error = QStringLiteral("控件不存在：%1").arg(objectName);
        return false;
    }
    if (element == formWidgetElement()) {
        if (error)
            *error = QStringLiteral("不能删除窗体顶层控件");
        return false;
    }

    QDomNode parent = element.parentNode();
    if (parent.isNull()) {
        if (error)
            *error = QStringLiteral("控件没有父节点");
        return false;
    }
    if (parent.isElement() && parent.toElement().tagName() == QLatin1String(kItemTag))
        parent.parentNode().removeChild(parent);
    else
        parent.removeChild(element);

    // 清掉指向该控件的信号槽连接，避免 Designer 报“未知对象”
    QDomElement connections = m_doc.documentElement().firstChildElement(QStringLiteral("connections"));
    if (!connections.isNull()) {
        QVector<QDomElement> stale;
        for (QDomElement c = connections.firstChildElement(QStringLiteral("connection")); !c.isNull();
             c = c.nextSiblingElement(QStringLiteral("connection"))) {
            const QString sender = c.firstChildElement(QStringLiteral("sender")).text();
            const QString receiver = c.firstChildElement(QStringLiteral("receiver")).text();
            if (sender == objectName || receiver == objectName)
                stale.append(c);
        }
        for (const QDomElement &c : stale)
            connections.removeChild(c);
    }
    return true;
}

bool UiDocument::setProperty(const QString &objectName, const QString &key,
                             const QJsonValue &value, QString *error)
{
    if (key.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("prop_key 不能为空");
        return false;
    }
    if (key == QLatin1String(kObjectName)) {
        if (error)
            *error = QStringLiteral("objectName 不能通过属性修改（会影响信号槽绑定），请删旧建新");
        return false;
    }

    QDomElement element = findWidgetElement(objectName);
    if (element.isNull()) {
        if (error)
            *error = QStringLiteral("控件不存在：%1").arg(objectName);
        return false;
    }

    if (value.isNull() || value.isUndefined()) {
        const QDomElement existing = propertyElement(element, key);
        if (!existing.isNull())
            element.removeChild(existing);
        return true;
    }

    if (key == QStringLiteral("geometry")) {
        if (!value.isObject()) {
            if (error)
                *error = QStringLiteral("geometry 需要对象 {x,y,width,height}");
            return false;
        }
        const QJsonObject o = value.toObject();
        const int w = o.contains(QStringLiteral("width")) ? o.value(QStringLiteral("width")).toInt()
                                                         : o.value(QStringLiteral("w")).toInt();
        const int h = o.contains(QStringLiteral("height")) ? o.value(QStringLiteral("height")).toInt()
                                                          : o.value(QStringLiteral("h")).toInt();
        setGeometryProperty(element, QRect(o.value(QStringLiteral("x")).toInt(),
                                           o.value(QStringLiteral("y")).toInt(), w, h));
        return true;
    }

    QDomElement prop = ensureProperty(element, key);
    QString valueError;
    if (!writePropertyValue(prop, value, &valueError)) {
        if (error)
            *error = valueError;
        return false;
    }
    return true;
}

bool UiDocument::bindSignalSlot(const QString &senderObject, const QString &signal,
                                const QString &receiverObject, const QString &slot, QString *error)
{
    if (signal.trimmed().isEmpty() || slot.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("signal / slot 不能为空");
        return false;
    }
    if (!contains(senderObject) || !contains(receiverObject)) {
        if (error)
            *error = QStringLiteral("sender_obj 或 receiver_obj 不存在");
        return false;
    }

    QDomElement root = m_doc.documentElement();
    QDomElement connections = root.firstChildElement(QStringLiteral("connections"));
    if (connections.isNull()) {
        connections = m_doc.createElement(QStringLiteral("connections"));
        root.appendChild(connections);
    }

    for (QDomElement c = connections.firstChildElement(QStringLiteral("connection")); !c.isNull();
         c = c.nextSiblingElement(QStringLiteral("connection"))) {
        if (c.firstChildElement(QStringLiteral("sender")).text() == senderObject
            && c.firstChildElement(QStringLiteral("signal")).text() == signal
            && c.firstChildElement(QStringLiteral("receiver")).text() == receiverObject
            && c.firstChildElement(QStringLiteral("slot")).text() == slot) {
            if (error)
                *error = QStringLiteral("该信号槽连接已存在");
            return false;
        }
    }

    QDomElement connection = m_doc.createElement(QStringLiteral("connection"));
    appendTextElement(connection, QStringLiteral("sender"), senderObject);
    appendTextElement(connection, QStringLiteral("signal"), signal);
    appendTextElement(connection, QStringLiteral("receiver"), receiverObject);
    appendTextElement(connection, QStringLiteral("slot"), slot);
    connections.appendChild(connection);
    return true;
}

QByteArray UiDocument::createForm(const QString &uiName, const QString &baseClass,
                                  const QVector<WidgetSpec> &widgets, QString *error)
{
    const QString name = uiName.trimmed();
    if (name.isEmpty()) {
        if (error)
            *error = QStringLiteral("ui_name 不能为空");
        return QByteArray();
    }
    QString base = baseClass.trimmed();
    if (base.isEmpty())
        base = QStringLiteral("QWidget");
    static const QStringList allowed = {QStringLiteral("QWidget"), QStringLiteral("QDialog"),
                                        QStringLiteral("QMainWindow")};
    if (!allowed.contains(base)) {
        if (error)
            *error = QStringLiteral("base_class 仅支持 QWidget / QDialog / QMainWindow");
        return QByteArray();
    }

    QDomDocument doc;
    QDomElement ui = doc.createElement(QStringLiteral("ui"));
    ui.setAttribute(QStringLiteral("version"), QStringLiteral("4.0"));
    doc.appendChild(ui);

    QDomElement classEl = doc.createElement(QStringLiteral("class"));
    classEl.appendChild(doc.createTextNode(name));
    ui.appendChild(classEl);

    QDomElement root = doc.createElement(QStringLiteral("widget"));
    root.setAttribute(QStringLiteral("class"), base);
    root.setAttribute(QStringLiteral("name"), name);
    ui.appendChild(root);

    QDomElement nameProp = ensureProperty(root, QString::fromLatin1(kObjectName));
    appendTextElement(nameProp, QStringLiteral("string"), name);

    QRect bound(0, 0, 0, 0);
    for (const WidgetSpec &spec : widgets) {
        const QRect g = spec.geometry;
        bound = bound.united(QRect(g.x(), g.y(), g.width(), g.height()));
    }
    const QSize formSize(qMax(400, bound.right() + 24), qMax(300, bound.bottom() + 24));
    setGeometryProperty(root, QRect(0, 0, formSize.width(), formSize.height()));

    QDomElement titleProp = ensureProperty(root, QStringLiteral("windowTitle"));
    appendTextElement(titleProp, QStringLiteral("string"), name);

    QDomElement container = root;
    if (base == QLatin1String("QMainWindow")) {
        QDomElement central = doc.createElement(QStringLiteral("widget"));
        central.setAttribute(QStringLiteral("class"), QStringLiteral("QWidget"));
        central.setAttribute(QStringLiteral("name"), QStringLiteral("centralwidget"));
        QDomElement centralName = ensureProperty(central, QString::fromLatin1(kObjectName));
        appendTextElement(centralName, QStringLiteral("string"), QStringLiteral("centralwidget"));
        root.appendChild(central);
        container = central;
    }

    QStringList usedNames;
    for (const WidgetSpec &spec : widgets) {
        if (spec.objectName.trimmed().isEmpty()) {
            if (error)
                *error = QStringLiteral("widgets[] 中存在空的 objectName");
            return QByteArray();
        }
        if (usedNames.contains(spec.objectName)) {
            if (error)
                *error = QStringLiteral("objectName 重复：%1").arg(spec.objectName);
            return QByteArray();
        }
        usedNames.append(spec.objectName);

        QDomElement widgetElement = doc.createElement(QStringLiteral("widget"));
        widgetElement.setAttribute(QStringLiteral("class"), spec.type.trimmed());
        widgetElement.setAttribute(QStringLiteral("name"), spec.objectName.trimmed());

        QDomElement objName = ensureProperty(widgetElement, QString::fromLatin1(kObjectName));
        appendTextElement(objName, QStringLiteral("string"), spec.objectName.trimmed());
        setGeometryProperty(widgetElement, spec.geometry);

        for (auto it = spec.properties.constBegin(); it != spec.properties.constEnd(); ++it) {
            if (it.key() == QLatin1String(kObjectName) || it.key() == QStringLiteral("geometry"))
                continue;
            QDomElement prop = ensureProperty(widgetElement, it.key());
            QString valueError;
            if (!writePropertyValue(prop, it.value(), &valueError)) {
                if (error)
                    *error = QStringLiteral("属性 %1 写入失败：%2").arg(it.key(), valueError);
                return QByteArray();
            }
        }
        container.appendChild(widgetElement);
    }

    ui.appendChild(doc.createElement(QStringLiteral("resources")));
    ui.appendChild(doc.createElement(QStringLiteral("connections")));
    return serialize(doc);
}

} // namespace UiDom
} // namespace AiAssistant
