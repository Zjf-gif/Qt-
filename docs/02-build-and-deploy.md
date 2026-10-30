# 02 · 编译与部署（Qt Creator 4.15.0 + Qt 5.15.2 MSVC2019）

Qt Creator 4.x **没有独立的 Plugin SDK 发行包**，插件必须链接 Creator 自身的构建产物，
因此标准做法是：**在 Creator 源码树内构建插件**（本仓库的 `src/` 就是为这种方式准备的）。

---

## 1. 准备环境

| 项 | 要求 |
| --- | --- |
| Qt Creator 源码 | `v4.15.0` 标签：`git clone --recursive -b v4.15.0 https://code.qt.io/qt-creator/qt-creator.git`；无 git 时用官方包 `https://download.qt.io/archive/qtcreator/4.15/4.15.0/qt-creator-opensource-src-4.15.0.tar.xz`（已含 3rdparty 子模块，`tar -xf` 即可） |
| Qt | **Qt 5.15.2 MSVC2019 64-bit**（必须与目标 Creator 完全一致），需包含 **Qt Designer / QtUiTools / QtScript**（`QT += designer uitools`，以及 Creator 自带 qbs 需要 QtScript） |
| 编译器 | MSVC 2019（v142）或 VS2022（v143，与 v142 二进制兼容），x64 |
| 目标 Creator | 官方 Qt Creator 4.15.0（Community/Enterprise 均可），同为 MSVC2019 + Qt 5.15.2 构建 |
| 磁盘 | Creator Release 构建约 5~8 GB |

> ⚠️ **ABI 铁律**：Creator 只会加载「同一 Qt 版本 + 同一编译器 + 同一运行库（MD/MDd）+ 同一
> 架构」的插件。Debug 版 Creator 只能加载 Debug 插件，Release 版 Creator 只能加载 Release 插件。
> 官方安装包是 Release + MD，所以部署用 **Release** 构建。
>
> ⚠️ **QtScript 坑**：Qt 5.15.2 的默认在线安装集合里没有 `qtscript`。缺它时 Creator 会尝试编译
> 自带的 QtScript 副本，而该步骤需要 `perl`（`syncqt`），普通 Windows 环境没有 perl 就会在
> qmake 阶段报 `Failed to execute syncqt for the bundled QtScript module`。
> 解决：补装该模块（无需 Qt 账号）：
>
> ```powershell
> py -3 -m pip install aqtinstall
> py -3 -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 -O D:\Qt --noarchives -m qtscript
> ```

## 2. 构建 Creator（获得 Plugin SDK）

```powershell
# 有 git 时：
git clone --recursive -b v4.15.0 https://code.qt.io/qt-creator/qt-creator.git C:\qt-creator-4.15.0
# 没 git 时（本机实测）：
curl.exe -L -o C:\dl\qtcreator-src.tar.xz https://download.qt.io/archive/qtcreator/4.15/4.15.0/qt-creator-opensource-src-4.15.0.tar.xz
tar -xf C:\dl\qtcreator-src.tar.xz -C C:\          # 解出 C:\qt-creator-opensource-src-4.15.0
```

方式 A（推荐，可控）：用 Qt Creator 打开 `qtcreator.pro`，选择 “Qt 5.15.2 MSVC2019 64bit”
套件，Release 配置，构建（16 核约 40~80 分钟）。产物：

```
<build>\bin\qtcreator.exe
<build>\lib\qtcreator\plugins\Core4.dll     ← 插件 DLL / 导入库都输出到这里
```

方式 B（命令行，本机实测脚本）：

```bat
rem 环境：MSVC x64 + Qt 5.15.2
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set PATH=D:\Qt\5.15.2\msvc2019_64\bin;D:\Qt\Tools\QtCreator\bin;%PATH%

mkdir C:\build && cd /d C:\build
qmake C:\qt-creator-4.15.0\qtcreator.pro -spec win32-msvc -r CONFIG+=release CONFIG-=debug_and_release
jom -j10
```

> 只是想“拿到 SDK 编译插件”的话，**也必须真正构建一次 Creator**：`qtcreatorplugin.pri` 依赖
> 构建树里的 `lib/qtcreator`、生成的 `Makefile`、以及各插件导出的导入库。

## 3. 构建插件

插件的工程结构遵循官方约定，`TARGET`/依赖/描述文件名全部由 `qtcreatorplugin.pri` 推导：

* `src/aiassistant_dependencies.pri` → `QTC_PLUGIN_NAME = AiAssistant`（决定 DLL 名
  `AiAssistant4.dll`）与 `QTC_PLUGIN_DEPENDS = coreplugin projectexplorer`；
* `src/AiAssistant.json.in` → qmake 替换 `$$QTCREATOR_VERSION` / `$$dependencyList`，
  在构建目录生成 `AiAssistant.json` 供 moc 内嵌（**文件名必须与 `QTC_PLUGIN_NAME` 一致**）；
* `src/aiassistant.pro` → `include(../../qtcreatorplugin.pri)` + 源文件清单。

```powershell
# 1) 拷入源码树
Copy-Item C:\Users\Administrator\Desktop\QT插件\src `
          C:\qt-creator-4.15.0\src\plugins\aiassistant -Recurse

# 2) 在 src\plugins\plugins.pro 里登记（否则 qmake 不会生成该子工程的 Makefile）
#    在文件靠后位置加一行：
#        SUBDIRS += aiassistant

# 3) 重新 qmake 并只构建该插件（依赖会自动先构建）
cd /d C:\build
qmake C:\qt-creator-4.15.0\qtcreator.pro -spec win32-msvc -r CONFIG+=release CONFIG-=debug_and_release
jom -j10 sub-aiassistant
```

产物：

```
C:\build\lib\qtcreator\plugins\AiAssistant4.dll
```

> 关于文件名：Creator 4.x 的插件 DLL 名由 `qtcreator.pri` 的 `qtLibraryName()` 计算为
> `<QTC_PLUGIN_NAME><IDE 主版本>`（Debug 构建再加 `d`），所以是 `AiAssistant4.dll`
> （Debug 为 `AiAssistantd4.dll`）。这与本机已装 Creator 的 75 个插件（`Core4.dll` 等）一致。

## 4. 部署

```powershell
# 插件 DLL（Release）拷到目标 Creator 的插件目录
Copy-Item C:\qt-creator-4.15.0-build\lib\qtcreator\plugins\AiAssistant4.dll `
          "C:\Qt\Tools\QtCreator\lib\qtcreator\plugins\"      # ← 换成你实际 Creator 安装目录
```

各平台插件目录：

| 平台 | 路径 |
| --- | --- |
| Windows | `<QtCreator>/lib/qtcreator/plugins/` |
| Linux | `<QtCreator>/lib/qtcreator/plugins/`（部分 4.x 版本为 `lib/qtcreator/plugins`） |
| macOS | `Qt Creator.app/Contents/PlugIns/` |

调试符号（可选）：同目录放 `AiAssistant4.pdb`；用 Debug 版 Creator 时放 `AiAssistant4d.dll`/`.pdb`。

## 5. 启动与自检

1. 启动 Creator，首次会读 `ICore::settings()`（Windows 在
   `%APPDATA%\QtProject\qtcreator\`，可用 `-settingspath <dir>` 隔离）。
2. 菜单 **Tools → AI 助手 → AI 助手面板**（`Ctrl+Shift+A`）应打开侧边 Dock；
   若没有该菜单，说明插件未加载。
3. 查看加载日志：Creator 启动时终端/调试输出会打印插件加载失败原因
   （缺依赖、ABI 不匹配、“not a Qt plugin” 等）。
4. **Help → About Plugins…** 中应能看到 `AiAssistant`。
5. 打开设置（Dock 上「打开设置」）填 LLM 地址与模型，例如：
   * Ollama 本地：`http://127.0.0.1:11434/v1/chat/completions`，模型 `qwen2.5-coder:7b`
   * Ollama 原生：`http://127.0.0.1:11434/api/chat`（自动识别协议）
   * DeepSeek：`https://api.deepseek.com/v1/chat/completions` + API Key + 模型 `deepseek-chat`
6. 输入“看看当前项目结构”，应当只调用只读工具（不弹审批）。

## 6. 调试（断点进插件）

1. 用 **Debug** 配置构建 Creator（`CONFIG+=debug`）与插件，得到
   `qt-creator-4.15.0-build\bin\qtcreator.exe`。
2. 在 Creator 里打开 `qtcreator.pro` 作为**被调试程序**：
   `Run → Run` 选择 `qtcreator.exe`（Debug 版），工作目录设为构建树 `bin`。
3. 在插件源码里下断点（如 `ApprovalInterceptor::review`、`ToolDispatcher::apply`、
   `AgentController::step`）。
4. 更快的方式：用 `-pluginpath` 从任意目录加载插件（免拷贝）：

   ```powershell
   qtcreator.exe -pluginpath C:\my-plugins -settingspath C:\tmp\qtc-settings
   ```

5. 常见问题：
   * 断点不命中 → Debug/Release 不匹配，或 DLL 实际是旧的（清理 `lib/qtcreator/plugins` 后重建）；
   * 加载失败 → ABI 不符（Qt 补丁号、MSVC 版本、MD/MDd）；
   * 菜单不出现 → `Dependencies` 里的插件名在本机并不存在（例如把 `Designer` 写成硬依赖）。

## 7. 兼容点清单（本机已按真实 4.15 SDK 修正，均已合入源码）

| 位置 | 真实 4.15 情况 | 处理方式 |
| --- | --- | --- |
| `src/aiassistantplugin.cpp` | `Core::ActionManager` 的方法都是 **static**，id 类型是 `Utils::Id` | 直接传 `Constants::XXX`（`const char[]`，可隐式构造 `Utils::Id`）；通过实例指针调用静态方法合法 |
| `src/creator/creatorcompat.cpp` | `Utils::Id` 只有 `Id(const char*)`，**没有** `Id(QString)`，且 `Id(const QLatin1String&)` 被 `= delete` | 命令 id 查找统一用 `Utils::Id::fromString(QString)` |
| `src/aiassistantplugin.cpp` | `Core::Context` 在 `coreplugin/icontext.h` | 改 include（已改） |
| `src/creator/projectservice.cpp` | `Core::DocumentModel` 是 **class**，头文件在 `coreplugin/editormanager/documentmodel.h` | 改 include（已改）；仍保留 `AI_NO_DOCUMENT_MODEL` 逃生开关 |
| `src/creator/projectservice.cpp` | `Core::IEditor` **没有 `widget()`**（只有 `toolBar()`） | 改为监听 `QApplication::focusChanged` 记住最近聚焦的 `QPlainTextEdit`；并给审批弹窗的参数编辑器打 `aiassistantParamEditor` 标记以排除 |
| `src/creator/projectservice.cpp` | `IDocument::save(QString *errorString, …, bool)` 的 `errorString` **没有默认值** | 调用 `save(nullptr)` |
| `src/creator/projectservice.cpp` | `IDocument::filePath()` 返回 `const Utils::FilePath &`，`Project::projectDirectory()` 返回 `Utils::FilePath` | `Compat::toPathString()` 模板兼容 |
| `src/creator/buildservice.cpp` | `ProjectExplorer::BuildManager` 可用反射调用；命令 id 需探测 | 反射 `invokeMethod("buildProject")` + `buildQueueFinished(bool)`，失败再回退命令 id；id 表在 `creatorcompat.cpp` |
| `.pro` / 目录约定 | 插件必须登记进 `src/plugins/plugins.pro`；DLL 名 = `<QTC_PLUGIN_NAME><4>` | 已按此组织（`aiassistant_dependencies.pri` + `AiAssistant.json.in`） |
| 编译选项 | Creator 打开 `QT_NO_CAST_TO_ASCII`、`QT_RESTRICTED_CAST_FROM_ASCII` 等严格开关；MSVC 默认按 GBK(936) 读源码 | 插件源码全用 `QStringLiteral`（已通过严格宏编译）；`.pri` 里加 `msvc: QMAKE_CXXFLAGS += /utf-8` 并关闭 PCH |

## 7.1 实测踩坑（照着做可少走弯路）

1. **Qt 缺 `qtscript` 模块** → Creator 自带的 qbs 会尝试编译内置 QtScript 副本，需要 `perl`/`syncqt`，
   qmake 阶段直接失败。装模块即可：`aqt install-qt … --noarchives -m qtscript`。
2. **中文源码 + MSVC** → 默认按 GBK 解释，出现大量 C2079/C2146 之类"莫名其妙"的语法错误。
   加 `/utf-8` 后，又与 Creator 共享 PCH 冲突（C2855 `/source-charset` 与预编译头不一致），
   所以在插件的 `.pri` 里同时关闭 PCH（`PRECOMPILED_HEADER =` + `CONFIG -= precompile_header`）。
3. **插件描述文件引号**：`AiAssistant.json.in` 里的字面引号必须写成 `\"`，
   否则 `QMAKE_SUBSTITUTES` 会把裸引号吃掉，生成非法 JSON（moc 报插件元数据无效/缺失）。
4. **插件没被编译**：不在 `plugins.pro` 的 `SUBDIRS` 里，qmake 根本不会生成该子工程的 Makefile。
5. **只重建插件**：顶层 Makefile 里没有 `sub-aiassistant` 目标，要在 `C:\build\src\plugins` 目录里
   执行 `jom sub-aiassistant`。
6. **不要重复部署旧 DLL**：moc 内嵌的元数据只在 `aiassistantplugin.cpp` 重新编译时更新；
   改了 `.json.in` 后记得删掉该 obj/moc 文件再重建（或整目录重建）。
7. **QtUiTools 在 Windows 是静态库**（Qt 安装里只有 `Qt5UiTools.lib`，没有 DLL），
   所以官方 Creator 的 bin 里没有 `Qt5UiTools.dll` 也不会影响本插件。

## 8. 卸载 / 回滚

```powershell
# 插件无独立安装程序：删除 DLL 即卸载
Remove-Item "<QtCreator>\lib\qtcreator\plugins\AiAssistant4.dll"
```

配置与历史（可一并删除）：

```
<settings>/AiAssistant/*                 # QSettings 里的插件配置
<settings>/aiassistant/history/*.json    # 按项目隔离的对话历史
```
