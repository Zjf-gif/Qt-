/****************************************************************************
** Creator SDK 兼容层（唯一适配点）
**
** Qt Creator 各版本之间存在 API 差异，尤其是：
**   * IDocument::filePath() 在部分版本返回 Utils::FilePath，部分返回 QString
**   * 构建/运行的触发入口（BuildManager / 已注册 Command）在不同版本命名不同
**   * DocumentModel 的可用性
**
** 所有“可能随版本变化”的调用都收敛到本文件：
**   * 路径返回值统一走 toPathString()（模板 + SFINAE，QString / Utils::FilePath 都能编译）
**   * 构建/运行命令 id 全部以候选列表形式给出，运行时探测
**   * 若某个 SDK 版本缺 DocumentModel，可用 DEFINES += AI_NO_DOCUMENT_MODEL 关闭
****************************************************************************/

#pragma once

#include <QString>
#include <QStringList>

class QObject;

namespace AiAssistant {
namespace Compat {

// QString 与 Utils::FilePath 通用转换
inline QString toPathString(const QString &value)
{
    return value;
}

template <typename T>
inline QString toPathString(const T &value)
{
    return value.toString();
}

// 在 QObject 树中按类名查找对象（用于反射调用不稳定的 BuildManager 接口）
QObject *findObjectByClassName(const QStringList &classNames);

// 按候选 id 触发 Creator 已注册的命令，返回是否成功
bool triggerCommand(const QStringList &commandIds);

bool commandExists(const QString &commandId);

// --- 命令 id 候选（如与本机 Creator 不符，只改这里）------------------------
QStringList buildCommandCandidates();
QStringList runCommandCandidates();
QStringList stopCommandCandidates();

} // namespace Compat
} // namespace AiAssistant
