/****************************************************************************
** 审批拦截器（安全核心）
**
** 设计原则（硬编码，无任何开关）：
**   1) 已知修改类工具 → 永远需要审批；
**   2) 注册表标记为非只读 → 需要审批；
**   3) 未在“只读白名单”里的未知工具 → 默认需要审批（default-deny）；
**   4) 没有安装审批 UI（Presenter）时，一律按“拒绝”处理（fail-closed）。
**
** 本类不含任何配置读取，也不提供 setEnabled/disable 之类的接口。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace AiAssistant {

class ApprovalInterceptor : public QObject
{
    Q_OBJECT
public:
    // 返回最终审批结果；实现方负责弹出模态审批对话框
    using Presenter = std::function<ApprovalDecision(const ApprovalRequest &request)>;

    explicit ApprovalInterceptor(QObject *parent = nullptr);

    // 铁律常量：编译期即保证审批永远开启
    static constexpr bool kApprovalAlwaysRequired = true;

    static bool isMutatingTool(const QString &toolName);
    static bool isKnownReadOnlyTool(const QString &toolName);
    static QStringList mutatingTools();
    static QStringList readOnlyTools();

    // 核心判定：registrySaysReadOnly 只是“辅助信息”，永远不能把修改类工具变成免审批
    static bool requiresApproval(const QString &toolName, bool registrySaysReadOnly);

    void setPresenter(Presenter presenter);
    bool hasPresenter() const { return static_cast<bool>(m_presenter); }

    ApprovalDecision review(const ApprovalRequest &request);

signals:
    void approvalRequested(const AiAssistant::ApprovalRequest &request);
    void approvalDecided(const AiAssistant::ApprovalRequest &request,
                         const AiAssistant::ApprovalDecision &decision);

private:
    Presenter m_presenter;
};

} // namespace AiAssistant
