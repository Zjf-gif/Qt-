# _syntaxcheck · 编译级自检工程（可选，不属于插件产物）

目的：在没有 Qt Creator 4.15 源码/SDK 的机器上，验证**插件自身代码**能否编译通过
（类型、函数签名、moc 信号槽、Qt5 API 使用、DOM 生成、SSE 解析、Diff 算法等）。

做法：

* `stubs/` 里是 Creator SDK 的**最小桩头文件**，只声明本插件实际用到的符号
  （`ICore`、`ActionManager`/`Command`/`ActionContainer`、`Context`、`Utils::Id`、
  `Utils::FilePath`、`IDocument`/`IEditor`/`EditorManager`/`DocumentModel`、
  `ProjectExplorer::Project`/`SessionManager`、`ExtensionSystem::IPlugin`）。
* `QtDesigner`、`QtUiTools` 用的是**真实**的 Qt 头文件（不是桩），所以 Designer 采集与
  QUiLoader 预览这两块是真实 API 校验。
* `check_all.pro` 是全量工程（29 个 .cpp + 全部头文件 + moc）。

运行（Windows，Qt + MinGW 或 MSVC 均可）：

```powershell
cd _syntaxcheck\build
qmake ..\check_all.pro -spec win32-g++ CONFIG+=release
mingw32-make -j4          # 或 nmake / jom
```

预期：**零错误、零警告**（已在 MinGW 7.3 + Qt 5.12 上验证；插件目标环境是 Qt 5.15，
代码只用到 5.12 就已存在的 API）。

> 注意：
> 1. 桩头文件只用于编译自检，**不能**用来链接进真实 Creator；
> 2. 真实构建请按 [../docs/02-build-and-deploy.md](../docs/02-build-and-deploy.md)
>    把 `../src` 放进 Creator 源码树编译；
> 3. 若构建目录路径包含非 ASCII 字符（例如中文目录），旧版 qmake/moc 可能报
>    "Failed to resolve moc_predefs.h" 之类的警告，把构建目录放到纯 ASCII 路径即可。
