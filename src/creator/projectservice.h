/****************************************************************************
** 项目 / 文件读取与刷新（只读为主，写入由 ToolDispatcher 统一负责）
**
** 职责：
**   1) 取 SessionManager 的激活项目，用正则解析 .pro 提取
**      HEADERS / SOURCES / FORMS / RESOURCES / DISTFILES
**   2) 读取文件（优先走已打开文档，回退 QFile）
**   3) 写盘后通知 Creator 刷新已打开文档（IDocument::setContents）
**   4) 当前编辑器光标插入 / 选区读取
****************************************************************************/

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QPlainTextEdit;

namespace AiAssistant {

struct ProjectFileLists
{
    QStringList headers;
    QStringList sources;
    QStringList forms;
    QStringList resources;
    QStringList other;
};

struct ProjectSnapshot
{
    bool hasProject = false;
    QString projectName;
    QString projectFilePath;
    QString projectDirectory;
    QStringList proFiles;
    ProjectFileLists files;
    QStringList openDocuments;
    QString generatedAt;

    QJsonObject toJson() const;
    QString toSummary() const;
};

class ProjectService : public QObject
{
    Q_OBJECT
public:
    explicit ProjectService(QObject *parent = nullptr);

    ProjectSnapshot snapshot() const;     // 惰性刷新
    void invalidate();
    ProjectSnapshot refresh();

    bool hasProject() const;
    QString projectDirectory() const;

    QString resolvePath(const QString &path, QString *error) const;
    QString relativeToProject(const QString &path) const;

    QString readTextFile(const QString &path, QString *error, int maxBytes = 1024 * 1024) const;
    QByteArray readFileBytes(const QString &path, bool *ok) const;
    bool writeFileBytes(const QString &path, const QByteArray &data, QString *error) const;
    bool fileExists(const QString &path) const;

    bool isOpenAndModified(const QString &path) const;
    QString documentContents(const QString &path) const;
    bool setDocumentContents(const QString &path, const QByteArray &data, QString *error);
    void notifyFilesChanged(const QStringList &paths);

    QString currentEditorFilePath() const;
    QString currentEditorSelection() const;
    bool insertTextAtCursor(const QString &text, QString *error);

    static ProjectFileLists parseProFile(const QString &proPath);

signals:
    void projectChanged();

private slots:
    // Creator 4.15 的 Core::IEditor 只有 toolBar()，没有 widget()，
    // 因此这里通过 QApplication::focusChanged 记住“最近获得焦点的代码编辑器控件”
    void onFocusChanged(QWidget *oldWidget, QWidget *newWidget);

private:
    ProjectSnapshot collect();
    static void scanDirectory(const QString &root, ProjectFileLists *lists, int depth = 0);
    QPlainTextEdit *currentPlainTextEdit() const;

    mutable ProjectSnapshot m_snapshot;
    mutable bool m_valid = false;
    QPointer<QPlainTextEdit> m_lastEditorWidget;
};

} // namespace AiAssistant
