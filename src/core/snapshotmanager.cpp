#include "core/snapshotmanager.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace AiAssistant {

QString SnapshotBatch::displayText() const
{
    const QString stamp = QDateTime::fromMSecsSinceEpoch(time).toString(QStringLiteral("MM-dd HH:mm:ss"));
    return QStringLiteral("%1  %2  （%3 个文件）").arg(stamp, title).arg(items.size());
}

QString SnapshotBatch::fileSummary() const
{
    QStringList names;
    for (const SnapshotItem &item : items) {
        const QString name = QFileInfo(item.filePath).fileName();
        if (!name.isEmpty() && !names.contains(name))
            names.append(name);
        if (names.size() >= 4)
            break;
    }
    return names.join(QStringLiteral(", "));
}

SnapshotManager::SnapshotManager(QObject *parent)
    : QObject(parent)
{
}

void SnapshotManager::setHooks(const Hooks &hooks)
{
    m_hooks = hooks;
}

void SnapshotManager::setMaxBatches(int maxBatches)
{
    m_maxBatches = qBound(1, maxBatches, 5000);
    while (m_batches.size() > m_maxBatches)
        m_batches.removeFirst();
    emit batchesChanged();
}

QString SnapshotManager::beginBatch(const QString &title)
{
    SnapshotBatch batch;
    batch.id = QStringLiteral("snap-%1-%2").arg(++m_counter).arg(QDateTime::currentMSecsSinceEpoch());
    batch.title = title.trimmed().isEmpty() ? QStringLiteral("未命名快照") : title.trimmed();
    batch.time = QDateTime::currentMSecsSinceEpoch();
    m_batches.append(batch);
    emit batchesChanged();
    return batch.id;
}

int SnapshotManager::indexOf(const QString &batchId) const
{
    for (int i = 0; i < m_batches.size(); ++i) {
        if (m_batches.at(i).id == batchId)
            return i;
    }
    return -1;
}

void SnapshotManager::captureFile(const QString &batchId, const QString &path, const QString &toolName)
{
    const int index = indexOf(batchId);
    if (index < 0 || path.isEmpty())
        return;

    SnapshotBatch &batch = m_batches[index];
    for (const SnapshotItem &item : batch.items) {
        if (item.filePath == path && !item.editorDocument)
            return;   // 同一批次内同一文件只保留最早的原始内容
    }

    SnapshotItem item;
    item.filePath = path;
    item.time = QDateTime::currentMSecsSinceEpoch();
    item.toolName = toolName;
    item.existedBefore = QFileInfo::exists(path);
    if (item.existedBefore) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;   // 读不到就不产生“假快照”
        item.oldContent = file.readAll();
        file.close();
    }

    batch.items.append(item);
    emit batchesChanged();
}

void SnapshotManager::captureEditorDocument(const QString &batchId, const QString &path,
                                            const QByteArray &content, const QString &toolName)
{
    const int index = indexOf(batchId);
    if (index < 0)
        return;

    SnapshotItem item;
    item.filePath = path;
    item.oldContent = content;
    item.existedBefore = true;
    item.editorDocument = true;
    item.time = QDateTime::currentMSecsSinceEpoch();
    item.toolName = toolName;
    m_batches[index].items.append(item);
    emit batchesChanged();
}

void SnapshotManager::commitBatch(const QString &batchId)
{
    const int index = indexOf(batchId);
    if (index < 0)
        return;

    if (m_batches.at(index).items.isEmpty()) {   // 空批次没有回滚价值
        m_batches.removeAt(index);
        emit batchesChanged();
        return;
    }

    while (m_batches.size() > m_maxBatches)
        m_batches.removeFirst();
    emit batchesChanged();
}

void SnapshotManager::discardBatch(const QString &batchId)
{
    const int index = indexOf(batchId);
    if (index < 0)
        return;
    m_batches.removeAt(index);
    emit batchesChanged();
}

bool SnapshotManager::batch(const QString &id, SnapshotBatch *out) const
{
    const int index = indexOf(id);
    if (index < 0)
        return false;
    if (out)
        *out = m_batches.at(index);
    return true;
}

bool SnapshotManager::lastBatch(SnapshotBatch *out) const
{
    if (m_batches.isEmpty())
        return false;
    if (out)
        *out = m_batches.last();
    return true;
}

bool SnapshotManager::rollbackBatch(const QString &batchId, QStringList *restoredFiles, QString *error)
{
    const int index = indexOf(batchId);
    if (index < 0) {
        if (error)
            *error = QStringLiteral("快照不存在或已被丢弃：%1").arg(batchId);
        return false;
    }

    const SnapshotBatch batch = m_batches.at(index);
    QStringList restored;
    QStringList notify;

    // 逆序恢复：后做的修改先被撤销
    for (int i = batch.items.size() - 1; i >= 0; --i) {
        const SnapshotItem &item = batch.items.at(i);
        QString itemError;

        if (item.editorDocument) {
            if (!m_hooks.restoreEditor) {
                if (error)
                    *error = QStringLiteral("未注入编辑器恢复能力，无法回滚 %1").arg(item.filePath);
                return false;
            }
            if (!m_hooks.restoreEditor(item.filePath, item.oldContent, &itemError)) {
                if (error)
                    *error = itemError;
                return false;
            }
            restored.append(item.filePath);
            continue;
        }

        if (item.existedBefore) {
            if (!m_hooks.writeFile || !m_hooks.writeFile(item.filePath, item.oldContent, &itemError)) {
                if (error)
                    *error = itemError.isEmpty() ? QStringLiteral("无法写回 %1").arg(item.filePath) : itemError;
                return false;
            }
        } else {
            if (!m_hooks.deleteFile || !m_hooks.deleteFile(item.filePath, &itemError)) {
                if (error)
                    *error = itemError.isEmpty() ? QStringLiteral("无法删除 %1").arg(item.filePath) : itemError;
                return false;
            }
        }
        restored.append(item.filePath);
        notify.append(item.filePath);
    }

    if (m_hooks.notifyChanged && !notify.isEmpty())
        m_hooks.notifyChanged(notify);

    m_batches.removeAt(index);
    emit batchesChanged();

    if (restoredFiles)
        *restoredFiles = restored;
    return true;
}

bool SnapshotManager::rollbackLast(QStringList *restoredFiles, QString *error)
{
    if (m_batches.isEmpty()) {
        if (error)
            *error = QStringLiteral("没有任何快照可回滚");
        return false;
    }
    return rollbackBatch(m_batches.last().id, restoredFiles, error);
}

} // namespace AiAssistant
