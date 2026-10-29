/****************************************************************************
** 插件入口
**
** 负责：
**   * 装配各层对象（UI / Agent / 审批 / 工具 / LLM / 持久化）
**   * 注册侧边 Dock、Tools 菜单项、编辑器与 Designer 右键菜单
**   * 从 ICore::settings() 加载配置
**   * 卸载时取消进行中的请求、保存历史、释放 Dock
****************************************************************************/

#pragma once

#include <extensionsystem/iplugin.h>

#include <QPointer>

class QAction;
class QDockWidget;

namespace AiAssistant {
struct AgentSettings;
struct ApprovalDecision;
struct ApprovalRequest;
struct ChatMessage;
struct ToolCall;
struct ToolResult;
enum class AgentState;

class AgentController;
class AiChatDock;
class ApprovalInterceptor;
class BuildService;
class BuildTools;
class CodeTools;
class ContextMenuHook;
class ConversationStore;
class DesignerBridge;
class LlmClient;
class ProjectService;
class PromptBuilder;
class ReadTools;
class SnapshotManager;
class ToolDispatcher;
struct ToolRuntime;
class ToolRegistry;
class UiTools;
} // namespace AiAssistant

class AiAssistantPlugin : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "AiAssistant.json")

public:
    AiAssistantPlugin();
    ~AiAssistantPlugin() override;

    bool initialize(const QStringList &arguments, QString *errorString) override;
    void extensionsInitialized() override;
    ShutdownFlag aboutToShutdown() override;

private slots:
    void onMessageAdded(const AiAssistant::ChatMessage &message);
    void onStateChanged(AiAssistant::AgentState state);
    void onFilesChanged(const QStringList &paths);
    void onBuildFinished(bool success, const QString &summary);

private:
    void setupDock();
    void setupActions();
    void setupMenus();
    void setupConnections();
    void applySettings();
    void refreshStoreProject();
    void updateDockInfo();

    AiAssistant::ApprovalDecision presentApproval(const AiAssistant::ApprovalRequest &request);

    void showDock();
    void takeManualSnapshot();
    void showRollbackDialog();
    void openSettings();
    void handleRefactorRequest(const QString &filePath, const QString &selection);
    void handleDesignerRequest(const QString &uiFile, const QString &objectName);

    AiAssistant::AgentSettings *m_settings = nullptr;
    AiAssistant::ToolRuntime *m_runtime = nullptr;

    AiAssistant::ProjectService *m_projectService = nullptr;
    AiAssistant::DesignerBridge *m_designerBridge = nullptr;
    AiAssistant::BuildService *m_buildService = nullptr;
    AiAssistant::SnapshotManager *m_snapshotManager = nullptr;
    AiAssistant::ToolRegistry *m_registry = nullptr;
    AiAssistant::ToolDispatcher *m_dispatcher = nullptr;
    AiAssistant::ApprovalInterceptor *m_interceptor = nullptr;
    AiAssistant::PromptBuilder *m_promptBuilder = nullptr;
    AiAssistant::LlmClient *m_llm = nullptr;
    AiAssistant::ConversationStore *m_store = nullptr;
    AiAssistant::AgentController *m_agent = nullptr;
    AiAssistant::ContextMenuHook *m_menuHook = nullptr;

    AiAssistant::ReadTools *m_readTools = nullptr;
    AiAssistant::UiTools *m_uiTools = nullptr;
    AiAssistant::CodeTools *m_codeTools = nullptr;
    AiAssistant::BuildTools *m_buildTools = nullptr;

    AiAssistant::AiChatDock *m_dock = nullptr;
    QPointer<QDockWidget> m_dockWidget;
    QAction *m_showAction = nullptr;
    QAction *m_refactorAction = nullptr;
    QAction *m_designerAction = nullptr;
};
