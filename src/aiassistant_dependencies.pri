# AI Assistant 插件的依赖声明（qtcreatorplugin.pri 要求本文件与目录同名）
#
# QTC_PLUGIN_NAME 决定插件 DLL 名：Qt Creator 4.x 在 win32 下会拼成 <Name><主版本>.dll，
# 即 AiAssistant4.dll。
QTC_PLUGIN_NAME = AiAssistant

QTC_LIB_DEPENDS += \
    aggregation \
    extensionsystem \
    utils

QTC_PLUGIN_DEPENDS += \
    coreplugin \
    projectexplorer
