#include "aiassistantplugin.h"

#include "constants.h"
#include "core/agentcontroller.h"
#include "core/agenttypes.h"
#include "core/approvalinterceptor.h"
#include "core/conversationstore.h"
#include "core/promptbuilder.h"
#include "core/snapshotmanager.h"
#include "creator/buildservice.h"
#include "creator/contextmenuhook.h"
#include "creator/designerbridge.h"
#include "creator/projectservice.h"
#include "llm/llmclient.h"
#include "settings/agentsettings.h"
#include "tools/buildtools.h"
#include "tools/codetools.h"
#include "tools/readtools.h"
#include "tools/tooldispatcher.h"
#include "tools/toolregistry.h"
#include "tools/toolruntime.h"
#include "tools/uitools.h"
#include "ui/aichatdock.h"
#include "ui/approvaldialog.h"
#include "ui/settingsdialog.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/icontext.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>

#include <algorithm>

using namespace AiAssistant;

// ---------------------------------------------------------------------------

AiAssistantPlugin::AiAssistantPlugin()
    : m_settings(new AgentSettings)
{
}

AiAssistantPlugin::~AiAssistantPlugin()
{
    if (m_llm)
        m_llm->cancel();

    if (m_dockWidget)
        m_dockWidget->setWidget(nullptr);   // 解除父子关系，避免二次析构
    delete m_dock;
    m_dock = nullptr;

    delete m_runtime;
    m_runtime = nullptr;
    delete m_registry;
    m_registry = nullptr;
    delete m_promptBuilder;
    m_promptBuilder = nullptr;
    delete m_settings;
    m_settings = nullptr;
}

bool AiAssistantPlugin::initialize(const QStringList &arguments, QString *errorString)
{
    Q_UNUSED(arguments)
    Q_UNUSED(errorString)

    AiAssistant::registerMetaTypes();

    QSettings *creatorSettings = Core::ICore::settings();
    *m_settings = AgentSettings::load(creatorSettings);

    // ---------------- 服务层 ----------------
    m_projectService = new ProjectService(this);
    m_designerBridge = new DesignerBridge(m_projectService, this);
    m_buildService = new BuildService(this);
    m_snapshotManager = new SnapshotManager(this);
    m_snapshotManager->setMaxBatches(m_settings->maxSnapshots);

    SnapshotManager::Hooks hooks;
    hooks.writeFile = [this](const QString &path, const QByteArray &data, QString *error) {
        return m_projectService->writeFileBytes(path, data, error);
    };
    hooks.deleteFile = [this](const QString &path, QString *error) {
        if (QFile::remove(path))
            return true;
        if (error)
            *error = tr("无法删除文件：%1").arg(path);
        return false;
    };
    hooks.restoreEditor = [this](const QString &path, const QByteArray &data, QString *error) {
        if (m_projectService->setDocumentContents(path, data, error)) {
            m_projectService->notifyFilesChanged({path});
            return true;
        }
        return m_projectService->writeFileBytes(path, data, error);
    };
    hooks.notifyChanged = [this](const QStringList &paths) {
        m_projectService->notifyFilesChanged(paths);
    };
    m_snapshotManager->setHooks(hooks);

    // ---------------- 工具层 ----------------
    m_runtime = new ToolRuntime;
    m_runtime->settings = m_settings;
    m_runtime->project = m_projectService;
    m_runtime->designer = m_designerBridge;
    m_runtime->build = m_buildService;
    m_runtime->snapshots = m_snapshotManager;
    m_runtime->log = [this](const QString &level, const QString &text) {
        if (m_dock)
            m_dock->appendToolLog(text, level);
    };

    m_registry = new ToolRegistry;
    m_readTools = new ReadTools(m_runtime, this);
    m_uiTools = new UiTools(m_runtime, this);
    m_codeTools = new CodeTools(m_runtime, this);
    m_buildTools = new BuildTools(m_runtime, this);
    m_readTools->registerTools(m_registry);
    m_uiTools->registerTools(m_registry);
    m_codeTools->registerTools(m_registry);
    m_buildTools->registerTools(m_registry);

    m_dispatcher = new ToolDispatcher(this);
    m_dispatcher->setRuntime(m_runtime);
    m_dispatcher->setRegistry(m_registry);

    // ---------------- 审批拦截器（硬编码、不可关闭）----------------
    m_interceptor = new ApprovalInterceptor(this);
    m_interceptor->setPresenter([this](const ApprovalRequest &request) {
        return presentApproval(request);
    });

    // ---------------- Agent / LLM / 持久化 ----------------
    m_promptBuilder = new PromptBuilder;
    m_llm = new LlmClient(this);
    m_store = new ConversationStore(this);
    if (creatorSettings)
        m_store->setSettingsFileName(creatorSettings->fileName());
    m_store->setEnabled(m_settings->persistHistory);
    refreshStoreProject();

    m_agent = new AgentController(this);
    m_agent->setServices(m_llm, m_dispatcher, m_registry, m_interceptor, m_promptBuilder,
                         m_store, m_projectService, m_designerBridge, m_settings);

    // ---------------- Dock ----------------
    setupDock();
    setupConnections();

    // ---------------- 右键菜单注入 ----------------
    m_menuHook = new ContextMenuHook(m_projectService, this);
    qApp->installEventFilter(m_menuHook);
    connect(m_menuHook, &ContextMenuHook::refactorSelectionRequested, this,
            &AiAssistantPlugin::handleRefactorRequest);
    connect(m_menuHook, &ContextMenuHook::designerWidgetRequested, this,
            &AiAssistantPlugin::handleDesignerRequest);

    setupActions();
    setupMenus();
    applySettings();

    m_agent->loadHistory();
    updateDockInfo();
    return true;
}

void AiAssistantPlugin::setupDock()
{
    m_dock = new AiChatDock;
}

void AiAssistantPlugin::setupConnections()
{
    // Agent → Dock
    connect(m_agent, &AgentController::messageAdded, this, &AiAssistantPlugin::onMessageAdded);
    connect(m_agent, &AgentController::stateChanged, this, &AiAssistantPlugin::onStateChanged);
    connect(m_agent, &AgentController::streamStarted, m_dock, &AiChatDock::beginStream);
    connect(m_agent, &AgentController::streamText, m_dock, &AiChatDock::appendStreamText);
    connect(m_agent, &AgentController::streamFinished, m_dock, &AiChatDock::endStream);
    connect(m_agent, &AgentController::toolLog, this,
            [this](const QString &level, const QString &text) {
                if (m_dock)
                    m_dock->appendToolLog(text, level);
            });
    connect(m_agent, &AgentController::errorOccurred, m_dock, &AiChatDock::appendError);
    connect(m_agent, &AgentController::approvalRecord, m_dock, &AiChatDock::appendApprovalRecord);
    connect(m_agent, &AgentController::historyCleared, m_dock, &AiChatDock::clearView);

    // Dock → Agent
    connect(m_dock, &AiChatDock::sendRequested, m_agent, &AgentController::sendUserMessage);
    connect(m_dock, &AiChatDock::stopRequested, m_agent, &AgentController::cancel);
    connect(m_dock, &AiChatDock::refreshContextRequested, m_agent, &AgentController::refreshContext);
    connect(m_dock, &AiChatDock::snapshotRequested, this, &AiAssistantPlugin::takeManualSnapshot);
    connect(m_dock, &AiChatDock::rollbackRequested, this, &AiAssistantPlugin::showRollbackDialog);
    connect(m_dock, &AiChatDock::settingsRequested, this, &AiAssistantPlugin::openSettings);
    connect(m_dock, &AiChatDock::clearHistoryRequested, this, [this] {
        const QMessageBox::StandardButton answer =
            QMessageBox::question(Core::ICore::mainWindow(), tr("清空对话"),
                                  tr("确定清空当前项目的对话历史吗？（快照不受影响）"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes)
            m_agent->clearHistory();
    });

    // 工具 / 快照 / 构建 信号
    connect(m_dispatcher, &ToolDispatcher::filesChanged, this, &AiAssistantPlugin::onFilesChanged);
    connect(m_snapshotManager, &SnapshotManager::batchesChanged, this,
            &AiAssistantPlugin::updateDockInfo);
    connect(m_projectService, &ProjectService::projectChanged, this,
            &AiAssistantPlugin::refreshStoreProject);
    connect(m_buildService, &BuildService::buildStarted, this, [this](const QString &title) {
        if (m_dock)
            m_dock->appendToolLog(tr("开始构建：%1").arg(title), QStringLiteral("info"));
    });
    connect(m_buildService, &BuildService::buildFinished, this, &AiAssistantPlugin::onBuildFinished);
    connect(m_buildService, &BuildService::runOutputCaptured, this, [this](const QString &output) {
        if (!m_dispatcher->hasPendingAsync()) {
            if (m_dock && !output.trimmed().isEmpty())
                m_dock->appendToolLog(tr("应用输出已更新"), QStringLiteral("info"));
            return;
        }
        ToolResult result;
        result.success = true;
        result.readOnly = false;
        if (output.trimmed().isEmpty()) {
            result.content = tr("运行已启动，但“应用程序输出”面板暂时没有内容"
                                "（程序可能还在启动，或没有向 stdout 输出）。");
        } else {
            const QString tail = output.size() > 8000 ? output.right(8000) : output;
            result.content = tr("应用输出（最多最近 8000 字符）：\n%1").arg(tail);
        }
        m_dispatcher->completeAsyncTool(result);
    });
}

void AiAssistantPlugin::extensionsInitialized()
{
    QMainWindow *mainWindow = Core::ICore::mainWindow();
    if (!mainWindow || !m_dock)
        return;

    m_dockWidget = new QDockWidget(tr("AI 助手"), mainWindow);
    m_dockWidget->setObjectName(QLatin1String(AiAssistant::Constants::AI_DOCK_ID));
    m_dockWidget->setWidget(m_dock);
    m_dockWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    mainWindow->addDockWidget(Qt::RightDockWidgetArea, m_dockWidget);

    updateDockInfo();
}

ExtensionSystem::IPlugin::ShutdownFlag AiAssistantPlugin::aboutToShutdown()
{
    if (m_agent)
        m_agent->cancel();
    if (m_llm)
        m_llm->cancel();
    if (m_store && m_agent)
        m_store->save(m_agent->history());

    // Dock 归主窗口所有，插件卸载时主动释放，避免悬挂
    if (m_dockWidget) {
        m_dockWidget->setWidget(nullptr);
        delete m_dockWidget;
        m_dockWidget = nullptr;
    }
    delete m_dock;
    m_dock = nullptr;

    return SynchronousShutdown;
}

// ---------------------------------------------------------------------------
// 菜单 / Action
// ---------------------------------------------------------------------------
void AiAssistantPlugin::setupActions()
{
    Core::ActionManager *actionManager = Core::ActionManager::instance();
    if (!actionManager)
        return;

    const Core::Context context(Core::Constants::C_GLOBAL);

    m_showAction = new QAction(tr("AI 助手面板"), this);
    Core::Command *showCommand = actionManager->registerAction(
        m_showAction, AiAssistant::Constants::SHOW_DOCK_ACTION, context);
    if (showCommand)
        showCommand->setDefaultKeySequence(QKeySequence(tr("Ctrl+Alt+A")));
    connect(m_showAction, &QAction::triggered, this, &AiAssistantPlugin::showDock);

    m_refactorAction = new QAction(tr("让AI重构选中代码"), this);
    Core::Command *refactorCommand = actionManager->registerAction(
        m_refactorAction, AiAssistant::Constants::REFACTOR_ACTION, context);
    if (refactorCommand)
        refactorCommand->setDefaultKeySequence(QKeySequence(tr("Ctrl+Alt+R")));
    connect(m_refactorAction, &QAction::triggered, this, [this] {
        if (!m_projectService)
            return;
        handleRefactorRequest(m_projectService->currentEditorFilePath(),
                              m_projectService->currentEditorSelection());
    });

    m_designerAction = new QAction(tr("让AI操作当前选中控件"), this);
    Core::Command *designerCommand = actionManager->registerAction(
        m_designerAction, AiAssistant::Constants::DESIGNER_ACTION, context);
    if (designerCommand)
        designerCommand->setDefaultKeySequence(QKeySequence(tr("Ctrl+Alt+D")));
    connect(m_designerAction, &QAction::triggered, this, [this] {
        if (!m_designerBridge)
            return;
        const QJsonObject info = m_designerBridge->selectedWidget();
        handleDesignerRequest(m_designerBridge->currentUiFile(),
                              info.value(QStringLiteral("objectName")).toString());
    });
}

void AiAssistantPlugin::setupMenus()
{
    Core::ActionManager *actionManager = Core::ActionManager::instance();
    if (!actionManager)
        return;

    Core::ActionContainer *toolsMenu =
        actionManager->actionContainer(Core::Constants::M_TOOLS);
    if (!toolsMenu)
        return;

    Core::ActionContainer *menu =
        actionManager->createMenu(AiAssistant::Constants::MENU_ID);
    if (!menu)
        return;

    menu->menu()->setTitle(tr("AI 助手"));
    if (Core::Command *command = actionManager->command(AiAssistant::Constants::SHOW_DOCK_ACTION))
        menu->addAction(command);
    if (Core::Command *command = actionManager->command(AiAssistant::Constants::REFACTOR_ACTION))
        menu->addAction(command);
    if (Core::Command *command = actionManager->command(AiAssistant::Constants::DESIGNER_ACTION))
        menu->addAction(command);

    toolsMenu->addMenu(menu);
}

// ---------------------------------------------------------------------------
// 设置
// ---------------------------------------------------------------------------
void AiAssistantPlugin::applySettings()
{
    if (!m_settings)
        return;

    if (m_llm)
        m_llm->setSettings(*m_settings);
    if (m_snapshotManager)
        m_snapshotManager->setMaxBatches(m_settings->maxSnapshots);
    if (m_store) {
        m_store->setEnabled(m_settings->persistHistory);
        refreshStoreProject();
    }
    updateDockInfo();
}

void AiAssistantPlugin::refreshStoreProject()
{
    if (!m_store || !m_projectService)
        return;
    m_store->setProjectDirectory(m_projectService->projectDirectory());
}

void AiAssistantPlugin::updateDockInfo()
{
    if (!m_dock)
        return;
    if (m_snapshotManager)
        m_dock->setSnapshotCount(m_snapshotManager->batchCount());
    if (m_settings)
        m_dock->setModelInfo(m_settings->model, m_settings->endpoint());
}

void AiAssistantPlugin::openSettings()
{
    if (!m_settings)
        return;

    SettingsDialog dialog(*m_settings, Core::ICore::mainWindow());
    if (dialog.exec() != QDialog::Accepted)
        return;

    *m_settings = dialog.settings();
    m_settings->save(Core::ICore::settings());
    applySettings();
    if (m_dock)
        m_dock->appendSystemNote(tr("设置已保存并立即生效"));
}

// ---------------------------------------------------------------------------
// Dock 相关
// ---------------------------------------------------------------------------
void AiAssistantPlugin::showDock()
{
    QMainWindow *mainWindow = Core::ICore::mainWindow();
    if (!m_dockWidget && mainWindow && m_dock) {
        m_dockWidget = new QDockWidget(tr("AI 助手"), mainWindow);
        m_dockWidget->setObjectName(QLatin1String(AiAssistant::Constants::AI_DOCK_ID));
        m_dockWidget->setWidget(m_dock);
        m_dockWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        mainWindow->addDockWidget(Qt::RightDockWidgetArea, m_dockWidget);
    }
    if (m_dockWidget) {
        m_dockWidget->show();
        m_dockWidget->raise();
    }
    if (m_dock)
        m_dock->focusInput();
}

void AiAssistantPlugin::onMessageAdded(const ChatMessage &message)
{
    if (m_dock)
        m_dock->appendChatMessage(message);
}

void AiAssistantPlugin::onStateChanged(AgentState state)
{
    if (m_dock)
        m_dock->setAgentState(state);
    if (m_showAction)
        m_showAction->setEnabled(true);
}

void AiAssistantPlugin::onFilesChanged(const QStringList &paths)
{
    if (!m_dock)
        return;
    QStringList names;
    for (const QString &path : paths) {
        const QString name = QFileInfo(path).fileName();
        if (!name.isEmpty() && !names.contains(name))
            names.append(name);
    }
    m_dock->appendToolLog(tr("已更新 %1 个文件：%2")
                              .arg(paths.size())
                              .arg(names.join(QStringLiteral(", "))),
                          QStringLiteral("info"));
    updateDockInfo();
}

void AiAssistantPlugin::onBuildFinished(bool success, const QString &summary)
{
    if (m_dock)
        m_dock->appendToolLog(summary, success ? QStringLiteral("info") : QStringLiteral("warn"));

    if (!m_dispatcher->hasPendingAsync())
        return;

    const QJsonObject report = m_buildService->lastBuildReport();
    QString content = summary;
    const QJsonArray errors = report.value(QStringLiteral("errors")).toArray();
    if (!errors.isEmpty()) {
        content += QStringLiteral("\n\n编译错误：");
        for (int i = 0; i < errors.size() && i < 20; ++i)
            content += QStringLiteral("\n- ") + errors.at(i).toString();
    }
    const QJsonArray warnings = report.value(QStringLiteral("warnings")).toArray();
    if (!warnings.isEmpty()) {
        content += QStringLiteral("\n\n编译警告：");
        for (int i = 0; i < warnings.size() && i < 10; ++i)
            content += QStringLiteral("\n- ") + warnings.at(i).toString();
    }
    if (errors.isEmpty() && warnings.isEmpty()) {
        const QString tail = report.value(QStringLiteral("tail")).toString();
        if (!tail.trimmed().isEmpty())
            content += QStringLiteral("\n\n输出末尾：\n") + tail;
    }

    ToolResult result;
    result.success = success;
    result.readOnly = false;
    result.content = content;
    m_dispatcher->completeAsyncTool(result);
}

// ---------------------------------------------------------------------------
// 审批
// ---------------------------------------------------------------------------
ApprovalDecision AiAssistantPlugin::presentApproval(const ApprovalRequest &request)
{
    ApprovalDecision decision;   // 默认拒绝
    if (m_dock)
        m_dock->appendApprovalRecord(tr("请求审批：%1").arg(request.title));

    ApprovalDialog dialog(request, Core::ICore::mainWindow());
    const int result = dialog.exec();
    if (result == QDialog::Accepted) {
        decision = dialog.decision();
    } else {
        decision.kind = ApprovalDecision::Rejected;
        decision.calls = request.calls;
    }
    return decision;
}

// ---------------------------------------------------------------------------
// 快照 / 回滚
// ---------------------------------------------------------------------------
void AiAssistantPlugin::takeManualSnapshot()
{
    if (!m_snapshotManager || !m_projectService) {
        return;
    }

    const ProjectSnapshot snapshot = m_projectService->snapshot();
    QStringList files = snapshot.openDocuments;
    if (files.isEmpty()) {
        const QString current = m_projectService->currentEditorFilePath();
        if (!current.isEmpty())
            files.append(current);
    }
    if (files.isEmpty()) {
        if (m_dock)
            m_dock->appendToolLog(tr("没有可快照的文件，请先在编辑器中打开文件"), QStringLiteral("warn"));
        return;
    }

    const QString batchId = m_snapshotManager->beginBatch(tr("手动快照"));
    for (const QString &file : files)
        m_snapshotManager->captureFile(batchId, file, QStringLiteral("manual_snapshot"));
    m_snapshotManager->commitBatch(batchId);

    if (m_dock)
        m_dock->appendApprovalRecord(tr("已创建手动快照：%1 个文件").arg(files.size()));
    updateDockInfo();
}

void AiAssistantPlugin::showRollbackDialog()
{
    if (!m_snapshotManager || !m_dock)
        return;

    const QVector<SnapshotBatch> batches = m_snapshotManager->batches();
    if (batches.isEmpty()) {
        m_dock->appendToolLog(tr("还没有任何快照"), QStringLiteral("warn"));
        return;
    }

    QStringList items;
    for (const SnapshotBatch &batch : batches)
        items.append(batch.displayText());

    bool ok = false;
    const QString chosen = QInputDialog::getItem(Core::ICore::mainWindow(), tr("回滚"),
                                                 tr("选择要恢复的快照（越靠上越早）："), items,
                                                 items.size() - 1, false, &ok);
    if (!ok || chosen.isEmpty())
        return;

    const int index = items.indexOf(chosen);
    if (index < 0)
        return;

    const SnapshotBatch batch = batches.at(index);
    const QMessageBox::StandardButton answer = QMessageBox::question(
        Core::ICore::mainWindow(), tr("确认回滚"),
        tr("将把以下 %1 个文件恢复到快照「%2」的状态，当前内容会被覆盖：\n\n%3")
            .arg(batch.items.size())
            .arg(batch.title, batch.fileSummary()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QStringList restored;
    QString error;
    if (!m_snapshotManager->rollbackBatch(batch.id, &restored, &error)) {
        m_dock->appendError(tr("回滚失败：%1").arg(error));
        return;
    }

    m_dock->appendApprovalRecord(tr("已回滚 %1 个文件").arg(restored.size()));
    updateDockInfo();
}

// ---------------------------------------------------------------------------
// 右键菜单触发的两条指令
// ---------------------------------------------------------------------------
void AiAssistantPlugin::handleRefactorRequest(const QString &filePath, const QString &selection)
{
    if (!m_dock || !m_agent)
        return;

    if (selection.trimmed().isEmpty()) {
        m_dock->appendToolLog(tr("请先在代码编辑器中选中要重构的代码"), QStringLiteral("warn"));
        m_dock->focusInput();
        return;
    }

    showDock();

    const QString relative = filePath.isEmpty() ? tr("(当前编辑器)")
                                                : m_projectService->relativeToProject(filePath);
    QString prompt = tr("请重构下面这段选中代码。\n"
                        "要求：\n"
                        "1) 严格保持 Qt5 兼容，不引入 Qt6 专属 API；\n"
                        "2) 不改变外部行为与函数签名（除非必要并说明原因）；\n"
                        "3) 用 patch_file 做局部替换，不要全量覆盖；\n"
                        "4) 完成后说明改了哪些点。\n\n"
                        "文件：%1\n```cpp\n%2\n```")
                         .arg(relative, selection);
    if (prompt.size() > 24000)
        prompt = prompt.left(24000) + QStringLiteral("\n```");

    m_agent->sendUserMessage(prompt);
}

void AiAssistantPlugin::handleDesignerRequest(const QString &uiFile, const QString &objectName)
{
    if (!m_dock || !m_agent || !m_designerBridge)
        return;

    const QJsonObject info = m_designerBridge->selectedWidget();
    if (!info.value(QStringLiteral("available")).toBool()) {
        m_dock->appendToolLog(info.value(QStringLiteral("note")).toString(), QStringLiteral("warn"));
        return;
    }

    showDock();

    QJsonObject summary;
    summary.insert(QStringLiteral("uiFile"), uiFile);
    summary.insert(QStringLiteral("objectName"), objectName);
    summary.insert(QStringLiteral("class"), info.value(QStringLiteral("class")));
    summary.insert(QStringLiteral("geometry"), info.value(QStringLiteral("geometry")));
    summary.insert(QStringLiteral("inLayout"), info.value(QStringLiteral("inLayout")));

    const QString prompt = tr("我在 Designer 里选中了控件 %1（类 %2）。\n"
                              "当前信息：%3\n\n"
                              "请根据我的意图操作它：\n"
                              "- 移动/改大小 → ui_move_widget（等于拖拽）\n"
                              "- 改属性 → ui_modify_widget_property\n"
                              "- 新增相关控件 → ui_add_widget\n"
                              "- 绑定信号槽 → ui_bind_signal_slot\n"
                              "ui_path 使用 %4。所有修改都会先给我审批。\n"
                              "如果我还没说具体要做什么，请先用一句话问我要做什么，并列出可行操作。")
                               .arg(objectName,
                                    info.value(QStringLiteral("class")).toString(),
                                    QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Compact)),
                                    uiFile);

    m_agent->sendUserMessage(prompt);
}
