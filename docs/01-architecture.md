# 01 · 分层架构与关键类设计

## 1. 分层

```
┌──────────────────────────────────────────────────────────────┐
│ 插件入口层  AiAssistantPlugin (ExtensionSystem::IPlugin)      │
│   注册 Dock / Tools 菜单 / 右键菜单 / 加载 ICore::settings     │
└───────────────▲──────────────────────────────┬───────────────┘
                │                              │
┌───────────────┴───────────────┐  ┌───────────▼───────────────┐
│ UI 交互层                     │  │ Agent & 审批核心层         │
│  AiChatDock                   │  │  AgentController(状态机)   │
│  ApprovalDialog(模态审批)     │◄─┤  ApprovalInterceptor(硬门) │
│  DiffView / UiPreviewWidget   │  │  SnapshotManager(环形快照) │
│  SettingsDialog               │  │  ConversationStore         │
└───────────────────────────────┘  │  PromptBuilder             │
                                   └───────────┬───────────────┘
                                               │ ToolCall / ToolPlan
┌──────────────────────────────────────────────▼───────────────┐
│ 能力封装层                                                    │
│  ToolRegistry / ToolDispatcher(唯一写盘口)                    │
│  ReadTools / UiTools / CodeTools / BuildTools                 │
│  ── creator/ 适配：ProjectService · DesignerBridge ·          │
│                     BuildService · ContextMenuHook            │
│  ── uidom/ ：UiDocument（.ui XML 读写，纯 Qt）                 │
└──────────────────────────────────────────────┬───────────────┘
                                               │ messages + tools
┌──────────────────────────────────────────────▼───────────────┐
│ LLM 通信层  LlmClient（QNetworkAccessManager + SSE）          │
│             SseParser · ToolCallParser（降级恢复）            │
└──────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────┐
│ 持久化层  AgentSettings → ICore::settings()                  │
│           ConversationStore → <settings>/aiassistant/history │
└──────────────────────────────────────────────────────────────┘
```

**依赖方向是单向的**：UI → Agent → 工具 → 能力封装 → Creator SDK。能力封装层不认识 Agent，
LLM 层不认识任何 Creator 概念（Agent 也不认识 HTTP/SSE）。

## 2. 核心数据结构（src/core/agenttypes.h）

```cpp
struct ToolCall   { QString id, name; QJsonObject arguments; QString rawArguments; };
struct ToolResult { bool success, readOnly, pending; QString content, error, callId, toolName; };
struct ToolPlan   { QString toolName, title, summary, kind;    // kind: ui/code/project/editor/build/run
                    QStringList changedFiles; bool externalAction; QJsonObject payload;
                    ToolCall call; bool ok; QString error; };
struct FileChange { QString path; QByteArray before, after; bool existedBefore, deleteFile; };
struct ApprovalRequest  { QString title, kind, summary;
                          QVector<ToolCall> calls; QVector<ToolPlan> plans; QVector<FileChange> changes; };
struct ApprovalDecision { enum Kind { Rejected, Approved, Modified } kind; QVector<ToolCall> calls; QString comment; };
enum class AgentState   { Idle, Thinking, WaitUserApprove, ExecTool, Continue };
```

`ToolPlan` / `FileChange` 是整个安全模型的关键：**修改类工具不直接写文件**，
它们只产出「计划」；`FileChange.before/after` 同时供审批弹窗的 Diff、UI 预览和写盘使用。

## 3. 两阶段执行协议（prepare → approve → apply）

```
LLM tool_calls
   │
   ▼
AgentController::step()
   ├─ 只读工具 ──────────────► ToolDispatcher::executeReadOnly()  ──► 结果回填
   └─ 修改类工具（连续多条合并成一张审批单）
          │
          ▼
   ToolDispatcher::prepareWrites(calls)
          ├─ FileOverlay（内存叠加层）：逐个 ToolPlan 链式演算，磁盘零改动
          ├─ 聚合 FileChange：before = 磁盘现状，after = 叠加层最终内容
          └─ 产出 ApprovalRequest
          │
          ▼
   ApprovalInterceptor::review(request)   ← 硬编码规则，无开关
          │
          ├─ Rejected ──► 回填「用户拒绝执行该操作，请更换方案或询问其他思路」→ Continue
          └─ Approved / Modified
                 │
                 ▼
        ToolDispatcher::apply(request)
                 ├─ SnapshotManager::beginBatch + captureFile（写前快照）
                 ├─ ProjectService::writeFileBytes（逐文件）
                 ├─ 失败 → 自动整批回滚 + 返回错误
                 ├─ ProjectService::notifyFilesChanged（Creator 刷新）
                 └─ 构建/运行 → BuildService 异步启动，结果稍后 asyncToolResult 回填
```

`Modified`（用户改了 JSON 参数后执行）会**用新参数重新 prepare 一次**再 apply，
保证写进去的内容与用户在弹窗里看到/编辑的一致。

## 4. Agent 状态机（src/core/agentcontroller.cpp）

```
            sendUserMessage
   Idle ─────────────────────► Thinking ──(LLM 返回 tool_calls)──► Continue
    ▲                              │                                │
    │                              │(无 tool_calls)                 │ 只读工具
    │                              ▼                                ▼
    └─────────────────────────── Idle                          ExecTool ──► Continue
                                                                   ▲
                                       修改类工具                  │
                              Continue ──────────► WaitUserApprove ─┘
                                                        │ (拒绝)
                                                        └──────────► Continue
```

实现约定：

* 每一步都用 `QMetaObject::invokeMethod(this, "step", Qt::QueuedConnection)` 重新进入，
  **没有嵌套回调、没有递归过深**；
* `m_toolRounds` 有上限（默认 24，可配），防止模型死循环；
* 审批弹窗本身是模态 `exec()`：它跑在 `step()` 里，天然把状态机“暂停”在 `WaitUserApprove`；
* 构建/运行是异步的：`apply()` 返回 `pending=true`，状态机停在 `ExecTool`，
  直到 `ToolDispatcher::completeAsyncTool()` 发回 `asyncToolResult` 才继续。

## 5. 工具注册与分类

```cpp
class ToolRegistry {
    bool addReadTool(const ToolSpec &spec, ReadHandler);      // ReadHandler: (ToolCall, PrepareContext&) -> ToolResult
    bool addWriteTool(const ToolSpec &spec, PrepareHandler);  // PrepareHandler: (ToolCall, PrepareContext&) -> ToolPlan
    bool isReadOnly(const QString &name) const;               // 未注册 → false（保守）
};
```

`ToolSpec::readOnly` 在注册入口被强制赋值（`addReadTool` 恒 true、`addWriteTool` 恒 false），
而**最终是否需要审批**由 `ApprovalInterceptor::requiresApproval(name, registrySaysReadOnly)` 决定：

```cpp
if (isMutatingTool(name))       return true;   // 1) 硬编码修改类清单，优先
if (!registrySaysReadOnly)      return true;   // 2) 注册表认为可写
if (!isKnownReadOnlyTool(name)) return true;   // 3) 未知工具 → 默认审批（default-deny）
return false;                                  // 4) 明确只读 → 放行
```

## 6. 关键类接口

### 6.1 入口

```cpp
class AiAssistantPlugin : public ExtensionSystem::IPlugin {
    bool initialize(const QStringList &, QString *) override;   // 装配 + 配置 + Action
    void extensionsInitialized() override;                      // 加 Dock 到主窗口
    ShutdownFlag aboutToShutdown() override;                    // 取消请求 / 存历史 / 释放 Dock
};
```

### 6.2 Agent

```cpp
class AgentController : public QObject {
    void setServices(LlmClient*, ToolDispatcher*, ToolRegistry*, ApprovalInterceptor*,
                     PromptBuilder*, ConversationStore*, ProjectService*, DesignerBridge*, AgentSettings*);
public slots:
    void sendUserMessage(const QString &text);
    void cancel();
    void clearHistory();
    void loadHistory();
    void refreshContext();
    void step();                      // 状态机驱动（队列连接进入）
signals:
    void stateChanged(AgentState);
    void messageAdded(const ChatMessage &);
    void streamStarted(); void streamText(const QString &); void streamFinished();
    void toolLog(const QString &level, const QString &text);
    void errorOccurred(const QString &);
    void approvalRecord(const QString &);
    void historyCleared();
};
```

### 6.3 审批

```cpp
class ApprovalInterceptor : public QObject {
    using Presenter = std::function<ApprovalDecision(const ApprovalRequest &)>;
    static constexpr bool kApprovalAlwaysRequired = true;
    static bool requiresApproval(const QString &tool, bool registryReadOnly);
    void setPresenter(Presenter);
    ApprovalDecision review(const ApprovalRequest &);   // 无 Presenter → 拒绝（fail-closed）
};
```

### 6.4 快照

```cpp
struct SnapshotItem  { QString filePath; QByteArray oldContent;
                       bool existedBefore, editorDocument; qint64 time; QString toolName; };
struct SnapshotBatch { QString id, title; qint64 time; QVector<SnapshotItem> items; };

class SnapshotManager : public QObject {
    void setMaxBatches(int n);                       // 超限丢弃最早（环形队列）
    QString beginBatch(const QString &title);
    void captureFile(const QString &batchId, const QString &path, const QString &tool = {});
    void captureEditorDocument(...);                 // insert_code_at_cursor 的缓冲区快照
    void commitBatch(const QString &batchId);
    bool rollbackBatch(const QString &batchId, QStringList *restored, QString *error);
    void setHooks(const Hooks &);                    // 写盘/删文件/恢复编辑器/通知刷新（由插件注入）
};
```

### 6.5 Creator 适配

```cpp
class ProjectService : public QObject {          // SessionManager + .pro 正则解析 + 文档刷新
    ProjectSnapshot snapshot(); void invalidate(); ProjectSnapshot refresh();
    QString resolvePath(const QString &, QString *error) const;
    QString readTextFile(...) const; QByteArray readFileBytes(...) const;
    bool writeFileBytes(...) const; bool isOpenAndModified(const QString &) const;
    bool setDocumentContents(...); void notifyFilesChanged(const QStringList &);
    QString currentEditorFilePath() const; QString currentEditorSelection() const;
    bool insertTextAtCursor(const QString &, QString *error);
    static ProjectFileLists parseProFile(const QString &proPath);   // HEADERS/SOURCES/FORMS/RESOURCES
};

class DesignerBridge : public QObject {          // 只采集，不操控画布
    FormInfo currentForm() const;                // designer（画布实时）/ file（.ui 兜底）/ none
    QJsonObject selectedWidget() const;
    QString currentUiFile() const;
};

class BuildService : public QObject {            // 反射 + 命令 id + 编译输出解析
    bool startBuild(const QString &target, QString *error);
    bool startRun(QString *error); bool stopRun(QString *error);
    QJsonObject lastBuildReport() const;         // {errors[], warnings[], tail, inProgress}
signals:
    void buildFinished(bool success, const QString &summary);
    void runOutputCaptured(const QString &output);
};
```

## 7. LLM 通信层

```cpp
class LlmClient : public QObject {
    void setSettings(const AgentSettings &);
    void send(const QVector<ChatMessage> &messages, const QVector<ToolSpec> &tools);
    void cancel(const QString &reason = {});
signals:
    void requestStarted(); void streamDelta(const QString &);
    void completed(const QString &text, const QVector<ToolCall> &calls, const QString &finishReason);
    void failed(const QString &error); void canceled();
};
```

要点：

* URL 归一化：`/v1` → `/v1/chat/completions`；含 `/api/chat` 视为 Ollama 原生协议；
* 协议自适应：响应块里有 `choices` → OpenAI；有 `message` → Ollama；`response` → `/api/generate`；
* SSE 与 NDJSON 都能解析（`SseParser`），容忍缺失空行分隔的代理；
* 容错：单块 JSON 坏了不中断；超时/断连时若已有部分文本，则按 `timeout_partial` / `partial`
  降级继续；模型完全没走 tool_calls 时，`ToolCallParser` 从 ```json``` / `<tool_call>` / 裸 JSON
  里恢复调用（**恢复出来的调用同样要审批**）。

## 8. 线程与生命周期

* 全部在 GUI 线程：`QNetworkAccessManager`、所有 Widget、Creator API 都必须主线程访问；
* 插件卸载顺序：`aboutToShutdown()` → 取消 LLM/Agent → 保存历史 → 释放 Dock → 析构里 delete
  `ToolRuntime`/`ToolRegistry`/`PromptBuilder`/`AgentSettings`（工具对象随 QObject 树析构）；
* 所有跨对象指针都是裸指针 + 明确的所有者（插件持有），`QDockWidget` 用 `QPointer` 防悬挂。
