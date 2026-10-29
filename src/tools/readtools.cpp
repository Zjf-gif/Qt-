#include "tools/readtools.h"

#include "creator/buildservice.h"
#include "creator/designerbridge.h"
#include "creator/projectservice.h"
#include "tools/toolregistry.h"
#include "tools/toolschema.h"
#include "tools/toolruntime.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QStringList>

namespace AiAssistant {

namespace {

QString jsonText(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)).trimmed();
}

ToolResult jsonResult(const QJsonObject &object)
{
    return ToolResult::ok(jsonText(object), true);
}

QString numberedSlice(const QStringList &lines, int startLine, int endLine)
{
    QStringList output;
    const int first = qMax(1, startLine);
    const int last = qMin(lines.size(), endLine <= 0 ? lines.size() : endLine);
    for (int i = first; i <= last; ++i) {
        output.append(QStringLiteral("%1 | %2")
                          .arg(i, 5)
                          .arg(lines.at(i - 1)));
    }
    return output.join(QLatin1Char('\n'));
}

} // namespace

ReadTools::ReadTools(ToolRuntime *runtime, QObject *parent)
    : QObject(parent)
    , m_runtime(runtime)
{
}

void ReadTools::registerTools(ToolRegistry *registry)
{
    if (!registry)
        return;

    ToolRuntime *runtime = m_runtime;

    registry->addReadTool(
        ToolRegistry::makeSpec(
            QStringLiteral("get_designer_current_ui_info"),
            QStringLiteral("读取 Designer 当前窗体（或最近打开的 .ui 文件）的全部控件信息："
                           "objectName、类名、坐标、大小、布局、常用属性、信号槽连接。只读，不修改任何内容。"),
            Schema::emptyObject(), true),
        [runtime](const ToolCall &call, PrepareContext &) -> ToolResult {
            Q_UNUSED(call)
            if (!runtime || !runtime->designer)
                return ToolResult::fail(QStringLiteral("Designer 桥未初始化"), true);
            const DesignerBridge::FormInfo info = runtime->designer->currentForm();
            return jsonResult(info.toJson());
        });

    registry->addReadTool(
        ToolRegistry::makeSpec(
            QStringLiteral("get_selected_widget"),
            QStringLiteral("读取 Designer 画布上当前选中控件的详细信息（objectName、类名、几何、属性、子控件）。"
                           "只读。若画布未激活会返回 available=false 与提示。"),
            Schema::emptyObject(), true),
        [runtime](const ToolCall &call, PrepareContext &) -> ToolResult {
            Q_UNUSED(call)
            if (!runtime || !runtime->designer)
                return ToolResult::fail(QStringLiteral("Designer 桥未初始化"), true);
            return jsonResult(runtime->designer->selectedWidget());
        });

    registry->addReadTool(
        ToolRegistry::makeSpec(
            QStringLiteral("get_project_structure"),
            QStringLiteral("读取当前激活项目的结构：项目名、目录、.pro 路径、"
                           "HEADERS/SOURCES/FORMS/RESOURCES 列表、已打开文档。只读。"),
            Schema::emptyObject(), true),
        [runtime](const ToolCall &call, PrepareContext &) -> ToolResult {
            Q_UNUSED(call)
            if (!runtime || !runtime->project)
                return ToolResult::fail(QStringLiteral("项目服务未初始化"), true);
            const ProjectSnapshot snapshot = runtime->project->refresh();
            return jsonResult(snapshot.toJson());
        });

    registry->addReadTool(
        ToolRegistry::makeSpec(
            QStringLiteral("read_file"),
            QStringLiteral("读取文本文件（h/cpp/ui/pro/qrc 等）。path 可为相对项目目录的路径或绝对路径。"
                           "可选 start_line/end_line 指定行区间（1 基，含首尾）。只读。"),
            Schema::object({{QStringLiteral("path"), Schema::prop(QStringLiteral("string"), QStringLiteral("文件路径，支持相对项目目录"))},
                            {QStringLiteral("start_line"), Schema::prop(QStringLiteral("integer"), QStringLiteral("起始行（1 基，可选）"))},
                            {QStringLiteral("end_line"), Schema::prop(QStringLiteral("integer"), QStringLiteral("结束行（含，可选）"))},
                            {QStringLiteral("max_bytes"), Schema::prop(QStringLiteral("integer"), QStringLiteral("最大读取字节数，默认 524288"))}},
                           {QStringLiteral("path")}),
        true),
        [runtime](const ToolCall &call, PrepareContext &) -> ToolResult {
            if (!runtime || !runtime->project)
                return ToolResult::fail(QStringLiteral("项目服务未初始化"), true);

            const QString rawPath = call.arguments.value(QStringLiteral("path")).toString();
            if (rawPath.trimmed().isEmpty())
                return ToolResult::fail(QStringLiteral("缺少参数 path"), true);

            QString error;
            const QString resolved = runtime->project->resolvePath(rawPath, &error);
            if (resolved.isEmpty())
                return ToolResult::fail(error, true);
            if (!QFileInfo::exists(resolved))
                return ToolResult::fail(QStringLiteral("文件不存在：%1").arg(resolved), true);

            const int maxBytes = call.arguments.value(QStringLiteral("max_bytes")).toInt(524288);
            QString text = runtime->project->readTextFile(resolved, &error, maxBytes > 0 ? maxBytes : 524288);
            if (text.isEmpty() && !error.isEmpty())
                return ToolResult::fail(error, true);

            const QStringList lines = text.split(QLatin1Char('\n'));
            const int startLine = call.arguments.value(QStringLiteral("start_line")).toInt(1);
            const int endLine = call.arguments.value(QStringLiteral("end_line")).toInt(0);

            QJsonObject result;
            result.insert(QStringLiteral("path"), runtime->project->relativeToProject(resolved));
            result.insert(QStringLiteral("absolutePath"), resolved);
            result.insert(QStringLiteral("lineCount"), lines.size());
            result.insert(QStringLiteral("byteCount"), text.toUtf8().size());

            if (startLine > 1 || endLine > 0) {
                result.insert(QStringLiteral("startLine"), qMax(1, startLine));
                result.insert(QStringLiteral("content"),
                              numberedSlice(lines, startLine, endLine));
            } else if (lines.size() > 1200) {
                result.insert(QStringLiteral("content"), numberedSlice(lines, 1, 1200));
                result.insert(QStringLiteral("truncated"), true);
                result.insert(QStringLiteral("hint"),
                              QStringLiteral("文件超过 1200 行，仅返回前 1200 行；"
                                             "可传 start_line/end_line 分段读取"));
            } else {
                result.insert(QStringLiteral("content"), numberedSlice(lines, 1, lines.size()));
            }
            return jsonResult(result);
        });

    registry->addReadTool(
        ToolRegistry::makeSpec(
            QStringLiteral("get_build_errors"),
            QStringLiteral("读取最近一次构建的错误与警告（解析 Creator 的编译输出面板，"
                           "支持 MSVC 与 GCC/Clang 两种格式）。只读。"),
            Schema::emptyObject(), true),
        [runtime](const ToolCall &call, PrepareContext &) -> ToolResult {
            Q_UNUSED(call)
            if (!runtime || !runtime->build)
                return ToolResult::fail(QStringLiteral("构建服务未初始化"), true);
            return jsonResult(runtime->build->lastBuildReport());
        });
}

} // namespace AiAssistant
