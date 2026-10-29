/****************************************************************************
** 工具运行期上下文
**
** FileOverlay 是“修改类工具 prepare 阶段”的沙箱：
**   prepare 只读写内存叠加层，绝不碰磁盘；审批通过后由 ToolDispatcher 一次性写盘。
** 这样保证：
**   * 审批前磁盘一定没变；
**   * 同一批多次修改同一文件时，后一次基于前一次的结果（链式）；
**   * 审批弹窗里的 Diff / UI 预览看到的正是即将写入的内容。
****************************************************************************/

#pragma once

#include "core/agenttypes.h"
#include "creator/projectservice.h"
#include "settings/agentsettings.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

namespace AiAssistant {

class DesignerBridge;
class BuildService;
class SnapshotManager;

struct ToolRuntime
{
    AgentSettings *settings = nullptr;
    ProjectService *project = nullptr;
    DesignerBridge *designer = nullptr;
    BuildService *build = nullptr;
    SnapshotManager *snapshots = nullptr;
    std::function<void(const QString &level, const QString &text)> log;
};

class FileOverlay
{
public:
    void reset()
    {
        m_overlay.clear();
        m_order.clear();
    }

    bool contains(const QString &path) const { return m_overlay.contains(path); }

    QByteArray read(const QString &path, bool *ok) const
    {
        auto it = m_overlay.constFind(path);
        if (it != m_overlay.constEnd()) {
            if (ok)
                *ok = true;
            return it.value();
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (ok)
                *ok = false;
            return QByteArray();
        }
        const QByteArray data = file.readAll();
        file.close();
        if (ok)
            *ok = true;
        return data;
    }

    void write(const QString &path, const QByteArray &data)
    {
        if (!m_overlay.contains(path))
            m_order.append(path);
        m_overlay.insert(path, data);
    }

    void erase(const QString &path)
    {
        m_overlay.remove(path);
    }

    bool exists(const QString &path) const
    {
        if (m_overlay.contains(path))
            return true;
        return QFileInfo::exists(path);
    }

    QStringList touchedFiles() const { return m_order; }
    const QHash<QString, QByteArray> &changes() const { return m_overlay; }

private:
    QHash<QString, QByteArray> m_overlay;
    QStringList m_order;
};

class PrepareContext
{
public:
    PrepareContext(ToolRuntime *runtime, FileOverlay *overlay)
        : m_runtime(runtime)
        , m_overlay(overlay)
    {
    }

    ToolRuntime *runtime() const { return m_runtime; }
    AgentSettings *settings() const { return m_runtime ? m_runtime->settings : nullptr; }
    ProjectService *project() const { return m_runtime ? m_runtime->project : nullptr; }
    DesignerBridge *designer() const { return m_runtime ? m_runtime->designer : nullptr; }
    BuildService *build() const { return m_runtime ? m_runtime->build : nullptr; }

    QByteArray readBytes(const QString &path, bool *exists, QString *error) const
    {
        if (exists)
            *exists = m_overlay->exists(path);
        bool ok = false;
        const QByteArray data = m_overlay->read(path, &ok);
        if (!ok && error)
            *error = QStringLiteral("无法读取文件：%1").arg(path);
        return data;
    }

    QString readText(const QString &path, QString *error) const
    {
        bool exists = false;
        const QByteArray data = readBytes(path, &exists, error);
        return QString::fromUtf8(data);
    }

    void writeBytes(const QString &path, const QByteArray &data) { m_overlay->write(path, data); }
    bool exists(const QString &path) const { return m_overlay->exists(path); }
    QStringList touchedFiles() const { return m_overlay->touchedFiles(); }
    QByteArray overlayContent(const QString &path) const
    {
        return m_overlay->changes().value(path);
    }

    void log(const QString &level, const QString &text) const
    {
        if (m_runtime && m_runtime->log)
            m_runtime->log(level, text);
    }

    ProjectService *serviceProject() const { return project(); }

private:
    ToolRuntime *m_runtime = nullptr;
    FileOverlay *m_overlay = nullptr;
};

} // namespace AiAssistant
