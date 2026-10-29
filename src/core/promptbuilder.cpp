#include "core/promptbuilder.h"

#include "core/approvalinterceptor.h"

#include <QJsonDocument>

namespace AiAssistant {

QString PromptBuilder::systemPrompt()
{
    return QStringLiteral(
        "你是一名 Qt5 开发工程师，运行在 Qt Creator 4.15.0 AI 插件 Agent 中。"
        "宿主环境：Qt Creator 4.15.0，Qt 5.15，项目以 qmake(.pro) 为主。"
        "你拥有一组工具，包含读取项目、读取 Designer 画布 UI 控件、拖拽移动控件、新增控件、"
        "修改 UI 属性、绑定信号槽、读写源码、编译项目等能力。\n"
        "\n"
        "重要规则：\n"
        "1) 只读工具可以直接调用获取信息；所有修改 UI、修改代码、新增文件、删除控件的操作指令"
        "都会交给用户审批，用户不同意则无法执行。\n"
        "2) 你可以识别 Designer 画布中控件的 objectName、位置、大小、属性，你可以下达控件拖拽移动"
        "的指令，移动指令使用 ui_move_widget 工具。\n"
        "3) UI 文件必须生成 Qt5 标准 ui xml version=\"4.0\"，所有控件必须指定唯一 objectName。\n"
        "4) 优先使用 Qt5 兼容写法，禁止使用 Qt6 专属 API。\n"
        "5) 修改代码优先局部补丁替换（patch_file），不要全量覆盖；新增类自动更新 pro 的 "
        "HEADERS/SOURCES，新增 UI 自动更新 FORMS。\n"
        "6) 当你的操作被用户拒绝时，不要重复提交完全相同的指令，重新思考替代方案。\n"
        "7) 优先使用工具调用完成任务；仅当工具调用失败时才输出纯文本方案给用户审阅。\n");
}

QString PromptBuilder::toolGuide()
{
    QString text;
    text += QStringLiteral("\n【只读工具（直接执行）】\n");
    const QStringList readOnly = ApprovalInterceptor::readOnlyTools();
    for (const QString &name : readOnly)
        text += QStringLiteral("  - %1\n").arg(name);

    text += QStringLiteral("\n【修改类工具（必须人工审批，用户可改参数或拒绝）】\n");
    const QStringList mutating = ApprovalInterceptor::mutatingTools();
    for (const QString &name : mutating)
        text += QStringLiteral("  - %1\n").arg(name);

    text += QStringLiteral(
        "\n【坐标与布局】\n"
        "  - 父容器使用布局（QVBoxLayout/QHBoxLayout/QGridLayout/QFormLayout）时，控件位置由布局决定，"
        "改 geometry 不会生效；需要绝对定位时请先说明。\n"
        "  - ui_add_widget 的 props 支持两种写法：普通值（\"text\": \"确定\"），"
        "或显式类型（{\"type\":\"set\",\"value\":[\"Qt::AlignCenter\"]}）。\n"
        "\n【修改代码】\n"
        "  - patch_file 支持两种入参：unified diff（以 @@ 开头）或 edits 数组"
        "（[{\"oldText\":\"...\",\"newText\":\"...\"}]），优先使用它。\n"
        "  - write_file 是全量兜底，仅在新建文件或整文件重写时使用。\n");
    return text;
}

QString PromptBuilder::buildSystemMessage(const ProjectSnapshot &project,
                                          const QJsonObject &designerSelection) const
{
    QString message = systemPrompt();
    message += toolGuide();

    message += QStringLiteral("\n【当前工程上下文】\n");
    message += project.toSummary();

    if (!designerSelection.isEmpty()) {
        QString selection = QString::fromUtf8(
            QJsonDocument(designerSelection).toJson(QJsonDocument::Compact));
        if (selection.size() > 2500)
            selection = selection.left(2500) + QStringLiteral("…（已截断）");
        message += QStringLiteral("\n【Designer 当前状态】\n%1\n").arg(selection);
    }

    message += QStringLiteral(
        "\n【工作方式】\n"
        "  - 需要信息时先调用只读工具，不要凭空猜测文件内容或控件名。\n"
        "  - 一次可以连续提交多个修改类工具，它们会被合并到同一张审批单里。\n"
        "  - 每完成一步，用一句话说明结果；最终给出简短的验证建议（例如需要重新构建/运行）。\n");
    return message;
}

int PromptBuilder::charCount(const QVector<ChatMessage> &history)
{
    int sum = 0;
    for (const ChatMessage &message : history)
        sum += message.content.size();
    return sum;
}

QVector<ChatMessage> PromptBuilder::compact(const QVector<ChatMessage> &history, int charBudget)
{
    QVector<ChatMessage> messages = history;
    if (messages.isEmpty() || charBudget <= 0)
        return messages;

    // 1) 单条超长内容截断（主要是工具输出）
    for (ChatMessage &message : messages) {
        if (message.content.size() > 6000) {
            message.content = message.content.left(6000)
                              + QStringLiteral("\n…（内容过长，已截断；需要细节请再次调用工具）");
        }
    }

    // 2) 按轮次丢弃最老对话：一轮 = 一条 user 消息及其后的 assistant/tool 消息
    auto firstUserIndex = [](const QVector<ChatMessage> &list, int from) {
        for (int i = from; i < list.size(); ++i) {
            if (list.at(i).role == ChatRole::User)
                return i;
        }
        return -1;
    };

    while (charCount(messages) > charBudget) {
        const int firstUser = firstUserIndex(messages, 0);
        if (firstUser < 0)
            break;
        const int secondUser = firstUserIndex(messages, firstUser + 1);
        if (secondUser <= firstUser)
            break;   // 只剩最后一轮，不能再丢
        messages.remove(0, secondUser);
    }

    return messages;
}

} // namespace AiAssistant
