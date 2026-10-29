# 03 · 工具、审批与快照

## 1. 工具清单（与 README 表格一致，这里给参数细节）

### 1.1 只读工具（直接执行，不弹审批、不写任何文件）

| 工具 | 参数 | 返回 |
| --- | --- | --- |
| `get_designer_current_ui_info` | — | `{available, source:"designer"/"file", uiFile, formClass, formObjectName, selectedObjectName, widgets:[{objectName,class,geometry{x,y,width,height},parent,inLayout,layout,hasLayout,properties,children}]}` |
| `get_selected_widget` | — | 单控件详情 + `hint`（提示后续应调用哪些 `ui_*` 工具、`ui_path` 用什么） |
| `get_project_structure` | — | `{hasProject, projectName, projectFile, projectDirectory, proFiles, files{headers,sources,forms,resources,other}, counts, openDocuments}`（路径相对项目目录，超量有省略提示） |
| `read_file` | `path`（相对项目目录或绝对路径）、可选 `start_line`/`end_line`（1 基，含首尾）、`max_bytes` | `{path, absolutePath, lineCount, byteCount, content:"%5d \| 行内容", truncated?}` |
| `get_build_errors` | — | `{errors[], warnings[], errorCount, warningCount, tail, inProgress, outputLength}`（正则解析 MSVC `file(line): error C2065:` 与 GCC/Clang `file:line:col: error:`） |

### 1.2 修改类工具（全部必须人工审批）

| 工具 | 参数（`*` 必填） | 语义与实现 |
| --- | --- | --- |
| `ui_add_widget` | `ui_path*`, `widget_type*`, `objectName*`, `x,y,w,h`, `props{}`, `parent` | 父容器有布局 → 插入 `<item>`（`QGridLayout`/`QFormLayout` 自动带 `row`/`column`）；无布局 → 写 `<property name="geometry"><rect>`。`props` 支持普通值或显式类型 `{"type":"rect/size/point/color/set/stringlist/number/bool/string","value":...}` |
| `ui_move_widget` | `ui_path*`, `objectName*`, `new_x,new_y,new_w,new_h`（缺省沿用原值） | **= 拖拽**：只改该控件 `geometry` 的 `<rect>`。控件在布局中时会在摘要里提示「geometry 不生效」 |
| `ui_modify_widget_property` | `ui_path*`, `objectName*`, `prop_key*`, `prop_value` | 改/建/删属性节点；`prop_value=null` 删除该属性；禁止改 `objectName`（会破坏信号槽绑定，提示删旧建新） |
| `ui_bind_signal_slot` | `ui_path*`, `sender_obj*`, `signal*`, `receiver_obj*`, `slot*` | 在 `<connections>` 追加 `<connection>`，去重；sender/receiver 必须存在 |
| `ui_delete_widget` | `ui_path*`, `objectName*` | 删 `<widget>` 及其 `<item>` 包装，并清理指向它的 `<connection>`；拒绝删顶层控件 |
| `create_ui_form` | `ui_name*`, `save_path*`, `base_class`(默认 QWidget), `widgets[]` | 生成 `ui version="4.0"` 新窗体（每个控件唯一 objectName、geometry、windowTitle、`<resources/>`、`<connections/>`），并**追加** `.pro` 的 `FORMS +=` 一行 |
| `write_file` | `path*`, `content*` | 全量写入（兜底方案），自动建父目录；文件在编辑器中且有未保存修改时**拒绝执行**，避免丢改动 |
| `patch_file` | `path*`, `diff` 或 `edits[]` | 增量修改（优先）：`diff` 为 unified diff（`@@ -a,b +c,d @@`，上下文不匹配时报错不写入）；`edits` 为 `[{"oldText","newText","all":false}]` 精确替换 |
| `add_qt_class` | `class_name*`, `path*`, `base_class`, `namespace` | 生成 `<Class>.h/.cpp`（含 `Q_OBJECT`、构造/析构、include guard），并**追加** `.pro` 的 `HEADERS +=` / `SOURCES +=` |
| `insert_code_at_cursor` | `code*` | 在当前 `TextEditor` 光标处 `insertPlainText`；快照记录插入前的**编辑器缓冲区** |
| `build_project` | `target` | 触发 Creator 构建（见下），异步；构建结束把「错误/警告/输出末尾」回填给模型 |
| `run_project` | — | 触发 Creator 运行，约 2.5 秒后抓取「应用程序输出」面板内容回填 |

## 2. 审批协议

### 2.1 判定（`ApprovalInterceptor::requiresApproval`）

```
硬编码修改类清单（12 个）  → 必须审批
注册表标记为可写            → 必须审批
不在只读白名单（未知工具）  → 必须审批（default-deny）
明确只读白名单（5 个）      → 放行
```

* `static constexpr bool kApprovalAlwaysRequired = true;` + `static_assert` —— 编译期锁死；
* 没有 `setEnabled()/disable()/setAutoApprove()` 之类接口；
* 没有 `Presenter`（审批 UI）时 `review()` 返回 `Rejected`（fail-closed）。

### 2.2 弹窗（`ApprovalDialog`，模态 `exec()`）

* **头部**：操作标题 + 类型标签（UI 修改 / 代码修改 / 构建 / 运行 …）+ 涉及文件名；
* **每调用一个页签**：
  * 代码类：左侧行级 Diff（删除行红底、新增行绿底、`+N/-M` 统计）；
  * UI 类：左侧 Diff + 右侧 `QUiLoader` 渲染的**修改后窗体**（绿色实线=新增、蓝色虚线=移动、
    橙色点线=属性变更；被删除的控件列在图例里）；
  * 构建/运行：说明「会在 Creator 中立即执行、不写文件」；
  * 底部：**可编辑 JSON 参数**（点按钮时按这里的内容执行）+ 可选备注；
* **三个按钮**：`同意执行` / `拒绝本次操作` / `修改指令后执行`；
* 关闭窗口（Esc / ×）= 拒绝；
* 所有按钮 `setAutoDefault(false)`、不设 default button —— 防止误按回车直接同意。

### 2.3 三种结果如何回填给模型

| 结果 | 行为 | 回填给模型的内容 |
| --- | --- | --- |
| 同意 | 按原参数 `prepare → apply` | 每个工具的 `ToolResult`（成功摘要 / 写入的文件） |
| 修改后执行 | **用编辑后的参数重新 prepare**，再 apply | 同上（模型从结果里能看到实际执行的参数与文件） |
| 拒绝 | 不写盘、不执行 | 每个工具 `<tool>` 消息：「用户拒绝执行该操作，请更换方案或询问其他思路」（可带用户备注） |

### 2.4 批量合并

同一次 LLM 响应里**连续的**修改类工具调用会被合并成**一张审批单**（多个页签，一次决策、一次快照批次）。
只读调用夹在中间时会先执行，再收集后续连续的修改类调用。

## 3. 快照与回滚

* **写入前自动打快照**：`ToolDispatcher::apply()` 第一步就是
  `SnapshotManager::beginBatch(title)` + 对每个受影响文件 `captureFile()`；
* 快照内容 = 写盘前的**磁盘内容**（`existedBefore` 记录文件是否原本存在，回滚时不存在则删除）；
* `insert_code_at_cursor` 的记录方式不同：快照的是**编辑器缓冲区**（`editorDocument=true`），
  回滚时优先 `IDocument::setContents()`（不落盘），文档未打开才退回写文件；
* **环形队列**：`maxSnapshots`（默认 50，设置页可改，1~5000），超出丢弃最早的**批次**；
* 空批次自动丢弃（没有回滚价值）；
* 写盘中途失败 → **自动整批回滚** 后返回错误（不会留下半成品）；
* 回滚入口在 Dock 的「回滚」按钮：列出所有批次（时间 / 标题 / 文件数）→ 二次确认 →
  `rollbackBatch()`（逆序恢复）→ 通知 Creator 刷新。**该入口独立于 AI 流程**，也不需要审批
  （是用户自己的显式操作，且有二次确认）。

## 4. Creator 刷新策略

`.ui` 与源码都是**直接改磁盘文件**（这是本插件的核心实现思想），随后：

1. `ProjectService::notifyFilesChanged(paths)`：对**已打开且未修改**的文档调用
   `IDocument::setContents(磁盘内容)` + `save()` —— 编辑器视图立即同步；
2. Creator 自身的文件监视器会处理其余情况；
3. **Designer 注意事项**：
   * 写 `.ui` 前会检查该文档是否「已打开且有未保存修改」，是则**直接拒绝执行**并提示先保存，
     避免覆盖用户在画布上的未保存改动；
   * Designer 画布对磁盘变更不会自动重绘：切换窗体页签或重新打开 `.ui` 即可看到新布局
     （这正是“禁止操控画布私有 API”的代价，也是 4.15 下最稳的做法）。

## 5. 降级与容错

| 场景 | 行为 |
| --- | --- |
| 模型返回纯文本而非 tool_calls | `ToolCallParser` 从 ```json``` / `<tool_call>` / 裸 JSON 恢复调用；恢复的调用**照常审批**，并打日志 `text_fallback` |
| 参数 JSON 不合法 | 该工具作为失败结果回填给模型（不弹审批、不写盘） |
| SSE 单块 JSON 损坏 | 跳过该块继续解析 |
| 服务端一次性返回整包 JSON | 按 `Content-Type` 或「流式一无所获」自动退化整包解析 |
| 超时 | 有部分文本则按 `timeout_partial` 降级继续；否则报错并回到 Idle |
| 断连 | 有部分结果按 `partial` 继续，否则报错 |
| 构建失败 | `get_build_errors` / 异步回填里给出 `errors[]` 与输出末尾 |
| `.pro` 解析异常 | 正则解析失败只会得到空列表，不会抛异常；文件扫描作为兜底 |
| 工具轮次过多 | 超过 `maxToolIterations`（默认 24）主动停止并提示 |
| 审批 UI 缺失 | fail-closed：一律拒绝 |

## 6. 系统提示词

`PromptBuilder::systemPrompt()` 逐字下发规格五的提示词，再追加：

* 只读/修改类工具清单（由 `ApprovalInterceptor` 的白名单生成，保证与代码一致）；
* 坐标与布局注意事项（布局内改 geometry 不生效）；
* 代码修改规范（优先 `patch_file`、`write_file` 兜底）；
* 当前工程摘要（项目名/目录/pro/文件计数/UI 文件列表）；
* Designer 当前选中控件摘要（如有）；
* 上下文压缩规则：单条 > 6000 字符截断；总量超 `contextCharBudget` 时按“轮次”丢弃最早对话，
  始终保留最后一轮。
