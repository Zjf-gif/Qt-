#include "settings/agentsettings.h"

#include <QSettings>

namespace AiAssistant {

const char *AgentSettings::settingsGroup()
{
    return "AiAssistant";
}

QString AgentSettings::endpoint() const
{
    QString url = baseUrl.trimmed();
    if (url.isEmpty())
        url = QStringLiteral("http://127.0.0.1:11434/v1/chat/completions");
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);

    if (url.contains(QStringLiteral("/api/chat")))       // Ollama 原生
        return url;
    if (url.endsWith(QStringLiteral("/chat/completions")))
        return url;
    if (url.endsWith(QStringLiteral("/v1")))
        return url + QStringLiteral("/chat/completions");
    if (url.contains(QStringLiteral("/v1/")))
        return url;
    return url + QStringLiteral("/v1/chat/completions");
}

bool AgentSettings::isOllamaNative() const
{
    return endpoint().contains(QStringLiteral("/api/chat"));
}

bool AgentSettings::looksConfigured() const
{
    return !endpoint().isEmpty() && !model.trimmed().isEmpty();
}

AgentSettings AgentSettings::load(QSettings *settings)
{
    AgentSettings s;
    if (!settings)
        return s;

    settings->beginGroup(QLatin1String(settingsGroup()));
    s.baseUrl = settings->value(QStringLiteral("BaseUrl"), s.baseUrl).toString();
    s.apiKey = settings->value(QStringLiteral("ApiKey")).toString();
    s.model = settings->value(QStringLiteral("Model"), s.model).toString();
    s.proxyUrl = settings->value(QStringLiteral("ProxyUrl")).toString();
    s.requestTimeoutSec = settings->value(QStringLiteral("TimeoutSec"), s.requestTimeoutSec).toInt();
    s.temperature = settings->value(QStringLiteral("Temperature"), s.temperature).toDouble();
    s.maxSnapshots = settings->value(QStringLiteral("MaxSnapshots"), s.maxSnapshots).toInt();
    s.maxToolIterations = settings->value(QStringLiteral("MaxToolIterations"), s.maxToolIterations).toInt();
    s.contextCharBudget = settings->value(QStringLiteral("ContextCharBudget"), s.contextCharBudget).toInt();
    s.persistHistory = settings->value(QStringLiteral("PersistHistory"), s.persistHistory).toBool();
    settings->endGroup();

    // 防止手工改坏的 ini 造成死循环或不可用状态
    s.requestTimeoutSec = qBound(5, s.requestTimeoutSec, 3600);
    s.maxSnapshots = qBound(1, s.maxSnapshots, 5000);
    s.maxToolIterations = qBound(1, s.maxToolIterations, 200);
    s.contextCharBudget = qBound(4000, s.contextCharBudget, 2000000);
    s.temperature = qBound(0.0, s.temperature, 2.0);
    return s;
}

void AgentSettings::save(QSettings *settings) const
{
    if (!settings)
        return;

    settings->beginGroup(QLatin1String(settingsGroup()));
    settings->setValue(QStringLiteral("BaseUrl"), baseUrl);
    settings->setValue(QStringLiteral("ApiKey"), apiKey);
    settings->setValue(QStringLiteral("Model"), model);
    settings->setValue(QStringLiteral("ProxyUrl"), proxyUrl);
    settings->setValue(QStringLiteral("TimeoutSec"), requestTimeoutSec);
    settings->setValue(QStringLiteral("Temperature"), temperature);
    settings->setValue(QStringLiteral("MaxSnapshots"), maxSnapshots);
    settings->setValue(QStringLiteral("MaxToolIterations"), maxToolIterations);
    settings->setValue(QStringLiteral("ContextCharBudget"), contextCharBudget);
    settings->setValue(QStringLiteral("PersistHistory"), persistHistory);
    settings->endGroup();
    settings->sync();
}

} // namespace AiAssistant
