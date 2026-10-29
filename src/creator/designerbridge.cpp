#include "creator/designerbridge.h"

#include "creator/projectservice.h"
#include "uidom/uidocument.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDesignerFormEditorInterface>
#include <QDesignerFormWindowCursorInterface>
#include <QDesignerFormWindowInterface>
#include <QDesignerFormWindowManagerInterface>
#include <QGroupBox>
#include <QJsonArray>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMetaObject>
#include <QTabWidget>
#include <QWidget>

namespace AiAssistant {

namespace {

QDesignerFormWindowInterface *formWindowFromEditorInterface()
{
    QObject *app = QCoreApplication::instance();
    if (!app)
        return nullptr;

    const QList<QDesignerFormEditorInterface *> editors =
        app->findChildren<QDesignerFormEditorInterface *>();
    for (QDesignerFormEditorInterface *editor : editors) {
        if (!editor)
            continue;
        if (QDesignerFormWindowManagerInterface *manager = editor->formWindowManager()) {
            if (QDesignerFormWindowInterface *window = manager->activeFormWindow())
                return window;
        }
    }
    return nullptr;
}

QDesignerFormWindowInterface *formWindowFromWidgets()
{
    QDesignerFormWindowInterface *fallback = nullptr;
    const QWidgetList allWidgets = QApplication::allWidgets();
    for (QWidget *widget : allWidgets) {
        QDesignerFormWindowInterface *window = qobject_cast<QDesignerFormWindowInterface *>(widget);
        if (!window || !window->isVisible())
            continue;
        if (window->window() && window->window()->isActiveWindow())
            return window;
        fallback = window;
    }
    return fallback;
}

QDesignerFormWindowInterface *activeFormWindow()
{
    if (QDesignerFormWindowInterface *window = formWindowFromEditorInterface())
        return window;
    return formWindowFromWidgets();
}

bool isNoiseWidget(QWidget *widget)
{
    if (!widget)
        return true;
    const QString className = QString::fromLatin1(widget->metaObject()->className());
    if (className.startsWith(QLatin1String("QDesigner")))
        return true;
    if (className == QLatin1String("QScrollBar") || className == QLatin1String("QMenu"))
        return true;
    return false;
}

QJsonObject rectToJson(const QRect &rect)
{
    QJsonObject geometry;
    geometry.insert(QStringLiteral("x"), rect.x());
    geometry.insert(QStringLiteral("y"), rect.y());
    geometry.insert(QStringLiteral("width"), rect.width());
    geometry.insert(QStringLiteral("height"), rect.height());
    return geometry;
}

QJsonObject liveProperties(QWidget *widget)
{
    QJsonObject props;
    props.insert(QStringLiteral("toolTip"), widget->toolTip());
    props.insert(QStringLiteral("enabled"), widget->isEnabled());
    props.insert(QStringLiteral("visible"), widget->isVisible());
    props.insert(QStringLiteral("minimumSize"), rectToJson(QRect(QPoint(), widget->minimumSize())));
    props.insert(QStringLiteral("maximumSize"), rectToJson(QRect(QPoint(), widget->maximumSize())));

    if (QAbstractButton *button = qobject_cast<QAbstractButton *>(widget)) {
        props.insert(QStringLiteral("text"), button->text());
        props.insert(QStringLiteral("checkable"), button->isCheckable());
        props.insert(QStringLiteral("checked"), button->isChecked());
    }
    if (QLabel *label = qobject_cast<QLabel *>(widget))
        props.insert(QStringLiteral("text"), label->text());
    if (QLineEdit *lineEdit = qobject_cast<QLineEdit *>(widget)) {
        props.insert(QStringLiteral("text"), lineEdit->text());
        props.insert(QStringLiteral("placeholderText"), lineEdit->placeholderText());
        props.insert(QStringLiteral("readOnly"), lineEdit->isReadOnly());
    }
    if (QComboBox *comboBox = qobject_cast<QComboBox *>(widget)) {
        QStringList items;
        for (int i = 0; i < comboBox->count(); ++i)
            items.append(comboBox->itemText(i));
        props.insert(QStringLiteral("items"), QJsonArray::fromStringList(items));
        props.insert(QStringLiteral("currentIndex"), comboBox->currentIndex());
    }
    if (QGroupBox *groupBox = qobject_cast<QGroupBox *>(widget)) {
        props.insert(QStringLiteral("title"), groupBox->title());
        props.insert(QStringLiteral("checkable"), groupBox->isCheckable());
    }
    if (QAbstractSpinBox *spinBox = qobject_cast<QAbstractSpinBox *>(widget))
        props.insert(QStringLiteral("readOnly"), spinBox->isReadOnly());
    if (QTabWidget *tabWidget = qobject_cast<QTabWidget *>(widget)) {
        QStringList titles;
        for (int i = 0; i < tabWidget->count(); ++i)
            titles.append(tabWidget->tabText(i));
        props.insert(QStringLiteral("tabs"), QJsonArray::fromStringList(titles));
    }
    return props;
}

QJsonObject widgetToJson(QWidget *widget, QWidget *formContainer, int depth)
{
    QJsonObject json;
    json.insert(QStringLiteral("objectName"), widget->objectName());
    json.insert(QStringLiteral("class"), QString::fromLatin1(widget->metaObject()->className()));
    json.insert(QStringLiteral("geometry"), rectToJson(widget->geometry()));
    json.insert(QStringLiteral("sizeHint"), rectToJson(QRect(QPoint(), widget->sizeHint())));

    QWidget *parent = widget->parentWidget();
    json.insert(QStringLiteral("parent"), parent ? parent->objectName() : QString());

    bool inLayout = false;
    if (parent && parent->layout()) {
        QLayout *layout = parent->layout();
        inLayout = layout->indexOf(widget) >= 0;
        if (inLayout)
            json.insert(QStringLiteral("layout"), QString::fromLatin1(layout->metaObject()->className()));
    }
    json.insert(QStringLiteral("inLayout"), inLayout);

    if (widget->layout())
        json.insert(QStringLiteral("hasLayout"), QString::fromLatin1(widget->layout()->metaObject()->className()));

    json.insert(QStringLiteral("properties"), liveProperties(widget));

    QJsonArray children;
    if (depth < 6) {
        const QList<QWidget *> directChildren = widget->findChildren<QWidget *>(QString(),
                                                                                  Qt::FindDirectChildrenOnly);
        for (QWidget *child : directChildren) {
            if (isNoiseWidget(child))
                continue;
            if (child->objectName().isEmpty() || child->objectName().startsWith(QLatin1String("qt_")))
                continue;   // Designer 只把 objectName 非空的自定义控件暴露给用户
            children.append(widgetToJson(child, formContainer, depth + 1));
        }
    }
    json.insert(QStringLiteral("children"), children);
    return json;
}

QJsonArray collectWidgets(QWidget *formContainer)
{
    QJsonArray result;
    if (!formContainer)
        return result;

    const QList<QWidget *> directChildren =
        formContainer->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : directChildren) {
        if (isNoiseWidget(child))
            continue;
        if (child->objectName().isEmpty() || child->objectName().startsWith(QLatin1String("qt_")))
            continue;
        result.append(widgetToJson(child, formContainer, 0));
    }
    return result;
}

QJsonObject domWidgetToJson(const UiDom::WidgetNode &node, const UiDom::UiDocument &doc)
{
    QJsonObject json;
    json.insert(QStringLiteral("objectName"), node.objectName);
    json.insert(QStringLiteral("class"), node.className);
    json.insert(QStringLiteral("parent"), node.parentObjectName);
    json.insert(QStringLiteral("inLayout"), node.insideLayout);
    json.insert(QStringLiteral("geometry"), rectToJson(node.geometry));
    json.insert(QStringLiteral("properties"), node.properties);
    QJsonArray children;
    for (const QString &child : node.children)
        children.append(child);
    json.insert(QStringLiteral("children"), children);
    Q_UNUSED(doc)
    return json;
}

} // namespace

// ---------------------------------------------------------------------------

QJsonObject DesignerBridge::FormInfo::toJson() const
{
    QJsonObject json = data;
    json.insert(QStringLiteral("available"), available);
    json.insert(QStringLiteral("source"), source);
    json.insert(QStringLiteral("uiFile"), uiFile);
    json.insert(QStringLiteral("formClass"), formClass);
    json.insert(QStringLiteral("formObjectName"), formObjectName);
    json.insert(QStringLiteral("selectedObjectName"), selectedObjectName);
    if (!note.isEmpty())
        json.insert(QStringLiteral("note"), note);
    return json;
}

DesignerBridge::DesignerBridge(ProjectService *project, QObject *parent)
    : QObject(parent)
    , m_project(project)
{
}

bool DesignerBridge::designerAttached() const
{
    return activeFormWindow() != nullptr;
}

QString DesignerBridge::currentUiFile() const
{
    if (QDesignerFormWindowInterface *window = activeFormWindow()) {
        const QString fileName = window->fileName();
        if (!fileName.isEmpty())
            return fileName;
    }
    if (m_project) {
        const QString editorFile = m_project->currentEditorFilePath();
        if (editorFile.endsWith(QLatin1String(".ui")))
            return editorFile;
        const ProjectSnapshot snapshot = m_project->snapshot();
        if (!snapshot.files.forms.isEmpty())
            return snapshot.files.forms.first();
    }
    return QString();
}

DesignerBridge::FormInfo DesignerBridge::currentForm() const
{
    FormInfo info;

    if (QDesignerFormWindowInterface *window = activeFormWindow()) {
        info.available = true;
        info.source = QStringLiteral("designer");
        info.uiFile = window->fileName();
        QWidget *container = window->formContainer();
        info.formClass = container ? QString::fromLatin1(container->metaObject()->className()) : QString();
        info.formObjectName = container ? container->objectName() : QString();

        if (QDesignerFormWindowCursorInterface *cursor = window->cursor()) {
            if (QWidget *current = cursor->current())
                info.selectedObjectName = current->objectName();
        }

        QJsonObject data;
        data.insert(QStringLiteral("uiFile"), info.uiFile);
        data.insert(QStringLiteral("formClass"), info.formClass);
        data.insert(QStringLiteral("formObjectName"), info.formObjectName);
        data.insert(QStringLiteral("selectedObjectName"), info.selectedObjectName);
        data.insert(QStringLiteral("widgets"), collectWidgets(container));
        if (QDesignerFormWindowCursorInterface *cursor = window->cursor()) {
            QStringList selected;
            for (int i = 0; i < cursor->selectedWidgetCount(); ++i) {
                if (QWidget *widget = cursor->selectedWidget(i))
                    selected.append(widget->objectName());
            }
            data.insert(QStringLiteral("selectedWidgets"), QJsonArray::fromStringList(selected));
        }
        data.insert(QStringLiteral("dirty"), window->isDirty());
        data.insert(QStringLiteral("geometryNote"),
                    QStringLiteral("坐标由 Designer 画布实时采集；修改一律通过 ui_* 工具改 .ui XML"));
        info.data = data;
        return info;
    }

    // ---- 退化：读 .ui 文件 ----
    const QString uiFile = currentUiFile();
    if (uiFile.isEmpty()) {
        info.source = QStringLiteral("none");
        info.note = QStringLiteral("Designer 未打开窗体，且项目中没有可用的 .ui 文件");
        return info;
    }

    QString readError;
    const QString xml = m_project ? m_project->readTextFile(uiFile, &readError) : QString();
    if (xml.isEmpty()) {
        info.source = QStringLiteral("none");
        info.note = QStringLiteral("Designer 未打开窗体，且读取 .ui 失败：%1")
                        .arg(readError.isEmpty() ? QStringLiteral("文件为空或不可读") : readError);
        return info;
    }

    UiDom::UiDocument document;
    QString error;
    if (!document.parse(xml.toUtf8(), &error)) {
        info.source = QStringLiteral("none");
        info.note = QStringLiteral("Designer 未打开窗体，且读取 .ui 失败：%1").arg(error);
        return info;
    }

    info.available = true;
    info.source = QStringLiteral("file");
    info.uiFile = uiFile;
    info.formClass = document.formClass();
    info.formObjectName = document.formObjectName();
    info.note = QStringLiteral("Designer 未打开该窗体，数据来自 .ui 文件（坐标可能不是最新拖拽结果）");

    QJsonObject data;
    data.insert(QStringLiteral("uiFile"), uiFile);
    data.insert(QStringLiteral("formClass"), info.formClass);
    data.insert(QStringLiteral("formObjectName"), info.formObjectName);
    QJsonArray widgets;
    const QVector<UiDom::WidgetNode> nodes = document.widgets();
    for (const UiDom::WidgetNode &node : nodes)
        widgets.append(domWidgetToJson(node, document));
    data.insert(QStringLiteral("widgets"), widgets);
    data.insert(QStringLiteral("layouts"), QJsonArray::fromStringList(document.layoutSummary()));
    data.insert(QStringLiteral("connections"), QJsonArray::fromStringList(document.connections()));
    info.data = data;
    return info;
}

QJsonObject DesignerBridge::selectedWidget() const
{
    QJsonObject result;

    if (QDesignerFormWindowInterface *window = activeFormWindow()) {
        QWidget *current = window->cursor() ? window->cursor()->current() : nullptr;
        if (!current) {
            result.insert(QStringLiteral("available"), false);
            result.insert(QStringLiteral("note"),
                          QStringLiteral("Designer 已打开窗体但未选中控件，请先在画布上点选一个控件"));
            return result;
        }

        QJsonObject json = widgetToJson(current, window->formContainer(), 0);
        json.insert(QStringLiteral("available"), true);
        json.insert(QStringLiteral("source"), QStringLiteral("designer"));
        json.insert(QStringLiteral("uiFile"), window->fileName());
        json.insert(QStringLiteral("hint"),
                    QStringLiteral("修改该控件请调用 ui_* 工具，ui_path 使用本消息中的 uiFile，objectName 使用 selectedObjectName"));
        return json;
    }

    const QString uiFile = currentUiFile();
    if (!uiFile.isEmpty()) {
        result.insert(QStringLiteral("available"), false);
        result.insert(QStringLiteral("source"), QStringLiteral("file"));
        result.insert(QStringLiteral("uiFile"), uiFile);
        result.insert(QStringLiteral("note"),
                      QStringLiteral("Designer 画布未激活，无法获取“当前选中控件”。"
                                     "请先打开 %1 并选中控件，或先用 get_designer_current_ui_info 查看全部控件")
                          .arg(uiFile));
        return result;
    }

    result.insert(QStringLiteral("available"), false);
    result.insert(QStringLiteral("note"), QStringLiteral("没有可用的 Designer 窗体或 .ui 文件"));
    return result;
}

} // namespace AiAssistant
