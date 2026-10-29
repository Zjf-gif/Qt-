#include "creator/projectservice.h"

#include "creator/creatorcompat.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/idocument.h>
#ifndef AI_NO_DOCUMENT_MODEL
#include <coreplugin/editormanager/documentmodel.h>
#endif
#include <projectexplorer/project.h>
#include <projectexplorer/session.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTextCursor>
#include <QWidget>

// Qt 5.14 起 SkipEmptyParts 移到了 Qt 命名空间；这里保持 5.12+ 都能编译
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
#  define AI_SKIP_EMPTY_PARTS QString::SkipEmptyParts
#else
#  define AI_SKIP_EMPTY_PARTS Qt::SkipEmptyParts
#endif

namespace AiAssistant {

namespace {

QString cleanPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString relativeIfInside(const QString &path, const QString &baseDir)
{
    if (baseDir.isEmpty())
        return path;
    const QString normalizedBase = cleanPath(baseDir);
    const QString normalizedPath = cleanPath(path);
    if (normalizedPath.startsWith(normalizedBase + QLatin1Char('/')))
        return normalizedPath.mid(normalizedBase.size() + 1);
    return normalizedPath;
}

QJsonArray pathArray(const QStringList &paths, const QString &baseDir, int limit = 400)
{
    QStringList result;
    for (int i = 0; i < paths.size() && i < limit; ++i)
        result.append(relativeIfInside(paths.at(i), baseDir));
    if (paths.size() > limit)
        result.append(QStringLiteral("...（还有 %1 个文件已省略）").arg(paths.size() - limit));
    return QJsonArray::fromStringList(result);
}

void appendUnique(QStringList *target, const QStringList &values)
{
    for (const QString &value : values) {
        if (!target->contains(value))
            target->append(value);
    }
}

Core::IDocument *documentForPath(const QString &path)
{
    const QString normalized = cleanPath(path);

#ifndef AI_NO_DOCUMENT_MODEL
    const QList<Core::IDocument *> documents = Core::DocumentModel::openedDocuments();
    for (Core::IDocument *document : documents) {
        if (!document)
            continue;
        if (cleanPath(Compat::toPathString(document->filePath())) == normalized)
            return document;
    }
#endif

    if (Core::IEditor *editor = Core::EditorManager::currentEditor()) {
        if (Core::IDocument *document = editor->document()) {
            if (cleanPath(Compat::toPathString(document->filePath())) == normalized)
                return document;
        }
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------

QJsonObject ProjectSnapshot::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("hasProject"), hasProject);
    root.insert(QStringLiteral("projectName"), projectName);
    root.insert(QStringLiteral("projectFile"), relativeIfInside(projectFilePath, projectDirectory));
    root.insert(QStringLiteral("projectDirectory"), projectDirectory);
    root.insert(QStringLiteral("proFiles"), pathArray(proFiles, projectDirectory));

    QJsonObject files;
    files.insert(QStringLiteral("headers"), pathArray(this->files.headers, projectDirectory));
    files.insert(QStringLiteral("sources"), pathArray(this->files.sources, projectDirectory));
    files.insert(QStringLiteral("forms"), pathArray(this->files.forms, projectDirectory));
    files.insert(QStringLiteral("resources"), pathArray(this->files.resources, projectDirectory));
    files.insert(QStringLiteral("other"), pathArray(this->files.other, projectDirectory, 120));
    root.insert(QStringLiteral("files"), files);

    QJsonObject counts;
    counts.insert(QStringLiteral("headers"), this->files.headers.size());
    counts.insert(QStringLiteral("sources"), this->files.sources.size());
    counts.insert(QStringLiteral("forms"), this->files.forms.size());
    counts.insert(QStringLiteral("resources"), this->files.resources.size());
    root.insert(QStringLiteral("counts"), counts);

    root.insert(QStringLiteral("openDocuments"), pathArray(openDocuments, projectDirectory, 60));
    root.insert(QStringLiteral("generatedAt"), generatedAt);
    return root;
}

QString ProjectSnapshot::toSummary() const
{
    if (!hasProject)
        return QStringLiteral("当前没有激活的 qmake 项目（qtcreator 中未打开项目）。");

    QString text;
    text += QStringLiteral("项目：%1\n").arg(projectName);
    text += QStringLiteral("目录：%1\n").arg(projectDirectory);
    text += QStringLiteral("pro：%1\n").arg(relativeIfInside(projectFilePath, projectDirectory));
    text += QStringLiteral("头文件 %1 个，源文件 %2 个，UI %3 个，资源 %4 个\n")
                .arg(files.headers.size())
                .arg(files.sources.size())
                .arg(files.forms.size())
                .arg(files.resources.size());
    if (!files.forms.isEmpty()) {
        QStringList forms;
        for (int i = 0; i < files.forms.size() && i < 20; ++i)
            forms.append(relativeIfInside(files.forms.at(i), projectDirectory));
        text += QStringLiteral("UI 文件：%1\n").arg(forms.join(QStringLiteral(", ")));
    }
    return text;
}

// ---------------------------------------------------------------------------

ProjectService::ProjectService(QObject *parent)
    : QObject(parent)
{
    // Creator 4.15 的 Core::IEditor 没有 widget()，所以自己记住最近聚焦的代码编辑器控件
    if (QApplication *application = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        connect(application, &QApplication::focusChanged, this,
                &ProjectService::onFocusChanged);
    }
}

void ProjectService::onFocusChanged(QWidget *oldWidget, QWidget *newWidget)
{
    Q_UNUSED(oldWidget)
    if (!newWidget)
        return;

    QWidget *candidate = nullptr;
    for (QWidget *widget = newWidget; widget; widget = widget->parentWidget()) {
        if (qobject_cast<QPlainTextEdit *>(widget)) {
            candidate = widget;
            break;
        }
    }
    if (!candidate)
        return;

    // 跳过插件自己的参数编辑器（审批弹窗等），避免把 AI 对话框当成代码编辑器
    if (candidate->property("aiassistantParamEditor").toBool())
        return;

    m_lastEditorWidget = qobject_cast<QPlainTextEdit *>(candidate);
}

QPlainTextEdit *ProjectService::currentPlainTextEdit() const
{
    if (m_lastEditorWidget && m_lastEditorWidget->isVisible())
        return m_lastEditorWidget;

    if (QWidget *focus = QApplication::focusWidget()) {
        if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit *>(focus))
            return plain;
        if (QWidget *parent = focus->parentWidget()) {
            if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit *>(parent))
                return plain;
        }
    }
    return nullptr;
}

ProjectSnapshot ProjectService::snapshot() const
{
    if (!m_valid) {
        m_snapshot = const_cast<ProjectService *>(this)->collect();
        m_valid = true;
    }
    return m_snapshot;
}

void ProjectService::invalidate()
{
    m_valid = false;
}

ProjectSnapshot ProjectService::refresh()
{
    m_snapshot = collect();
    m_valid = true;
    emit projectChanged();
    return m_snapshot;
}

bool ProjectService::hasProject() const
{
    return snapshot().hasProject;
}

QString ProjectService::projectDirectory() const
{
    return snapshot().projectDirectory;
}

ProjectSnapshot ProjectService::collect()
{
    ProjectSnapshot snap;
    snap.generatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);

#ifndef AI_NO_DOCUMENT_MODEL
    const QList<Core::IDocument *> documents = Core::DocumentModel::openedDocuments();
    for (Core::IDocument *document : documents) {
        if (!document)
            continue;
        const QString path = Compat::toPathString(document->filePath());
        if (!path.isEmpty())
            snap.openDocuments.append(cleanPath(path));
    }
#endif
    if (snap.openDocuments.isEmpty()) {
        if (Core::IEditor *editor = Core::EditorManager::currentEditor()) {
            if (Core::IDocument *document = editor->document()) {
                const QString path = Compat::toPathString(document->filePath());
                if (!path.isEmpty())
                    snap.openDocuments.append(cleanPath(path));
            }
        }
    }

    ProjectExplorer::Project *project = ProjectExplorer::SessionManager::startupProject();
    if (!project)
        return snap;

    snap.hasProject = true;
    snap.projectName = project->displayName();
    snap.projectDirectory = cleanPath(Compat::toPathString(project->projectDirectory()));
    if (snap.projectDirectory.isEmpty())
        return snap;

    QDir dir(snap.projectDirectory);
    const QStringList topLevelPro = dir.entryList({QStringLiteral("*.pro")}, QDir::Files);
    for (const QString &name : topLevelPro)
        snap.proFiles.append(cleanPath(dir.absoluteFilePath(name)));

    if (snap.proFiles.isEmpty()) {   // 顶层没有 .pro：向下找一层（qmake subdirs 工程）
        const QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &sub : subDirs) {
            QDir subDir(dir.absoluteFilePath(sub));
            const QStringList names = subDir.entryList({QStringLiteral("*.pro")}, QDir::Files);
            for (const QString &name : names)
                snap.proFiles.append(cleanPath(subDir.absoluteFilePath(name)));
        }
    }

    for (const QString &proFile : snap.proFiles) {
        if (QFileInfo(proFile).completeBaseName().compare(snap.projectName, Qt::CaseInsensitive) == 0) {
            snap.projectFilePath = proFile;
            break;
        }
    }
    if (snap.projectFilePath.isEmpty() && !snap.proFiles.isEmpty())
        snap.projectFilePath = snap.proFiles.first();

    for (const QString &proFile : snap.proFiles) {
        const ProjectFileLists lists = parseProFile(proFile);
        appendUnique(&snap.files.headers, lists.headers);
        appendUnique(&snap.files.sources, lists.sources);
        appendUnique(&snap.files.forms, lists.forms);
        appendUnique(&snap.files.resources, lists.resources);
        appendUnique(&snap.files.other, lists.other);
    }

    scanDirectory(snap.projectDirectory, &snap.files);
    return snap;
}

ProjectFileLists ProjectService::parseProFile(const QString &proPath)
{
    ProjectFileLists lists;

    QFile file(proPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return lists;
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    const QString baseDir = QFileInfo(proPath).absolutePath();
    const QStringList rawLines = text.split(QLatin1Char('\n'));

    // 合并续行（行尾反斜杠）
    QStringList statements;
    QString pending;
    for (const QString &raw : rawLines) {
        QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('#')))
            continue;
        if (line.endsWith(QLatin1Char('\\'))) {
            pending += line.left(line.size() - 1) + QLatin1Char(' ');
            continue;
        }
        pending += line;
        if (!pending.trimmed().isEmpty())
            statements.append(pending);
        pending.clear();
    }
    if (!pending.trimmed().isEmpty())
        statements.append(pending);

    // qmake 赋值：SECTION = / += / *= / ~=
    static const QRegularExpression assignment(
        QStringLiteral("^\\s*(HEADERS|SOURCES|FORMS|RESOURCES|DISTFILES)\\s*[+*~]?=\\s*(.*)$"));

    for (const QString &statement : statements) {
        const QRegularExpressionMatch match = assignment.match(statement);
        if (!match.hasMatch())
            continue;

        const QString section = match.captured(1);
        QString value = match.captured(2);
        const QStringList tokens = value.split(QRegularExpression(QStringLiteral("\\s+")),
                                               AI_SKIP_EMPTY_PARTS);
        for (QString token : tokens) {
            if (token.contains(QLatin1String("$$")))   // 变量/函数引用，跳过（不写完整解析器）
                continue;
            token.remove(QLatin1Char('"'));
            token.remove(QLatin1Char('\''));
            if (token.isEmpty() || token == QLatin1String("-"))
                continue;

            QString resolved;
            if (QDir::isAbsolutePath(token))
                resolved = cleanPath(token);
            else
                resolved = cleanPath(baseDir + QLatin1Char('/') + token);

            if (section == QLatin1String("HEADERS"))
                lists.headers.append(resolved);
            else if (section == QLatin1String("SOURCES"))
                lists.sources.append(resolved);
            else if (section == QLatin1String("FORMS"))
                lists.forms.append(resolved);
            else if (section == QLatin1String("RESOURCES"))
                lists.resources.append(resolved);
            else
                lists.other.append(resolved);
        }
    }

    lists.headers.removeDuplicates();
    lists.sources.removeDuplicates();
    lists.forms.removeDuplicates();
    lists.resources.removeDuplicates();
    lists.other.removeDuplicates();
    return lists;
}

void ProjectService::scanDirectory(const QString &root, ProjectFileLists *lists, int depth)
{
    if (depth > 3)
        return;

    QDir dir(root);
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo &info : entries) {
        const QString name = info.fileName();
        if (info.isDir()) {
            if (name.startsWith(QLatin1Char('.')))
                continue;
            if (name.startsWith(QLatin1String("build"), Qt::CaseInsensitive))
                continue;
            if (name.compare(QLatin1String("debug"), Qt::CaseInsensitive) == 0
                || name.compare(QLatin1String("release"), Qt::CaseInsensitive) == 0) {
                continue;
            }
            scanDirectory(cleanPath(info.absoluteFilePath()), lists, depth + 1);
            continue;
        }

        const QString suffix = info.suffix().toLower();
        const QString path = cleanPath(info.absoluteFilePath());

        if (suffix == QLatin1String("h") || suffix == QLatin1String("hpp")
            || suffix == QLatin1String("hxx")) {
            if (!lists->headers.contains(path))
                lists->headers.append(path);
        } else if (suffix == QLatin1String("cpp") || suffix == QLatin1String("cc")
                   || suffix == QLatin1String("cxx") || suffix == QLatin1String("c")) {
            if (!lists->sources.contains(path))
                lists->sources.append(path);
        } else if (suffix == QLatin1String("ui")) {
            if (!lists->forms.contains(path))
                lists->forms.append(path);
        } else if (suffix == QLatin1String("qrc")) {
            if (!lists->resources.contains(path))
                lists->resources.append(path);
        } else if (suffix == QLatin1String("qml") || suffix == QLatin1String("js")
                   || suffix == QLatin1String("json") || suffix == QLatin1String("txt")
                   || suffix == QLatin1String("md") || suffix == QLatin1String("pri")
                   || suffix == QLatin1String("pro")) {
            if (!lists->other.contains(path))
                lists->other.append(path);
        }
    }
}

QString ProjectService::resolvePath(const QString &path, QString *error) const
{
    QString input = path.trimmed();
    if (input.size() >= 2 && input.startsWith(QLatin1Char('"')) && input.endsWith(QLatin1Char('"')))
        input = input.mid(1, input.size() - 2);
    input.replace(QLatin1Char('\\'), QLatin1Char('/'));

    if (input.isEmpty()) {
        if (error)
            *error = QStringLiteral("path 不能为空");
        return QString();
    }

    QString resolved;
    if (QDir::isAbsolutePath(input)) {
        resolved = cleanPath(input);
    } else {
        const QString base = projectDirectory();
        if (base.isEmpty()) {
            if (error)
                *error = QStringLiteral("当前没有打开的项目，相对路径无法解析，请使用绝对路径：%1").arg(input);
            return QString();
        }
        resolved = cleanPath(base + QLatin1Char('/') + input);
    }
    return resolved;
}

QString ProjectService::relativeToProject(const QString &path) const
{
    return relativeIfInside(path, projectDirectory());
}

QByteArray ProjectService::readFileBytes(const QString &path, bool *ok) const
{
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

QString ProjectService::readTextFile(const QString &path, QString *error, int maxBytes) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开文件：%1（%2）").arg(path, file.errorString());
        return QString();
    }

    const qint64 size = file.size();
    if (maxBytes > 0 && size > maxBytes) {
        if (error) {
            *error = QStringLiteral("文件过大（%1 字节，上限 %2），请使用 read_file 的分段读取或改用 grep 思路定位")
                         .arg(size)
                         .arg(maxBytes);
        }
        return QString();
    }

    QByteArray data = file.read(maxBytes > 0 ? maxBytes : size);
    file.close();
    if (data.contains('\0')) {
        if (error)
            *error = QStringLiteral("该文件看起来是二进制文件，拒绝按文本读取：%1").arg(path);
        return QString();
    }
    return QString::fromUtf8(data);
}

bool ProjectService::writeFileBytes(const QString &path, const QByteArray &data, QString *error) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("写入失败：%1（%2）").arg(path, file.errorString());
        return false;
    }
    const qint64 written = file.write(data);
    file.close();
    if (written != data.size()) {
        if (error)
            *error = QStringLiteral("写入不完整：%1").arg(path);
        return false;
    }
    return true;
}

bool ProjectService::fileExists(const QString &path) const
{
    return QFileInfo::exists(path);
}

bool ProjectService::isOpenAndModified(const QString &path) const
{
    if (Core::IDocument *document = documentForPath(path))
        return document->isModified();
    return false;
}

QString ProjectService::documentContents(const QString &path) const
{
    if (Core::IDocument *document = documentForPath(path))
        return QString::fromUtf8(document->contents());
    return QString();
}

bool ProjectService::setDocumentContents(const QString &path, const QByteArray &data, QString *error)
{
    Core::IDocument *document = documentForPath(path);
    if (!document) {
        if (error)
            *error = QStringLiteral("文件没有在编辑器中打开：%1").arg(path);
        return false;
    }
    if (!document->setContents(data)) {
        if (error)
            *error = QStringLiteral("无法更新编辑器内容：%1").arg(path);
        return false;
    }
    return true;
}

void ProjectService::notifyFilesChanged(const QStringList &paths)
{
    for (const QString &path : paths) {
        Core::IDocument *document = documentForPath(path);
        if (!document || document->isModified())
            continue;

        bool ok = false;
        const QByteArray data = readFileBytes(path, &ok);
        if (!ok)
            continue;
        if (document->contents() == data)
            continue;

        // setContents 会让编辑器视图立刻同步磁盘内容；save() 只是为了清掉 modified 标记
        document->setContents(data);
        document->save(nullptr);
    }
}

QString ProjectService::currentEditorFilePath() const
{
    Core::IEditor *editor = Core::EditorManager::currentEditor();
    if (!editor || !editor->document())
        return QString();
    return cleanPath(Compat::toPathString(editor->document()->filePath()));
}

QString ProjectService::currentEditorSelection() const
{
    QPlainTextEdit *plain = currentPlainTextEdit();
    if (!plain)
        return QString();
    QString text = plain->textCursor().selectedText();
    text.replace(QChar(0x2029), QLatin1Char('\n'));   // QTextDocument 段落分隔符
    return text;
}

bool ProjectService::insertTextAtCursor(const QString &text, QString *error)
{
    QPlainTextEdit *plain = currentPlainTextEdit();
    if (!plain) {
        if (error)
            *error = QStringLiteral("当前没有处于焦点状态的代码编辑器");
        return false;
    }
    if (plain->isReadOnly()) {
        if (error)
            *error = QStringLiteral("当前编辑器为只读，无法插入代码");
        return false;
    }
    plain->insertPlainText(text);
    return true;
}

} // namespace AiAssistant
