#include "core/approvalinterceptor.h"

#include <QDebug>
#include <QSet>

namespace AiAssistant {

namespace {

const QSet<QString> &mutatingToolSet()
{
    static const QSet<QString> tools = {
        QStringLiteral("ui_add_widget"),
        QStringLiteral("ui_move_widget"),
        QStringLiteral("ui_modify_widget_property"),
        QStringLiteral("ui_bind_signal_slot"),
        QStringLiteral("ui_delete_widget"),
        QStringLiteral("create_ui_form"),
        QStringLiteral("write_file"),
        QStringLiteral("patch_file"),
        QStringLiteral("add_qt_class"),
        QStringLiteral("insert_code_at_cursor"),
        QStringLiteral("build_project"),
        QStringLiteral("run_project"),
    };
    return tools;
}

const QSet<QString> &readOnlyToolSet()
{
    static const QSet<QString> tools = {
        QStringLiteral("get_designer_current_ui_info"),
        QStringLiteral("get_selected_widget"),
        QStringLiteral("get_project_structure"),
        QStringLiteral("read_file"),
        QStringLiteral("get_build_errors"),
    };
    return tools;
}

} // namespace

ApprovalInterceptor::ApprovalInterceptor(QObject *parent)
    : QObject(parent)
{
    static_assert(kApprovalAlwaysRequired, "审批铁律不允许被关闭");
}

bool ApprovalInterceptor::isMutatingTool(const QString &toolName)
{
    return mutatingToolSet().contains(toolName);
}

bool ApprovalInterceptor::isKnownReadOnlyTool(const QString &toolName)
{
    return readOnlyToolSet().contains(toolName);
}

QStringList ApprovalInterceptor::mutatingTools()
{
    QStringList list = mutatingToolSet().values();
    list.sort();
    return list;
}

QStringList ApprovalInterceptor::readOnlyTools()
{
    QStringList list = readOnlyToolSet().values();
    list.sort();
    return list;
}

bool ApprovalInterceptor::requiresApproval(const QString &toolName, bool registrySaysReadOnly)
{
    // 1) 硬编码的修改类工具：无论注册表怎么说都必须审批
    if (isMutatingTool(toolName))
        return true;
    // 2) 注册表认为可写：审批
    if (!registrySaysReadOnly)
        return true;
    // 3) 注册表说只读，但不在只读白名单里（未知工具）：默认审批
    if (!isKnownReadOnlyTool(toolName))
        return true;
    // 4) 明确只读：放行
    return false;
}

void ApprovalInterceptor::setPresenter(Presenter presenter)
{
    m_presenter = std::move(presenter);
}

ApprovalDecision ApprovalInterceptor::review(const ApprovalRequest &request)
{
    ApprovalDecision decision;   // 默认 Rejected
    if (!request.isValid())
        return decision;

    emit approvalRequested(request);

    if (!m_presenter) {
        // fail-closed：没有审批 UI 就绝不执行
        qWarning("AiAssistant: 审批 UI 未安装，按拒绝处理（fail-closed）");
        decision.kind = ApprovalDecision::Rejected;
        decision.comment = tr("插件内部错误：审批界面不可用，操作已被拒绝");
        emit approvalDecided(request, decision);
        return decision;
    }

    decision = m_presenter(request);

    // 防御：Presenter 返回“执行”但没有带回调用时，使用原始调用
    if (decision.execute() && decision.calls.isEmpty())
        decision.calls = request.calls;

    emit approvalDecided(request, decision);
    return decision;
}

} // namespace AiAssistant
