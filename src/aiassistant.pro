#==============================================================================
# AI Assistant Plugin for Qt Creator 4.15.0  （in-tree 工程文件）
#
# 用法：
#   1) 把整个 src/ 目录拷贝成
#        <qt-creator-4.15.0>/src/plugins/aiassistant/
#   2) 在 <qt-creator-4.15.0>/src/plugins/plugins.pro 的 SUBDIRS 里加一行 aiassistant
#   3) 用与 Creator 相同的 Qt 5.15.2 MSVC2019 kit 重新 qmake 并构建
#   4) 产物：<build>/lib/qtcreator/plugins/AiAssistant4.dll
#
# 说明：
#   * TARGET / 依赖 / 插件描述文件命名全部由官方 qtcreatorplugin.pri 依据
#     aiassistant_dependencies.pri 里的 QTC_PLUGIN_NAME(=AiAssistant) 推导，
#     因此这里 **不要** 再设置 TARGET；
#   * 插件描述用 AiAssistant.json.in，qmake 会替换 $$QTCREATOR_VERSION 与 $$dependencyList
#     并在构建目录生成 AiAssistant.json 供 moc 内嵌；
#   * Qt Creator 4.x 的插件 DLL 名为 <QTC_PLUGIN_NAME><IDE 主版本>.dll，即 AiAssistant4.dll。
#==============================================================================

include(../../qtcreatorplugin.pri)

include($$PWD/aiassistant_sources.pri)
