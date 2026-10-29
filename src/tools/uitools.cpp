#include "tools/uitools.h"

#include "creator/projectservice.h"
#include "tools/proeditor.h"
#include "tools/toolregistry.h"
#include "tools/toolschema.h"
#include "uidom/uidocument.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRect>
#include <QStringList>

namespace AiAssistant {

namespace {

struct LoadedUi
{
    bool ok = false;
    QString error;
    QString path;
    UiDom::UiDocument document;
};

QString argText(const ToolCall &call, const char *key)
{
    return call.arguments.value(QLatin1String(key)).toString().trimmed();
}

LoadedUi loadUi(ToolRuntime *runtime, PrepareContext &context, const QString &uiPath)
{
    LoadedUi result;
    if (!runtime || !runtime->project) {
        result.error = QStringLiteral("项目服务未初始化");
        return result;
    }
    if (uiPath.trimmed().isEmpty()) {
        result.error = QStringLiteral("缺少参数 ui_path");
        return result;
    }

    QString error;
    const QString resolved = runtime->project->resolvePath(uiPath, &error);
    if (resolved.isEmpty()) {
        result.error = error;
        return result;
    }
    if (!resolved.endsWith(QLatin1String(".ui"), Qt::CaseInsensitive)) {
        result.error = QStringLiteral("不是 .ui 文件：%1").arg(resolved);
        return result;
    }
    if (!context.exists(resolved)) {
        result.error = QStringLiteral("UI 文件不存在：%1").arg(resolved);
        return result;
    }
    if (runtime->project->isOpenAndModified(resolved)) {
        result.error = QStringLiteral("该 .ui 文件在 Creator 中已打开且有未保存的修改。"
                                      "请先保存（或放弃修改）后再让 AI 操作，避免覆盖你的改动");
        return result;
    }

    bool exists = false;
    const QByteArray data = context.readBytes(resolved, &exists, &error);
    if (!exists) {
        result.error = error.isEmpty() ? QStringLiteral("无法读取 %1").arg(resolved) : error;
        return result;
    }

    if (!result.document.parse(data, &error)) {
        result.error = QStringLiteral("解析 .ui 失败：%1").arg(error);
        return result;
    }

    result.path = resolved;
    result.ok = true;
    return result;
}

bool commitUi(PrepareContext &context, const LoadedUi &ui, ToolPlan *plan)
{
    QString error;
    const QByteArray xml = ui.document.toXml(&error);
    if (xml.isEmpty()) {
        plan->ok = false;
        plan->error = error.isEmpty() ? QStringLiteral("生成 .ui XML 失败") : error;
        return false;
    }
    context.writeBytes(ui.path, xml);
    plan->changedFiles.append(ui.path);
    plan->payload.insert(QStringLiteral("uiFile"), ui.path);
    return true;
}

// 把新建的 .ui / 类文件登记进 .pro（只追加一行，不改原有内容）
bool appendToProSection(ToolRuntime *runtime, PrepareContext &context, const QString &section,
                        const QString &filePath, bool *changed, QString *proPath)
{
    if (changed)
        *changed = false;
    if (!runtime || !runtime->project)
        return true;

    const ProjectSnapshot snapshot = runtime->project->snapshot();
    if (snapshot.projectFilePath.isEmpty())
        return true;

    bool exists = false;
    QString readError;
    const QByteArray content = context.readBytes(snapshot.projectFilePath, &exists, &readError);
    if (!exists)
        return true;

    bool present = false;
    const QString relative = ProFileEditor::relativeForPro(snapshot.projectFilePath, filePath);
    const QByteArray updated = ProFileEditor::appendEntry(content, section, relative, &present);
    if (present)
        return true;

    context.writeBytes(snapshot.projectFilePath, updated);
    if (changed)
        *changed = true;
    if (proPath)
        *proPath = snapshot.projectFilePath;
    return true;
}

QString shorten(const QString &text, int maxChars = 80)
{
    QString flat = text;
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (flat.size() > maxChars)
        flat = flat.left(maxChars) + QStringLiteral("…");
    return flat;
}

ToolPlan failure(const QString &error)
{
    ToolPlan plan;
    plan.ok = false;
    plan.error = error;
    return plan;
}

} // namespace

UiTools::UiTools(ToolRuntime *runtime, QObject *parent)
    : QObject(parent)
    , m_runtime(runtime)
{
}

void UiTools::registerTools(ToolRegistry *registry)
{
    if (!registry)
        return;

    ToolRuntime *runtime = m_runtime;

    // ---------------------------------------------------------------- 新增控件
    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("ui_add_widget"),
            QStringLiteral("在 .ui 里新增一个控件节点（修改 XML，不操作 Designer 画布）。父容器有布局时"
                           "自动插入 <item>；无布局时按 x/y/w/h 设置 <rect>。必须人工审批。"),
            Schema::object(
                {{QStringLiteral("ui_path"), Schema::prop(QStringLiteral("string"), QStringLiteral("目标 .ui 文件路径"))},
                 {QStringLiteral("widget_type"), Schema::prop(QStringLiteral("string"), QStringLiteral("Qt 控件类名，如 QPushButton"))},
                 {QStringLiteral("objectName"), Schema::prop(QStringLiteral("string"), QStringLiteral("唯一 objectName"))},
                 {QStringLiteral("x"), Schema::prop(QStringLiteral("integer"), QStringLiteral("左上角 x"))},
                 {QStringLiteral("y"), Schema::prop(QStringLiteral("integer"), QStringLiteral("左上角 y"))},
                 {QStringLiteral("w"), Schema::prop(QStringLiteral("integer"), QStringLiteral("宽度，默认 120"))},
                 {QStringLiteral("h"), Schema::prop(QStringLiteral("integer"), QStringLiteral("高度，默认 32"))},
                 {QStringLiteral("props"), Schema::prop(QStringLiteral("object"), QStringLiteral("额外属性，如 {\"text\":\"确定\"}"))},
                 {QStringLiteral("parent"), Schema::prop(QStringLiteral("string"), QStringLiteral("父控件 objectName，省略则挂到窗体顶层容器"))}},
                {QStringLiteral("ui_path"), QStringLiteral("widget_type"), QStringLiteral("objectName")}),
            false),
        [runtime](const ToolCall &call, PrepareContext &context) -> ToolPlan {
            ToolPlan plan;
            plan.kind = QStringLiteral("ui");

            const QString widgetType = argText(call, "widget_type");
            const QString objectName = argText(call, "objectName");
            if (widgetType.isEmpty() || objectName.isEmpty())
                return failure(QStringLiteral("widget_type 与 objectName 必填"));

            LoadedUi ui = loadUi(runtime, context, argText(call, "ui_path"));
            if (!ui.ok)
                return failure(ui.error);

            UiDom::WidgetSpec spec;
            spec.type = widgetType;
            spec.objectName = objectName;
            spec.parentObjectName = argText(call, "parent");
            spec.geometry = QRect(call.arguments.value(QStringLiteral("x")).toInt(0),
                                  call.arguments.value(QStringLiteral("y")).toInt(0),
                                  call.arguments.value(QStringLiteral("w")).toInt(120),
                                  call.arguments.value(QStringLiteral("h")).toInt(32));
            spec.properties = call.arguments.value(QStringLiteral("props")).toObject();

            QString error;
            if (!ui.document.addWidget(spec, &error))
                return failure(error);
            if (!commitUi(context, ui, &plan))
                return plan;

            plan.title = QStringLiteral("新增控件 %1（%2）").arg(objectName, widgetType);
            plan.summary = QStringLiteral("%1：新增 %2 \"%3\"，位置 (%4,%5)，大小 %6x%7")
                               .arg(QFileInfo(ui.path).fileName(), widgetType, objectName)
                               .arg(spec.geometry.x())
                               .arg(spec.geometry.y())
                               .arg(spec.geometry.width())
                               .arg(spec.geometry.height());
            if (ui.document.isInsideLayout(objectName))
                plan.summary += QStringLiteral("（父容器带布局，坐标由布局接管）");
            return plan;
        });

    // ---------------------------------------------------------------- 移动控件
    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("ui_move_widget"),
            QStringLiteral("等价于在 Designer 中拖拽控件：修改 .ui 里该控件的 <rect>（x/y/width/height）。"
                           "未提供的参数沿用当前值。必须人工审批。"),
            Schema::object(
                {{QStringLiteral("ui_path"), Schema::prop(QStringLiteral("string"), QStringLiteral("目标 .ui 文件路径"))},
                 {QStringLiteral("objectName"), Schema::prop(QStringLiteral("string"), QStringLiteral("要移动的控件 objectName"))},
                 {QStringLiteral("new_x"), Schema::prop(QStringLiteral("integer"), QStringLiteral("新的 x"))},
                 {QStringLiteral("new_y"), Schema::prop(QStringLiteral("integer"), QStringLiteral("新的 y"))},
                 {QStringLiteral("new_w"), Schema::prop(QStringLiteral("integer"), QStringLiteral("新的宽度"))},
                 {QStringLiteral("new_h"), Schema::prop(QStringLiteral("integer"), QStringLiteral("新的高度"))}},
                {QStringLiteral("ui_path"), QStringLiteral("objectName")}),
            false),
        [runtime](const ToolCall &call, PrepareContext &context) -> ToolPlan {
            ToolPlan plan;
            plan.kind = QStringLiteral("ui");

            const QString objectName = argText(call, "objectName");
            if (objectName.isEmpty())
                return failure(QStringLiteral("objectName 必填"));

            LoadedUi ui = loadUi(runtime, context, argText(call, "ui_path"));
            if (!ui.ok)
                return failure(ui.error);

            UiDom::WidgetNode node;
            if (!ui.document.widgetInfo(objectName, &node))
                return failure(QStringLiteral("控件不存在：%1").arg(objectName));

            const QRect before = node.geometry;
            QRect target = before;
            if (call.arguments.contains(QStringLiteral("new_x")))
                target.moveLeft(call.arguments.value(QStringLiteral("new_x")).toInt());
            if (call.arguments.contains(QStringLiteral("new_y")))
                target.moveTop(call.arguments.value(QStringLiteral("new_y")).toInt());
            if (call.arguments.contains(QStringLiteral("new_w")))
                target.setWidth(call.arguments.value(QStringLiteral("new_w")).toInt());
            if (call.arguments.contains(QStringLiteral("new_h")))
                target.setHeight(call.arguments.value(QStringLiteral("new_h")).toInt());

            QString error;
            if (!ui.document.moveWidget(objectName, target, &error))
                return failure(error);
            if (!commitUi(context, ui, &plan))
                return plan;

            const bool inLayout = ui.document.isInsideLayout(objectName);
            plan.title = QStringLiteral("移动控件 %1").arg(objectName);
            plan.summary = QStringLiteral("%1：\"%2\" 从 (%3,%4 %5x%6) 移到 (%7,%8 %9x%10)")
                               .arg(QFileInfo(ui.path).fileName(), objectName)
                               .arg(before.x())
                               .arg(before.y())
                               .arg(before.width())
                               .arg(before.height())
                               .arg(target.x())
                               .arg(target.y())
                               .arg(target.width())
                               .arg(target.height());
            if (inLayout)
                plan.summary += QStringLiteral("（该控件在布局中，geometry 不生效；如需自由摆放请先调整布局）");
            return plan;
        });

    // ---------------------------------------------------------------- 修改属性
    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("ui_modify_widget_property"),
            QStringLiteral("修改 .ui 中某控件的属性节点（如 text / enabled / placeholderText）。"
                           "prop_value 传 null 表示删除该属性。支持显式类型："
                           "{\"type\":\"rect\",\"value\":{...}}、{\"type\":\"set\",\"value\":[...]} 等。必须人工审批。"),
            Schema::object(
                {{QStringLiteral("ui_path"), Schema::prop(QStringLiteral("string"), QStringLiteral("目标 .ui 文件路径"))},
                 {QStringLiteral("objectName"), Schema::prop(QStringLiteral("string"), QStringLiteral("控件 objectName"))},
                 {QStringLiteral("prop_key"), Schema::prop(QStringLiteral("string"), QStringLiteral("属性名，如 text"))},
                 {QStringLiteral("prop_value"), Schema::prop(QStringLiteral("object"), QStringLiteral("属性值（字符串/数字/布尔/对象）"))}},
                {QStringLiteral("ui_path"), QStringLiteral("objectName"), QStringLiteral("prop_key")}),
            false),
        [runtime](const ToolCall &call, PrepareContext &context) -> ToolPlan {
            ToolPlan plan;
            plan.kind = QStringLiteral("ui");

            const QString objectName = argText(call, "objectName");
            const QString propKey = argText(call, "prop_key");
            if (objectName.isEmpty() || propKey.isEmpty())
                return failure(QStringLiteral("objectName 与 prop_key 必填"));

            LoadedUi ui = loadUi(runtime, context, argText(call, "ui_path"));
            if (!ui.ok)
                return failure(ui.error);

            UiDom::WidgetNode node;
            ui.document.widgetInfo(objectName, &node);
            const QJsonValue oldValue = node.properties.value(propKey);
            const QJsonValue newValue = call.arguments.value(QStringLiteral("prop_value"));

            QString error;
            if (!ui.document.setProperty(objectName, propKey, newValue, &error))
                return failure(error);
            if (!commitUi(context, ui, &plan))
                return plan;

            plan.title = QStringLiteral("修改属性 %1.%2").arg(objectName, propKey);
            plan.summary = QStringLiteral("%1：\"%2\".%3 %4 → %5")
                               .arg(QFileInfo(ui.path).fileName(), objectName, propKey,
                                    shorten(QString::fromUtf8(QJsonDocument(QJsonArray{oldValue}).toJson(QJsonDocument::Compact))),
                                    shorten(QString::fromUtf8(QJsonDocument(QJsonArray{newValue}).toJson(QJsonDocument::Compact))));
            return plan;
        });

    // ---------------------------------------------------------------- 绑定信号槽
    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("ui_bind_signal_slot"),
            QStringLiteral("在 .ui 的 <connections> 中新增一条信号槽连接（Designer 中“转到槽”的等价物）。"
                           "signal/slot 使用 Qt5 写法，例如 clicked() / on_pushButton_clicked()。必须人工审批。"),
            Schema::object(
                {{QStringLiteral("ui_path"), Schema::prop(QStringLiteral("string"), QStringLiteral("目标 .ui 文件路径"))},
                 {QStringLiteral("sender_obj"), Schema::prop(QStringLiteral("string"), QStringLiteral("发送者 objectName"))},
                 {QStringLiteral("signal"), Schema::prop(QStringLiteral("string"), QStringLiteral("信号签名，如 clicked()"))},
                 {QStringLiteral("receiver_obj"), Schema::prop(QStringLiteral("string"), QStringLiteral("接收者 objectName"))},
                 {QStringLiteral("slot"), Schema::prop(QStringLiteral("string"), QStringLiteral("槽签名，如 on_pushButton_clicked()"))}},
                {QStringLiteral("ui_path"), QStringLiteral("sender_obj"), QStringLiteral("signal"),
                 QStringLiteral("receiver_obj"), QStringLiteral("slot")}),
            false),
        [runtime](const ToolCall &call, PrepareContext &context) -> ToolPlan {
            ToolPlan plan;
            plan.kind = QStringLiteral("ui");

            const QString sender = argText(call, "sender_obj");
            const QString signal = argText(call, "signal");
            const QString receiver = argText(call, "receiver_obj");
            const QString slot = argText(call, "slot");
            if (sender.isEmpty() || receiver.isEmpty() || signal.isEmpty() || slot.isEmpty())
                return failure(QStringLiteral("sender_obj / signal / receiver_obj / slot 均必填"));

            LoadedUi ui = loadUi(runtime, context, argText(call, "ui_path"));
            if (!ui.ok)
                return failure(ui.error);

            QString error;
            if (!ui.document.bindSignalSlot(sender, signal, receiver, slot, &error))
                return failure(error);
            if (!commitUi(context, ui, &plan))
                return plan;

            plan.title = QStringLiteral("绑定信号槽 %1.%2 → %3.%4").arg(sender, signal, receiver, slot);
            plan.summary = QStringLiteral("%1：新增连接 %2.%3 → %4.%5")
                               .arg(QFileInfo(ui.path).fileName(), sender, signal, receiver, slot);
            return plan;
        });

    // ---------------------------------------------------------------- 删除控件
    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("ui_delete_widget"),
            QStringLiteral("删除 .ui 中的控件节点及其布局项，并清理指向它的 <connections>。"
                           "不允许删除窗体顶层控件。必须人工审批。"),
            Schema::object(
                {{QStringLiteral("ui_path"), Schema::prop(QStringLiteral("string"), QStringLiteral("目标 .ui 文件路径"))},
                 {QStringLiteral("objectName"), Schema::prop(QStringLiteral("string"), QStringLiteral("要删除的控件 objectName"))}},
                {QStringLiteral("ui_path"), QStringLiteral("objectName")}),
            false),
        [runtime](const ToolCall &call, PrepareContext &context) -> ToolPlan {
            ToolPlan plan;
            plan.kind = QStringLiteral("ui");

            const QString objectName = argText(call, "objectName");
            if (objectName.isEmpty())
                return failure(QStringLiteral("objectName 必填"));

            LoadedUi ui = loadUi(runtime, context, argText(call, "ui_path"));
            if (!ui.ok)
                return failure(ui.error);

            UiDom::WidgetNode node;
            if (!ui.document.widgetInfo(objectName, &node))
                return failure(QStringLiteral("控件不存在：%1").arg(objectName));

            QString error;
            if (!ui.document.removeWidget(objectName, &error))
                return failure(error);
            if (!commitUi(context, ui, &plan))
                return plan;

            plan.title = QStringLiteral("删除控件 %1").arg(objectName);
            plan.summary = QStringLiteral("%1：删除 %2 \"%3\"%4")
                               .arg(QFileInfo(ui.path).fileName(), node.className, objectName,
                                    node.children.isEmpty()
                                        ? QString()
                                        : QStringLiteral("（同时删除 %1 个子控件）").arg(node.children.size()));
            return plan;
        });

    // ---------------------------------------------------------------- 新建窗体
    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("create_ui_form"),
            QStringLiteral("新建一个 Qt5 标准 .ui 窗体（ui version=\"4.0\"），自动给所有控件写唯一 objectName，"
                           "并把文件登记进 .pro 的 FORMS。必须人工审批。"),
            Schema::object(
                {{QStringLiteral("ui_name"), Schema::prop(QStringLiteral("string"), QStringLiteral("窗体类名，也是默认文件名"))},
                 {QStringLiteral("base_class"), Schema::prop(QStringLiteral("string"), QStringLiteral("QWidget / QDialog / QMainWindow，默认 QWidget"))},
                 {QStringLiteral("save_path"), Schema::prop(QStringLiteral("string"), QStringLiteral("保存路径（.ui 文件或目录），相对项目目录"))},
                 {QStringLiteral("widgets"), Schema::prop(QStringLiteral("array"), QStringLiteral("控件数组：[{type,objectName,x,y,w,h,props}]"))}},
                {QStringLiteral("ui_name"), QStringLiteral("save_path")}),
            false),
        [runtime](const ToolCall &call, PrepareContext &context) -> ToolPlan {
            ToolPlan plan;
            plan.kind = QStringLiteral("ui");

            const QString uiName = argText(call, "ui_name");
            const QString savePath = argText(call, "save_path");
            if (uiName.isEmpty() || savePath.isEmpty())
                return failure(QStringLiteral("ui_name 与 save_path 必填"));
            if (!runtime || !runtime->project)
                return failure(QStringLiteral("项目服务未初始化"));

            QVector<UiDom::WidgetSpec> specs;
            const QJsonArray widgetArray = call.arguments.value(QStringLiteral("widgets")).toArray();
            for (const QJsonValue &entry : widgetArray) {
                const QJsonObject object = entry.toObject();
                UiDom::WidgetSpec spec;
                spec.type = object.value(QStringLiteral("type")).toString().trimmed();
                if (spec.type.isEmpty())
                    spec.type = object.value(QStringLiteral("widget_type")).toString().trimmed();
                spec.objectName = object.value(QStringLiteral("objectName")).toString().trimmed();
                if (spec.objectName.isEmpty())
                    spec.objectName = object.value(QStringLiteral("name")).toString().trimmed();
                spec.geometry = QRect(object.value(QStringLiteral("x")).toInt(0),
                                      object.value(QStringLiteral("y")).toInt(0),
                                      object.value(QStringLiteral("w")).toInt(120),
                                      object.value(QStringLiteral("h")).toInt(32));
                spec.properties = object.value(QStringLiteral("props")).toObject();
                spec.parentObjectName = object.value(QStringLiteral("parent")).toString().trimmed();
                if (spec.type.isEmpty() || spec.objectName.isEmpty())
                    return failure(QStringLiteral("widgets[] 中每项都需要 type 与 objectName"));
                specs.append(spec);
            }

            QString error;
            const QByteArray xml = UiDom::UiDocument::createForm(
                uiName, argText(call, "base_class"), specs, &error);
            if (xml.isEmpty())
                return failure(error);

            QString target = savePath;
            if (!target.endsWith(QLatin1String(".ui"), Qt::CaseInsensitive))
                target += QLatin1Char('/') + uiName + QStringLiteral(".ui");

            const QString resolved = runtime->project->resolvePath(target, &error);
            if (resolved.isEmpty())
                return failure(error);
            if (context.exists(resolved))
                return failure(QStringLiteral("目标文件已存在：%1").arg(resolved));

            context.writeBytes(resolved, xml);
            plan.changedFiles.append(resolved);

            bool proChanged = false;
            QString proPath;
            appendToProSection(runtime, context, QStringLiteral("FORMS"), resolved, &proChanged, &proPath);
            if (proChanged)
                plan.changedFiles.append(proPath);

            plan.title = QStringLiteral("新建窗体 %1").arg(uiName);
            plan.summary = QStringLiteral("创建 %1（%2 个控件）→ %3%4")
                               .arg(uiName)
                               .arg(specs.size())
                               .arg(runtime->project->relativeToProject(resolved),
                                    proChanged ? QStringLiteral("，并登记进 .pro 的 FORMS") : QString());
            return plan;
        });
}

} // namespace AiAssistant
