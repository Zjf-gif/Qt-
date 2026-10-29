#include "tools/buildtools.h"

#include "creator/buildservice.h"
#include "creator/projectservice.h"
#include "tools/toolregistry.h"
#include "tools/toolschema.h"

namespace AiAssistant {

namespace {

ToolPlan failure(const QString &error)
{
    ToolPlan plan;
    plan.ok = false;
    plan.error = error;
    return plan;
}

} // namespace

BuildTools::BuildTools(ToolRuntime *runtime, QObject *parent)
    : QObject(parent)
    , m_runtime(runtime)
{
}

void BuildTools::registerTools(ToolRegistry *registry)
{
    if (!registry)
        return;

    ToolRuntime *runtime = m_runtime;

    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("build_project"),
            QStringLiteral("触发 Creator 构建（使用 Creator 自己的构建队列，构建输出仍在“编译输出”面板）。"
                           "构建是异步的：工具结果在构建结束后自动回填给模型。必须人工审批。"),
            Schema::object(
                {{QStringLiteral("target"), Schema::prop(QStringLiteral("string"), QStringLiteral("构建目标/项目名，省略则构建启动项目"))}}),
            false),
        [runtime](const ToolCall &call, PrepareContext &context) -> ToolPlan {
            Q_UNUSED(context)
            ToolPlan plan;
            plan.kind = QStringLiteral("build");
            plan.externalAction = true;

            if (!runtime || !runtime->project || !runtime->project->hasProject())
                return failure(QStringLiteral("当前没有激活项目，无法构建"));
            if (runtime->build && runtime->build->isBuildInProgress())
                return failure(QStringLiteral("已有构建在进行中，请等待结束后再试"));

            const QString target = call.arguments.value(QStringLiteral("target")).toString().trimmed();
            plan.payload.insert(QStringLiteral("target"), target);
            plan.title = QStringLiteral("构建项目");
            plan.summary = target.isEmpty()
                               ? QStringLiteral("触发 Creator 构建当前启动项目")
                               : QStringLiteral("触发 Creator 构建：%1").arg(target);
            return plan;
        });

    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("run_project"),
            QStringLiteral("运行当前项目（触发 Creator 的运行命令），并在启动后采集“应用程序输出”面板内容。"
                           "必须人工审批。"),
            Schema::emptyObject(),
            false),
        [runtime](const ToolCall &call, PrepareContext &context) -> ToolPlan {
            Q_UNUSED(call)
            Q_UNUSED(context)
            ToolPlan plan;
            plan.kind = QStringLiteral("run");
            plan.externalAction = true;

            if (!runtime || !runtime->project || !runtime->project->hasProject())
                return failure(QStringLiteral("当前没有激活项目，无法运行"));

            plan.title = QStringLiteral("运行项目");
            plan.summary = QStringLiteral("触发 Creator 运行当前项目，并采集应用输出");
            return plan;
        });
}

} // namespace AiAssistant
