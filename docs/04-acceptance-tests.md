# 04 · 验收自测清单

> 前置：已按 [02-build-and-deploy.md](02-build-and-deploy.md) 部署 `AiAssistant4.dll`，
> Creator 里能看到 Tools → AI 助手 菜单，Dock 能打开，设置里已配置可用的 LLM。
>
> 建议准备一个 qmake 测试工程（`QWidget` + `mainwindow.ui`，放 2~3 个按钮），
> 并在 Creator 里打开 `mainwindow.ui`（Designer 画布）与 `mainwindow.cpp`。

## 验收 1 · 只读工具：不弹窗、不改文件

1. Dock 输入：`读取当前项目结构`。
2. 期望：
   * 消息流里出现 `[工具] 调用只读工具 get_project_structure`；
   * **不弹审批窗口**；
   * 项目目录下所有文件 `LastWriteTime` 不变（可在 PowerShell 里 `Get-ChildItem -Recurse | Select Name,LastWriteTime` 前后对比）。
3. 再测 `get_designer_current_ui_info`、`get_selected_widget`、`read_file`、`get_build_errors`：
   同样不弹窗、不改文件。

**判定**：全部无弹窗 + 文件时间戳无变化 → 通过。

## 验收 2 · 修改类工具：必弹审批、拒绝后重新规划

1. Dock 输入：`在 mainwindow.ui 里加一个 QPushButton，objectName=btnAiTest，文字“AI 测试”，位置 20,20 大小 120x32`。
2. 期望：
   * 弹出审批窗口，标题“新增控件 btnAiTest（QPushButton）”，类型标签“UI 修改”；
   * 右侧是修改后窗体预览，绿色框标出新增按钮；左侧是 `.ui` 的 XML diff；
   * 参数 JSON 可编辑。
3. 点 **拒绝本次操作**：
   * 不写盘（`.ui` 时间戳不变）；
   * Dock 出现 `[审批] 已拒绝：…`；
   * 模型收到「用户拒绝执行该操作，请更换方案或询问其他思路」，**下一轮不会再提交完全相同的指令**
     （观察它的下一条消息：要么换方案、要么发问）。
4. 重复一次并点 **同意执行**：
   * 写盘成功，Dock 出现 `已写入 1 个文件，已通知 Creator 刷新`；
   * `.ui` 里出现 `objectName=btnAiTest` 与 geometry。

**判定**：拒绝零写入 + 模型收到拒绝理由并改变方案 → 通过。

## 验收 3 · 拖拽语义：`ui_move_widget` → xml rect 变更 → Designer 位置变化

1. 在 Designer 画布选中 `btnAiTest`，右键 → **让AI操作当前选中控件**（或 `Ctrl+Alt+D`）。
2. 输入：`把它移动到 (300, 200)`。
3. 期望：
   * 审批弹窗的 diff 显示 `x/y` 变化（`20,20` → `300,200`）；
   * 同意后 `<property name="geometry"><rect><x>300</x><y>200</y>…`；
   * 在 Creator 里**重新打开/切换** `mainwindow.ui` 后，按钮出现在 (300,200)。
4. 若控件在布局中：工具摘要会提示「该控件在布局中，geometry 不生效」——这属于预期行为。

**判定**：xml 的 `<rect>` 改变 + 重新打开 Designer 后位置变化 → 通过。

## 验收 4 · 快照回滚：恢复到修改前版本

1. 先 `git status` 或记录 `mainwindow.ui` 内容/时间戳。
2. 执行一次审批通过的 UI 修改（如验收 2 的同意分支）。
3. 点 Dock 的 **回滚** → 选择刚才的批次（形如 `07-01 12:33:10  新增控件 btnAiTest（QPushButton）（1 个文件）`）→ 确认。
4. 期望：
   * `.ui` 恢复到修改前字节（`git diff` 为空）；
   * Dock 出现 `[审批] 已回滚 1 个文件`；
   * 再点「回滚」时该批次已从列表消失。
5. 边界：`insert_code_at_cursor` 的回滚只恢复**编辑器缓冲区**（文档未关闭时不会写盘）。

**判定**：内容逐字节恢复 → 通过。

## 验收 5 · 边界：格式错误 / 断连 / 编译失败 / pro 异常都不崩溃

| 用例 | 操作 | 期望 |
| --- | --- | --- |
| LLM 输出非 JSON | 用 `temperature=1.5` 或让模型“用自然语言描述修改” | 要么走 `text_fallback`（恢复出的调用照样审批），要么作为纯文本显示；**不崩溃** |
| 参数非法 | 模型给出 `{"path": 123}` 之类 | 工具失败结果回填，不弹审批、不写盘 |
| 断连 | 请求中途把 Ollama 进程杀掉 / 拔网线 | Dock 显示错误或 `partial` 降级提示，状态回到「空闲」，可继续对话 |
| 超时 | 设置里把超时改成 5 秒，用慢模型 | 显示超时错误（或 `timeout_partial` 降级），不卡死 UI |
| 编译失败 | 故意写坏一行代码 → `build_project` | 审批 → 构建 → 结束后回填 `errors[]`；`get_build_errors` 也能读到 |
| pro 异常 | 手工把 `.pro` 写成乱码 → `get_project_structure` | 返回空列表 + 文件扫描兜底，不崩溃 |
| 大文件 | `read_file` 一个 > 500KB 的文件 | 拒绝并提示用 `start_line/end_line` |
| 二进制文件 | `read_file` 一个 png | 拒绝（“看起来是二进制文件”） |
| Designer 未保存 | 在画布上改一下不保存，然后让 AI 改同一个 `.ui` | 直接拒绝执行并提示“请先保存”，不覆盖用户改动 |
| 未知工具 | 手工构造调用名（或模型幻觉） | 默认走审批（default-deny）+ 执行时报“未知工具” |

**判定**：全部用例都没有崩溃/卡死、状态机都能回到 Idle → 通过。

## 验收 6 · 权限：不存在任何可关闭审批的配置开关

代码级检查（评审用）：

```powershell
# 1) 配置结构里没有任何审批相关字段
Select-String -Path src\settings\agentsettings.h -Pattern "approval|auto|execute|toggle"

# 2) 审批拦截器没有 setEnabled/disable 之类接口
Select-String -Path src\core\approvalinterceptor.h -Pattern "setEnabled|disable|setAuto"

# 3) 全插件只有一处调用 apply()（在审批通过分支里）
Select-String -Path src\**\*.cpp -Pattern "->apply\(" | Select-Object Path,LineNumber,Line

# 4) 设置对话框里没有审批开关
Select-String -Path src\ui\settingsdialog.cpp -Pattern "审批"
```

运行期检查：

* 打开「打开设置」，逐项检查：LLM 地址 / Key / 模型 / 代理 / 超时 / temperature /
  快照上限 / 工具轮次上限 / 上下文预算 / 历史开关 —— **没有**审批相关项；
* `QSettings` 里 `AiAssistant/*` 键全表核对，无 approval 类键；
* 修改类工具（12 个）逐个触发一次，均必须弹窗。

**判定**：`apply()` 只在 `AgentController::step()` 的审批通过分支出现；配置/设置页无开关 → 通过。

---

## 附加自测（建议在交付前跑一遍）

1. **右键菜单**：编辑器选中一段代码 → 右键应出现「让AI重构选中代码（N 个字符）」；
   Designer 画布右键应出现「让AI操作控件 xxx」。若某平台拿不到弹出菜单，
   Tools 菜单里的等价命令（`Ctrl+Alt+R` / `Ctrl+Alt+D`）必须可用。
2. **批量合并**：让模型一次做两件事（“把按钮移到右下角，并把文字改成登录”），
   应当只弹**一次**审批，两个页签。
3. **修改后执行**：审批时把 `text` 改成别的字符串 → 点「修改指令后执行」→ 写入的是你改后的值，
   Dock 显示 `已按修改后的参数执行：…`。
4. **历史隔离**：两个不同项目各聊几句 → `<settings>/aiassistant/history/` 下应有两个不同 json。
5. **关闭历史**：设置里取消“按项目保存对话历史” → 之后不再写入历史文件。
6. **停止 Agent**：长回答过程中点「停止Agent」→ 立即停止流式输出，状态回到空闲，不弹错误。

## 自检工程的编译验证（可选，不需要 Creator SDK）

`_syntaxcheck/` 里有一套 Qt + SDK 桩头文件的编译工程，可在没有 Creator 源码的机器上
验证插件自身代码（类型/签名/moc/DOM/SSE/Diff 算法）能否编译：

```powershell
cd _syntaxcheck\build          # 需要 Qt + MinGW/MSVC 环境变量指向 qmake / make
qmake ..\check_all.pro && mingw32-make -j4
```

> 桩头文件只覆盖本插件实际用到的 `ICore` / `ActionManager` / `DocumentModel` / `IPlugin` /
> `Project` / `SessionManager` 等符号，用于**编译级自检**，不能替代真实 SDK 构建。
