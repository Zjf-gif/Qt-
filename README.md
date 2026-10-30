# AI Assistant —— Qt Creator 4.15.0 插件（AI 辅助开发）

嵌入 Creator 侧边 Dock 的 AI Agent。插件本身**不内置大模型**，只作为 LLM 客户端
（OpenAI 兼容协议 / Ollama 原生协议，SSE 流式 + ToolCall），把「读项目 / 读 Designer 画布 /
改 .ui XML / 改源码 / 构建 / 运行」封装成工具提供给模型。

> **最高优先级铁律**
> 1. 只读工具直接执行；
> 2. **所有**修改类操作（UI / 代码 / 项目配置 / 构建 / 运行）必须经过模态人工审批；
> 3. 审批开关**硬编码**、不可关闭，配置里也**不存在**相关开关；
> 4. 审批通过、写盘前**自动打快照**，可回滚；
> 5. 审批被拒绝时，把「用户拒绝执行该操作，请更换方案或询问其他思路」回填给模型，让它重新规划。

---

## 1. 目录结构

```
QT插件/
├─ src/                            # ★ 插件源码，整个目录拷进 Creator 源码树
│  ├─ aiassistant.pro              #   in-tree 工程文件（include 官方 qtcreatorplugin.pri）
│  ├─ aiassistant_dependencies.pri #   QTC_PLUGIN_NAME=AiAssistant + 依赖的 Creator 插件/库
│  ├─ AiAssistant.json.in          #   插件元数据模板（qmake 替换版本号与依赖列表）
│  ├─ aiassistant_sources.pri      #   源文件清单
│  ├─ aiassistantplugin.h/.cpp     #   入口层：Dock / Action / 配置 / 装配 / 卸载
│  ├─ constants.h
│  ├─ settings/agentsettings.*     #   配置（ICore::settings）——无任何审批开关
│  ├─ core/
│  │  ├─ agenttypes.h/.cpp         #   ToolCall / ToolResult / ChatMessage / ToolPlan / 审批结构
│  │  ├─ agentcontroller.*         #   Agent 状态机（Idle→Thinking→WaitUserApprove→ExecTool→Continue）
│  │  ├─ approvalinterceptor.*     #   审批拦截器（硬编码规则、fail-closed）
│  │  ├─ snapshotmanager.*         #   快照环形队列 + 回滚
│  │  ├─ conversationstore.*       #   按项目隔离的对话历史
│  │  └─ promptbuilder.*           #   系统提示词 + 上下文压缩
│  ├─ creator/                     #   Creator SDK 适配层（唯一的不稳定 API 集中处）
│  │  ├─ creatorcompat.*           #   路径类型兼容、命令 id 候选、按类名反射查找
│  │  ├─ projectservice.*          #   SessionManager / .pro 正则解析 / 文档读写与刷新 / 光标插入
│  │  ├─ designerbridge.*          #   Designer 画布“只读”采集（QtDesigner 公开接口）
│  │  ├─ buildservice.*            #   构建/运行（反射 + 命令 id + 编译输出解析）
│  │  └─ contextmenuhook.*         #   编辑器 / Designer 右键菜单注入
│  ├─ llm/
│  │  ├─ llmclient.*               #   QNetworkAccessManager + SSE，OpenAI/Ollama 双协议
│  │  ├─ sseparser.*               #   SSE / NDJSON 分片解析
│  │  └─ toolcallparser.*          #   纯文本工具调用恢复（容错降级）
│  ├─ tools/
│  │  ├─ toolruntime.h             #   ToolRuntime + FileOverlay + PrepareContext（prepare 沙箱）
│  │  ├─ toolregistry.*            #   工具注册（spec + 只读/修改 handler）
│  │  ├─ tooldispatcher.*          #   prepareWrites → apply（唯一写盘入口）
│  │  ├─ readtools.*               #   5 个只读工具
│  │  ├─ uitools.*                 #   6 个 UI 工具（QDom 改 .ui XML）
│  │  ├─ codetools.*               #   write_file / patch_file / add_qt_class / 光标插入
│  │  ├─ buildtools.*              #   build_project / run_project
│  │  └─ proeditor.*               #   .pro 行级追加（HEADERS/SOURCES/FORMS）
│  ├─ uidom/uidocument.*           #   Qt5 ui xml(4.0) 读写引擎（纯 Qt，可单测）
│  └─ ui/
│     ├─ aichatdock.*              #   对话面板
│     ├─ approvaldialog.*          #   审批弹窗（Diff / UI 预览 / 可编辑 JSON / 三按钮）
│     ├─ diffview.*                #   行级 diff 高亮
│     ├─ uipreviewwidget.*         #   QUiLoader 渲染修改后的窗体 + 新增/移动/删除标记
│     └─ settingsdialog.*          #   设置页
├─ docs/
│  ├─ 01-architecture.md           # 分层架构、关键类与接口、状态机、时序
│  ├─ 02-build-and-deploy.md       # Creator 源码获取 → 编译 SDK → 编译插件 → 部署 → 调试
│  ├─ 03-tools-and-approval.md     # 工具清单/参数/审批类型、审批协议、快照与回滚语义
│  └─ 04-acceptance-tests.md       # 验收标准逐项自测步骤
└─ _syntaxcheck/                   # 可选自检工程（Qt + SDK 桩头文件全量编译）
```

## 2. 5 分钟上手（编译部署）

```powershell
# 1) 取 Creator 4.15.0 源码（无 git 也可用官方 tar.xz：
#    https://download.qt.io/archive/qtcreator/4.15/4.15.0/qt-creator-opensource-src-4.15.0.tar.xz）
git clone --recursive -b v4.15.0 https://code.qt.io/qt-creator/qt-creator.git

# 2) 把本仓库的 src 拷进 Creator 源码树
Copy-Item .\src  <qt-creator>\src\plugins\aiassistant -Recurse

# 3) 在 <qt-creator>\src\plugins\plugins.pro 的 SUBDIRS 里登记插件
#    （在该文件末尾的 isEmpty(IDE_PACKAGE_MODE) 块之后加一行：）
#        SUBDIRS += aiassistant

# 4) 用 Qt 5.15.2 MSVC2019 (64-bit) 构建 Creator：
#    Creator IDE 里打开 <qt-creator>\qtcreator.pro → Run qmake → Build，
#    或在构建目录执行 qmake + jom（见 docs/02-build-and-deploy.md）
#    产物：<build>/lib/qtcreator/plugins/AiAssistant4.dll
#    （Creator 4.x 插件 DLL 名 = QTC_PLUGIN_NAME + IDE 主版本，即 AiAssistant4.dll）

# 5) 部署
Copy-Item <build>\lib\qtcreator\plugins\AiAssistant4.dll `
          "<QtCreator>\lib\qtcreator\plugins\"

# 5) 启动 Creator，打开 Tools → AI 助手 → AI 助手面板（Ctrl+Shift+A）
```

详细步骤（含 Debug 联调、ABI 注意事项、可选 `-pluginpath` 加载）见
[docs/02-build-and-deploy.md](docs/02-build-and-deploy.md)。

## 3. 工具与审批一览

| 工具 | 参数 | 动作 | 审批 |
| --- | --- | --- | --- |
| `get_designer_current_ui_info` | 无 | 读 Designer 当前窗体（画布实时坐标优先） | 只读 |
| `get_selected_widget` | 无 | 读当前选中控件详情 | 只读 |
| `get_project_structure` | 无 | 读项目 .pro 结构与文件列表 | 只读 |
| `read_file` | `path`(+`start_line`/`end_line`) | 读文本文件 | 只读 |
| `get_build_errors` | 无 | 读最近编译错误/警告 | 只读 |
| `ui_add_widget` | `ui_path, widget_type, objectName, x,y,w,h, props, parent` | 新增控件节点 | **必须审批** |
| `ui_move_widget` | `ui_path, objectName, new_x,new_y,new_w,new_h` | 改 `<rect>`（= 拖拽） | **必须审批** |
| `ui_modify_widget_property` | `ui_path, objectName, prop_key, prop_value` | 改属性节点 | **必须审批** |
| `ui_bind_signal_slot` | `ui_path, sender_obj, signal, receiver_obj, slot` | 新增 `<connection>` | **必须审批** |
| `ui_delete_widget` | `ui_path, objectName` | 删除控件节点 | **必须审批** |
| `create_ui_form` | `ui_name, base_class, save_path, widgets[]` | 新建 ui(version=4.0) + 登记 FORMS | **必须审批** |
| `write_file` | `path, content` | 全量写入 | **必须审批** |
| `patch_file` | `path, diff / edits[]` | 增量补丁（优先） | **必须审批** |
| `add_qt_class` | `class_name, path, base_class, namespace` | 新建 h/cpp + 更新 pro | **必须审批** |
| `insert_code_at_cursor` | `code` | 编辑器光标插入 | **必须审批** |
| `build_project` | `target` | 触发 Creator 构建（异步回填） | **必须审批** |
| `run_project` | 无 | 运行并采集应用输出 | **必须审批** |

审批规则、prepare/apply 协议与快照语义见
[docs/03-tools-and-approval.md](docs/03-tools-and-approval.md)。

## 4. 代码级安全保证（不是靠“约定”，靠结构）

1. **唯一写盘入口**：只有 `ToolDispatcher::apply()` 写文件，而它只有 `AgentController::step()`
   在「审批通过」分支里调用；`prepareWrites()` 只在内存叠加层（`FileOverlay`）里演算，
   审批前磁盘零改动。
2. **默认拒绝未知工具**：`ApprovalInterceptor::requiresApproval()` 对「不在只读白名单里」的工具
   一律要求审批；`static_assert(kApprovalAlwaysRequired)` 锁死「审批永远开启」。
3. **fail-closed**：没有安装审批 UI（Presenter）时，`review()` 直接返回「拒绝」。
4. **写前快照**：`apply()` 先 `beginBatch/captureFile`，再写盘；写失败自动回滚整批。
5. **配置里没有开关**：`AgentSettings` 不含任何 approval/auto-exec 字段，设置页也不提供。

## 5. 已验证到什么程度

### 5.1 已在本机完成真实构建并安装（2026-10-29）

| 步骤 | 结果 |
| --- | --- |
| Qt 5.15.2 msvc2019_64 | `D:\Qt\5.15.2\msvc2019_64`（aqt 从 aliyun 镜像安装，另补装 `qtscript` 模块） |
| Creator 4.15.0 源码 | `C:\qt-creator-opensource-src-4.15.0`（官方 tar.xz） |
| Creator Release 构建 | `C:\build`（VS2022 MSVC 14.42 + `jom -j10`，73 个插件 + `qtcreator.exe`） |
| 插件构建 | `C:\build\lib\qtcreator\plugins\AiAssistant4.dll`（1005 KB，0 error） |
| 安装 | 已复制到 `D:\Qt\Tools\QtCreator\lib\qtcreator\plugins\` |
| 加载验证 | ① `QPluginLoader` 用**已装 Creator 的 DLL** 成功 `instance()`；② 启动已装 Creator 后用 UI Automation 在窗口里找到了 Dock「AI 助手」及其全部按钮（发送/停止Agent/手动快照/回滚/刷新项目上下文/打开设置/清空对话）与状态行「模型 … ｜ 快照 0 ｜ 审批：强制」 |

> 也就是说：**你现在直接启动 `D:\Qt\Tools\QtCreator\bin\qtcreator.exe` 就能用**，
> 菜单位置：`Tools → AI 助手 → AI 助手面板`（或 `Ctrl+Shift+A`）。

### 5.2 集成期修正的真实 SDK 差异（已全部合入源码）

1. `Core::Context` 在 `coreplugin/icontext.h`（不是 `context.h`）；
2. `Core::DocumentModel` 是 **class**，在 `coreplugin/editormanager/documentmodel.h`；
3. `Core::IEditor` **没有 `widget()`** → 改用 `QApplication::focusChanged` 记住编辑器控件；
4. `IDocument::save(QString *)` 无默认参数 → 传 `nullptr`；
5. `Utils::Id` 只有 `const char *` 构造 + `Id::fromString(QString)`，`QLatin1String` 构造被 `= delete`；
6. `QT_NO_CAST_TO_ASCII` / `QT_RESTRICTED_CAST_FROM_ASCII` 等严格选项下全量编译通过（源码全用 `QStringLiteral`）；
7. 插件必须登记进 `src/plugins/plugins.pro`，产物名由 `QTC_PLUGIN_NAME` 推导为 `AiAssistant4.dll`；
8. 源码含中文 → MSVC 必须按 UTF-8 编译（`/utf-8`），并因此关闭了 Creator 的共享 PCH；
9. `AiAssistant.json.in` 里的引号必须写成 `\"`（QMAKE_SUBSTITUTES 会吃掉裸引号，生成非法 JSON）；
10. Windows 上 QtUiTools 是**静态库**，所以 Creator 不带 `Qt5UiTools.dll`，本插件也无需额外部署它。

### 5.3 开源工程自检（无需 Creator SDK）

* 29 个 `.cpp` + 32 个头文件在 **MinGW 7.3 + Qt 5.12** 下、并开启 Creator 的严格宏
  （`QT_NO_CAST_TO_ASCII`/`QT_RESTRICTED_CAST_FROM_ASCII`/`QT_USE_QSTRINGBUILDER`）时
  `-Wall -Wextra` **零错误零警告**（见 `_syntaxcheck/`，SDK 用等价桩头文件）。

### 5.4 重新构建插件（后续改代码时）

```powershell
# 1) 把最新源码同步进 Creator 源码树
robocopy C:\Users\Administrator\Desktop\QT插件\src `
         C:\qt-creator-opensource-src-4.15.0\src\plugins\aiassistant /MIR
# 2) 只重建插件（用与 Creator 相同的 Qt/MSVC）
cmd /c "call C:\dl\mk.bat && cd /d C:\build\src\plugins && jom -j6 sub-aiassistant"
# 3) 重新部署 + 重启 Creator
Copy-Item C:\build\lib\qtcreator\plugins\AiAssistant4.dll `
          D:\Qt\Tools\QtCreator\lib\qtcreator\plugins\ -Force
```

`C:\dl\mk.bat` 是本次搭建时生成的环境脚本（vcvars64 + Qt 5.15.2 PATH），内容见
[docs/02-build-and-deploy.md](docs/02-build-and-deploy.md)。
