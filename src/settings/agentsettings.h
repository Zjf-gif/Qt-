/****************************************************************************
** 插件配置：全部落在 Creator 全局 settings（ICore::settings()）。
**
** 安全铁律：本结构体里 **不存在** 任何与“是否审批 / 是否自动执行”相关的字段，
** 配置页面也不会暴露此类开关。审批逻辑由 ApprovalInterceptor 硬编码。
****************************************************************************/

#pragma once

#include <QString>

class QSettings;

namespace AiAssistant {

struct AgentSettings
{
    // LLM
    QString baseUrl = QStringLiteral("http://127.0.0.1:11434/v1/chat/completions");
    QString apiKey;
    QString model = QStringLiteral("qwen2.5-coder:7b");
    QString proxyUrl;                 // 空 = 使用系统代理
    int requestTimeoutSec = 180;
    double temperature = 0.2;

    // 快照
    int maxSnapshots = 50;            // 快照批次上限，超限丢弃最早

    // Agent
    int maxToolIterations = 24;       // 单轮对话最大工具迭代次数，防止死循环
    int contextCharBudget = 60000;    // 上下文压缩阈值（字符）

    // 对话历史
    bool persistHistory = true;

    // --- 派生信息 -----------------------------------------------------------
    QString endpoint() const;         // 补全后的完整 URL
    bool isOllamaNative() const;      // /api/chat 原生协议
    bool looksConfigured() const;

    // --- 持久化（QSettings 由调用方传入 ICore::settings()）------------------
    static AgentSettings load(QSettings *settings);
    void save(QSettings *settings) const;

    static const char *settingsGroup();
};

} // namespace AiAssistant
