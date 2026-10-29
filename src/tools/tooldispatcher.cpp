#include "tools/tooldispatcher.h"

#include "creator/buildservice.h"
#include "creator/projectservice.h"
#include "core/snapshotmanager.h"
#include "tools/toolregistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace AiAssistant {

ToolDispatcher::ToolDispatcher(QObject *parent)
    : QObject(parent)
{
}

void ToolDispatcher::logMessage(const QString &level, const QString &text)
{
    emit log(level, text);
}

ToolResult ToolDispatcher::executeReadOnly(const ToolCall &call)
{
    if (!m_registry || !m_runtime) {
        ToolResult result = ToolResult::fail(tr("工具系统尚未初始化"), true);
        result.callId = call.id;
        result.toolName = call.name;
        return result;
    }

    logMessage(QStringLiteral("info"), tr("调用只读工具 %1").arg(call.name));
    FileOverlay overlay;   // 只读工具不写叠加层
    PrepareContext context(m_runtime, &overlay);
    return m_registry->executeRead(call, context);
}

bool ToolDispatcher::prepareWrites(const QVector<ToolCall> &calls, ApprovalRequest *request,
                                   QString *error)
{
    if (!request) {
        if (error)
            *error = tr("内部错误：ApprovalRequest 为空");
        return false;
    }
    if (!m_registry || !m_runtime) {
        if (error)
            *error = tr("工具系统尚未初始化");
        return false;
    }
    if (calls.isEmpty()) {
        if (error)
            *error = tr("没有需要审批的调用");
        return false;
    }

    FileOverlay overlay;
    PrepareContext context(m_runtime, &overlay);

    QVector<ToolPlan> plans;
    plans.reserve(calls.size());
    for (const ToolCall &call : calls) {
        if (!m_registry->contains(call.name)) {
            if (error)
                *error = tr("未知工具：%1").arg(call.name);
            return false;
        }
        ToolPlan plan = m_registry->prepareWrite(call, context);
        if (!plan.ok) {
            if (error) {
                *error = plan.error.isEmpty() ? tr("工具 %1 准备失败").arg(call.name) : plan.error;
            }
            return false;
        }
        plans.append(plan);
    }

    ApprovalRequest req;
    req.calls = calls;
    req.plans = plans;

    // 聚合每个文件的最终变更：before = 磁盘现状，after = 叠加层最终内容
    for (const QString &path : overlay.touchedFiles()) {
        FileChange change;
        change.path = path;
        change.existedBefore = QFileInfo::exists(path);
        if (change.existedBefore) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                change.before = file.readAll();
                file.close();
            }
        }
        change.after = overlay.changes().value(path);
        if (change.before == change.after && change.existedBefore)
            continue;   // 没有任何实际变化
        req.changes.append(change);
    }

    const bool hasExternalAction = [&plans]() {
        for (const ToolPlan &plan : plans) {
            if (plan.externalAction)
                return true;
        }
        return false;
    }();

    if (req.changes.isEmpty() && !hasExternalAction) {
        if (error)
            *error = tr("这些操作没有产生任何实际变更（可能目标内容已一致）");
        return false;
    }

    // 标题 / 摘要 / 类型
    bool hasUi = false;
    bool hasCode = false;
    QString firstKind;
    QStringList summaries;
    for (const ToolPlan &plan : plans) {
        if (plan.kind == QLatin1String("ui"))
            hasUi = true;
        else if (plan.kind == QLatin1String("code") || plan.kind == QLatin1String("project"))
            hasCode = true;
        if (firstKind.isEmpty())
            firstKind = plan.kind;
        if (!plan.summary.trimmed().isEmpty())
            summaries.append(QStringLiteral("· ") + plan.summary.trimmed());
    }

    if (plans.size() == 1) {
        req.title = plans.first().title.trimmed().isEmpty() ? calls.first().name
                                                            : plans.first().title.trimmed();
    } else {
        req.title = tr("批量操作（%1 项，合并为一张审批单）").arg(plans.size());
    }

    if (hasUi && hasCode)
        req.kind = QStringLiteral("ui+code");
    else if (hasUi)
        req.kind = QStringLiteral("ui");
    else if (hasCode)
        req.kind = QStringLiteral("code");
    else
        req.kind = firstKind;

    req.summary = summaries.join(QLatin1Char('\n'));

    *request = req;
    return true;
}

ToolDispatcher::ApplyOutcome ToolDispatcher::apply(const ApprovalRequest &request)
{
    ApplyOutcome outcome;
    if (!m_registry || !m_runtime) {
        outcome.ok = false;
        outcome.error = tr("工具系统尚未初始化");
        return outcome;
    }
    if (request.plans.isEmpty()) {
        outcome.ok = false;
        outcome.error = tr("没有可以执行的计划");
        return outcome;
    }

    outcome.results.resize(request.plans.size());
    auto setResult = [&outcome](int index, const ToolResult &result) {
        if (index >= 0 && index < outcome.results.size()) {
            outcome.results[index] = result;
            outcome.results[index].callId = result.callId;
        }
    };

    bool needsSnapshot = !request.changes.isEmpty();
    for (const ToolPlan &plan : request.plans) {
        if (plan.kind == QLatin1String("editor"))
            needsSnapshot = true;
    }
    if (needsSnapshot && m_runtime->snapshots)
        outcome.snapshotBatchId = m_runtime->snapshots->beginBatch(request.title);

    // ---------- 1) 编辑器缓冲区插入（快照基于插入前内容） ----------
    for (int i = 0; i < request.plans.size(); ++i) {
        const ToolPlan &plan = request.plans.at(i);
        if (plan.kind != QLatin1String("editor"))
            continue;

        const QString filePath = plan.payload.value(QStringLiteral("filePath")).toString();
        const QString code = plan.payload.value(QStringLiteral("code")).toString();

        // 审批期间用户可能切换了编辑器：目标不一致就不插，避免插错文件
        const QString currentFile = m_runtime->project->currentEditorFilePath();
        if (!filePath.isEmpty() && !currentFile.isEmpty()
            && QDir::cleanPath(filePath) != QDir::cleanPath(currentFile)) {
            ToolResult mismatch = ToolResult::fail(
                tr("审批期间编辑器已从 %1 切换到 %2，为避免插错文件，本次插入已取消。请重新发起。")
                    .arg(m_runtime->project->relativeToProject(filePath),
                         m_runtime->project->relativeToProject(currentFile)),
                false);
            mismatch.callId = plan.call.id;
            mismatch.toolName = plan.toolName;
            setResult(i, mismatch);
            outcome.ok = false;
            outcome.error = mismatch.error;
            continue;
        }

        if (!outcome.snapshotBatchId.isEmpty() && m_runtime->snapshots && !filePath.isEmpty()) {
            const QString contents = m_runtime->project->documentContents(filePath);
            m_runtime->snapshots->captureEditorDocument(outcome.snapshotBatchId, filePath,
                                                        contents.toUtf8(), plan.toolName);
        }

        QString insertError;
        ToolResult result;
        if (m_runtime->project->insertTextAtCursor(code, &insertError)) {
            result = ToolResult::ok(tr("已在光标处插入 %1 行代码（可用 Ctrl+Z 撤销）").arg(code.count(QLatin1Char('\n')) + 1), false);
        } else {
            result = ToolResult::fail(insertError, false);
            outcome.ok = false;
            outcome.error = insertError;
        }
        result.callId = plan.call.id;
        result.toolName = plan.toolName;
        setResult(i, result);
    }

    // ---------- 2) 写盘（先打快照，再写） ----------
    QStringList written;
    if (!request.changes.isEmpty() && m_runtime->snapshots && !outcome.snapshotBatchId.isEmpty()) {
        for (const FileChange &change : request.changes)
            m_runtime->snapshots->captureFile(outcome.snapshotBatchId, change.path, request.kind);
    }

    for (const FileChange &change : request.changes) {
        if (change.deleteFile) {
            QFile::remove(change.path);
            written.append(change.path);
            continue;
        }

        QString writeError;
        if (!m_runtime->project->writeFileBytes(change.path, change.after, &writeError)) {
            // 写入失败：自动回滚整批，避免半成品
            QStringList restored;
            QString rollbackError;
            if (m_runtime->snapshots && !outcome.snapshotBatchId.isEmpty())
                m_runtime->snapshots->rollbackBatch(outcome.snapshotBatchId, &restored, &rollbackError);

            outcome.ok = false;
            outcome.error = tr("写入 %1 失败：%2").arg(change.path, writeError);
            for (int i = 0; i < request.plans.size(); ++i) {
                if (request.plans.at(i).kind == QLatin1String("editor"))
                    continue;
                ToolResult result = ToolResult::fail(outcome.error, false);
                result.callId = request.plans.at(i).call.id;
                result.toolName = request.plans.at(i).toolName;
                setResult(i, result);
            }
            return outcome;
        }
        written.append(change.path);
    }

    if (!written.isEmpty()) {
        m_runtime->project->notifyFilesChanged(written);
        emit filesChanged(written);
        logMessage(QStringLiteral("info"),
                   tr("已写入 %1 个文件，已通知 Creator 刷新").arg(written.size()));
    }

    // ---------- 3) 外部动作：构建 / 运行 ----------
    bool pendingAssigned = false;
    for (int i = 0; i < request.plans.size(); ++i) {
        const ToolPlan &plan = request.plans.at(i);
        if (plan.kind == QLatin1String("editor"))
            continue;

        ToolResult result;
        result.callId = plan.call.id;
        result.toolName = plan.toolName;

        if (!plan.externalAction) {
            result = ToolResult::ok(plan.summary.trimmed().isEmpty() ? tr("已完成") : plan.summary.trimmed(),
                                    false);
            result.callId = plan.call.id;
            result.toolName = plan.toolName;
            setResult(i, result);
            continue;
        }

        if (pendingAssigned) {
            result = ToolResult::fail(tr("同一批审批单里只能启动一个构建/运行动作"), false);
            setResult(i, result);
            continue;
        }

        if (plan.kind == QLatin1String("build")) {
            QString buildError;
            if (!m_runtime->build->startBuild(plan.payload.value(QStringLiteral("target")).toString(),
                                              &buildError)) {
                result = ToolResult::fail(buildError, false);
                setResult(i, result);
                outcome.ok = false;
                if (outcome.error.isEmpty())
                    outcome.error = buildError;
                continue;
            }
            result = ToolResult::ok(tr("构建已启动，等待构建结束…"), false);
            result.pending = true;
            setResult(i, result);
            outcome.pending = true;
            outcome.pendingCallId = plan.call.id;
            outcome.pendingToolName = plan.toolName;
            m_pendingCallId = plan.call.id;
            m_pendingToolName = plan.toolName;
            pendingAssigned = true;
        } else if (plan.kind == QLatin1String("run")) {
            QString runError;
            if (!m_runtime->build->startRun(&runError)) {
                result = ToolResult::fail(runError, false);
                setResult(i, result);
                outcome.ok = false;
                if (outcome.error.isEmpty())
                    outcome.error = runError;
                continue;
            }
            result = ToolResult::ok(tr("程序已启动，正在采集输出…"), false);
            result.pending = true;
            setResult(i, result);
            outcome.pending = true;
            outcome.pendingCallId = plan.call.id;
            outcome.pendingToolName = plan.toolName;
            m_pendingCallId = plan.call.id;
            m_pendingToolName = plan.toolName;
            pendingAssigned = true;
        }
    }

    outcome.changedFiles = written;
    if (m_runtime->snapshots && !outcome.snapshotBatchId.isEmpty())
        m_runtime->snapshots->commitBatch(outcome.snapshotBatchId);

    return outcome;
}

void ToolDispatcher::completeAsyncTool(ToolResult result)
{
    if (result.callId.isEmpty())
        result.callId = m_pendingCallId;
    if (result.toolName.isEmpty())
        result.toolName = m_pendingToolName;
    m_pendingCallId.clear();
    m_pendingToolName.clear();
    emit asyncToolResult(result);
}

void ToolDispatcher::clearPendingAsync()
{
    m_pendingCallId.clear();
    m_pendingToolName.clear();
}

} // namespace AiAssistant
