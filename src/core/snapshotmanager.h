/****************************************************************************
** 快照 / 回滚
**
** 环形队列：struct SnapshotItem { filePath, oldContent, time }
**   * 每次“审批通过、写入执行前”自动打一个批次快照；
**   * 一个批次可以包含多个文件（如 add_qt_class 同时改 .h/.cpp/.pro）；
**   * 超出 maxSnapshots 时丢弃最早的批次；
**   * 回滚 = 把 oldContent 写回文件（或编辑器缓冲区）并通知 Creator 刷新。
**
** 回滚入口独立于 AI 流程（Dock 上的“回滚”按钮），不需要审批，
** 但会二次确认；它恢复的是“用户自己确认过的历史状态”。
****************************************************************************/

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace AiAssistant {

struct SnapshotItem
{
    QString filePath;
    QByteArray oldContent;
    bool existedBefore = true;
    bool editorDocument = false;   // 针对未保存的编辑器缓冲区（insert_code_at_cursor）
    qint64 time = 0;
    QString toolName;
};

struct SnapshotBatch
{
    QString id;
    QString title;
    qint64 time = 0;
    QVector<SnapshotItem> items;

    QString displayText() const;
    QString fileSummary() const;
};

class SnapshotManager : public QObject
{
    Q_OBJECT
public:
    // 由插件注入的落盘/刷新能力，避免本类直接依赖 Creator
    struct Hooks
    {
        std::function<bool(const QString &path, const QByteArray &data, QString *error)> writeFile;
        std::function<bool(const QString &path, QString *error)> deleteFile;
        std::function<bool(const QString &path, const QByteArray &data, QString *error)> restoreEditor;
        std::function<void(const QStringList &paths)> notifyChanged;
    };

    explicit SnapshotManager(QObject *parent = nullptr);

    void setHooks(const Hooks &hooks);
    void setMaxBatches(int maxBatches);
    int maxBatches() const { return m_maxBatches; }

    QString beginBatch(const QString &title);
    void captureFile(const QString &batchId, const QString &path, const QString &toolName = QString());
    void captureEditorDocument(const QString &batchId, const QString &path, const QByteArray &content,
                               const QString &toolName = QString());
    void commitBatch(const QString &batchId);
    void discardBatch(const QString &batchId);

    QVector<SnapshotBatch> batches() const { return m_batches; }
    int batchCount() const { return m_batches.size(); }
    bool batch(const QString &id, SnapshotBatch *out) const;
    bool lastBatch(SnapshotBatch *out) const;

    bool rollbackBatch(const QString &batchId, QStringList *restoredFiles, QString *error);
    bool rollbackLast(QStringList *restoredFiles, QString *error);

signals:
    void batchesChanged();

private:
    int indexOf(const QString &batchId) const;

    Hooks m_hooks;
    QVector<SnapshotBatch> m_batches;
    int m_maxBatches = 50;
    int m_counter = 0;
};

} // namespace AiAssistant
