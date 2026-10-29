#include "tools/codetools.h"

#include "creator/projectservice.h"
#include "tools/proeditor.h"
#include "tools/toolregistry.h"
#include "tools/toolschema.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>

namespace AiAssistant {

namespace {

QString argText(const ToolCall &call, const char *key)
{
    return call.arguments.value(QLatin1String(key)).toString();
}

QString shorten(const QString &text, int maxChars = 60)
{
    QString flat = text;
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (flat.size() > maxChars)
        flat = flat.left(maxChars) + QStringLiteral("…");
    return flat;
}

ToolPlan failure(const QString &error)
{
    ToolPlan plan;
    plan.ok = false;
    plan.error = error;
    return plan;
}

// ---------------------------------------------------------------------------
// unified diff
// ---------------------------------------------------------------------------
struct HunkLine
{
    QChar type;   // ' ', '-', '+'
    QString text;
};

struct Hunk
{
    int oldStart = 0;
    int newStart = 0;
    QVector<HunkLine> lines;
};

bool applyUnifiedDiff(const QString &base, const QString &diff, QString *result, QString *error)
{
    static const QRegularExpression header(
        QStringLiteral("^@@\\s+-([0-9]+)(?:,([0-9]+))?\\s+\\+([0-9]+)(?:,([0-9]+))?\\s+@@"));

    QVector<Hunk> hunks;
    Hunk current;
    bool inHunk = false;

    const QStringList diffLines = diff.split(QLatin1Char('\n'));
    for (const QString &rawLine : diffLines) {
        const QRegularExpressionMatch match = header.match(rawLine);
        if (match.hasMatch()) {
            if (inHunk)
                hunks.append(current);
            current = Hunk();
            current.oldStart = match.captured(1).toInt();
            current.newStart = match.captured(3).toInt();
            inHunk = true;
            continue;
        }
        if (!inHunk)
            continue;   // 跳过 --- / +++ / diff --git / index 等头信息

        if (rawLine.startsWith(QLatin1String("\\ No newline")))
            continue;

        if (rawLine.isEmpty()) {
            HunkLine line;
            line.type = QLatin1Char(' ');
            line.text = QString();
            current.lines.append(line);
            continue;
        }

        const QChar type = rawLine.at(0);
        if (type != QLatin1Char('+') && type != QLatin1Char('-') && type != QLatin1Char(' '))
            continue;   // 容错：忽略无法识别的行

        HunkLine line;
        line.type = type;
        line.text = rawLine.mid(1);
        current.lines.append(line);
    }
    if (inHunk)
        hunks.append(current);

    if (hunks.isEmpty()) {
        if (error)
            *error = QStringLiteral("未在 diff 中找到 \"@@ -a,b +c,d @@\" 段落");
        return false;
    }

    const QStringList baseLines = base.split(QLatin1Char('\n'));
    QStringList output;
    int oldIndex = 0;

    for (const Hunk &hunk : hunks) {
        const int targetOld = qMax(0, hunk.oldStart - 1);
        if (targetOld < oldIndex) {
            if (error)
                *error = QStringLiteral("diff 段落顺序异常（oldStart 回退）");
            return false;
        }
        while (oldIndex < targetOld && oldIndex < baseLines.size())
            output.append(baseLines.at(oldIndex++));

        int cursor = oldIndex;
        for (const HunkLine &line : hunk.lines) {
            if (line.type == QLatin1Char('+')) {
                output.append(line.text);
                continue;
            }

            if (cursor >= baseLines.size()) {
                if (error)
                    *error = QStringLiteral("diff 上下文超出文件末尾（期望 \"%1\"）").arg(shorten(line.text));
                return false;
            }

            const QString actual = baseLines.at(cursor);
            if (actual != line.text && actual.trimmed() != line.text.trimmed()) {
                if (error) {
                    *error = QStringLiteral("diff 上下文不匹配：第 %1 行期望 \"%2\"，实际 \"%3\"")
                                 .arg(cursor + 1)
                                 .arg(shorten(line.text), shorten(actual));
                }
                return false;
            }

            if (line.type == QLatin1Char(' '))
                output.append(actual);
            // '-' 行只消耗原文件行，不输出
            ++cursor;
        }
        oldIndex = cursor;
    }

    while (oldIndex < baseLines.size())
        output.append(baseLines.at(oldIndex++));

    *result = output.join(QLatin1Char('\n'));
    return true;
}

// ---------------------------------------------------------------------------
// edits 数组
// ---------------------------------------------------------------------------
bool applyEdits(const QString &base, const QJsonArray &edits, QString *result, QString *error)
{
    QString text = base;
    for (const QJsonValue &entry : edits) {
        const QJsonObject object = entry.toObject();
        QString oldText = object.value(QStringLiteral("oldText")).toString();
        if (oldText.isEmpty())
            oldText = object.value(QStringLiteral("old_text")).toString();
        if (oldText.isEmpty())
            oldText = object.value(QStringLiteral("old")).toString();
        QString newText = object.value(QStringLiteral("newText")).toString();
        if (newText.isEmpty())
            newText = object.value(QStringLiteral("new_text")).toString();
        if (newText.isEmpty())
            newText = object.value(QStringLiteral("new")).toString();
        const bool replaceAll = object.value(QStringLiteral("all")).toBool(false);

        if (oldText.isEmpty()) {
            if (error)
                *error = QStringLiteral("edits[] 中的 oldText 不能为空");
            return false;
        }
        const int index = text.indexOf(oldText);
        if (index < 0) {
            if (error) {
                *error = QStringLiteral("未找到待替换片段（可能已被修改过）：\"%1\"")
                             .arg(shorten(oldText, 80));
            }
            return false;
        }

        if (replaceAll)
            text.replace(oldText, newText);
        else
            text.replace(index, oldText.size(), newText);
    }
    *result = text;
    return true;
}

// ---------------------------------------------------------------------------
// 类模板
// ---------------------------------------------------------------------------
QString headerTemplate(const QString &className, const QString &baseClass, const QString &nameSpace)
{
    const QString guard = className.toUpper() + QStringLiteral("_H");
    QString text;
    text += QStringLiteral("#ifndef %1\n#define %1\n\n").arg(guard);
    text += QStringLiteral("#include <%1>\n\n").arg(baseClass);
    if (!nameSpace.isEmpty())
        text += QStringLiteral("namespace %1 {\n\n").arg(nameSpace);
    text += QStringLiteral("class %1 : public %2\n{\n    Q_OBJECT\n\npublic:\n").arg(className, baseClass);
    text += QStringLiteral("    explicit %1(QObject *parent = nullptr);\n").arg(className);
    text += QStringLiteral("    ~%1() override;\n\nprivate:\n};\n").arg(className);
    if (!nameSpace.isEmpty())
        text += QStringLiteral("\n} // namespace %1\n").arg(nameSpace);
    text += QStringLiteral("\n#endif // %1\n").arg(guard);
    return text;
}

QString sourceTemplate(const QString &className, const QString &baseClass, const QString &nameSpace)
{
    QString text;
    text += QStringLiteral("#include \"%1.h\"\n\n").arg(className.toLower());
    if (!nameSpace.isEmpty())
        text += QStringLiteral("namespace %1 {\n\n").arg(nameSpace);
    text += QStringLiteral("%1::%1(QObject *parent)\n    : %2(parent)\n{\n}\n\n")
                .arg(className, baseClass);
    text += QStringLiteral("%1::~%1() = default;\n").arg(className);
    if (!nameSpace.isEmpty())
        text += QStringLiteral("\n} // namespace %1\n").arg(nameSpace);
    return text;
}

} // namespace

CodeTools::CodeTools(ToolRuntime *runtime, QObject *parent)
    : QObject(parent)
    , m_runtime(runtime)
{
}

void CodeTools::registerTools(ToolRegistry *registry)
{
    if (!registry)
        return;

    ToolRuntime *runtime = m_runtime;

    // ---------------------------------------------------------------- write_file
    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("write_file"),
            QStringLiteral("全量写入文本文件（兜底方案；能局部替换请优先用 patch_file）。"
                           "父目录不存在会自动创建。必须人工审批。"),
            Schema::object(
                {{QStringLiteral("path"), Schema::prop(QStringLiteral("string"), QStringLiteral("文件路径"))},
                 {QStringLiteral("content"), Schema::prop(QStringLiteral("string"), QStringLiteral("完整文件内容"))}},
                {QStringLiteral("path"), QStringLiteral("content")}),
            false),
        [runtime](const ToolCall &call, PrepareContext &context) -> ToolPlan {
            ToolPlan plan;
            plan.kind = QStringLiteral("code");

            const QString path = argText(call, "path").trimmed();
            if (path.isEmpty())
                return failure(QStringLiteral("path 不能为空"));
            if (!call.arguments.contains(QStringLiteral("content")))
                return failure(QStringLiteral("content 不能为空"));

            QString error;
            const QString resolved = runtime->project->resolvePath(path, &error);
            if (resolved.isEmpty())
                return failure(error);
            if (runtime->project->isOpenAndModified(resolved)) {
                return failure(QStringLiteral("该文件在 Creator 中已打开且有未保存的修改。"
                                              "全量覆盖会丢掉这些改动，请先保存或用 patch_file 做局部替换"));
            }

            const QString content = argText(call, "content");
            const bool existedBefore = context.exists(resolved);
            context.writeBytes(resolved, content.toUtf8());
            plan.changedFiles.append(resolved);

            plan.title = QStringLiteral("写入文件 %1").arg(QFileInfo(resolved).fileName());
            plan.summary = QStringLiteral("%1：全量写入 %2 字节%3")
                               .arg(runtime->project->relativeToProject(resolved))
                               .arg(content.toUtf8().size())
                               .arg(existedBefore ? QStringLiteral("（覆盖已有文件）") : QStringLiteral("（新建文件）"));
            return plan;
        });

    // ---------------------------------------------------------------- patch_file
    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("patch_file"),
            QStringLiteral("对源码做增量修改。两种用法：1) diff 传 unified diff（含 @@ 段落）；"
                           "2) edits 传 [{\"oldText\":\"...\",\"newText\":\"...\"}] 做精确文本替换。"
                           "优先使用本工具而不是 write_file。必须人工审批。"),
            Schema::object(
                {{QStringLiteral("path"), Schema::prop(QStringLiteral("string"), QStringLiteral("文件路径"))},
                 {QStringLiteral("diff"), Schema::prop(QStringLiteral("string"), QStringLiteral("unified diff 文本（可选）"))},
                 {QStringLiteral("edits"), Schema::prop(QStringLiteral("array"), QStringLiteral("精确替换列表（可选）"))}},
                {QStringLiteral("path")}),
            false),
        [runtime](const ToolCall &call, PrepareContext &context) -> ToolPlan {
            ToolPlan plan;
            plan.kind = QStringLiteral("code");

            const QString path = argText(call, "path").trimmed();
            if (path.isEmpty())
                return failure(QStringLiteral("path 不能为空"));

            QString error;
            const QString resolved = runtime->project->resolvePath(path, &error);
            if (resolved.isEmpty())
                return failure(error);
            if (!context.exists(resolved))
                return failure(QStringLiteral("文件不存在：%1（新建文件请用 write_file）").arg(resolved));
            if (runtime->project->isOpenAndModified(resolved)) {
                return failure(QStringLiteral("该文件有未保存修改，请先保存后再让 AI 修改（避免覆盖你的改动）"));
            }

            bool exists = false;
            const QByteArray beforeBytes = context.readBytes(resolved, &exists, &error);
            if (!exists)
                return failure(error.isEmpty() ? QStringLiteral("无法读取 %1").arg(resolved) : error);
            const QString before = QString::fromUtf8(beforeBytes);

            QString after;
            const QJsonArray edits = call.arguments.value(QStringLiteral("edits")).toArray();
            const QString diff = argText(call, "diff");
            if (!edits.isEmpty()) {
                if (!applyEdits(before, edits, &after, &error))
                    return failure(error);
            } else if (!diff.trimmed().isEmpty()) {
                if (!applyUnifiedDiff(before, diff, &after, &error))
                    return failure(error);
            } else {
                return failure(QStringLiteral("需要提供 diff 或 edits；整文件覆盖请用 write_file"));
            }

            if (after == before)
                return failure(QStringLiteral("补丁没有产生任何改变"));

            context.writeBytes(resolved, after.toUtf8());
            plan.changedFiles.append(resolved);

            plan.title = QStringLiteral("增量修改 %1").arg(QFileInfo(resolved).fileName());
            plan.summary = QStringLiteral("%1：%2 → %3 字节")
                               .arg(runtime->project->relativeToProject(resolved))
                               .arg(before.toUtf8().size())
                               .arg(after.toUtf8().size());
            return plan;
        });

    // ---------------------------------------------------------------- add_qt_class
    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("add_qt_class"),
            QStringLiteral("新建一个 Qt5 类（生成 .h/.cpp，含 Q_OBJECT），并自动追加到 .pro 的 HEADERS/SOURCES。"
                           "path 可以是目录，也可以是 .h/.cpp 路径。必须人工审批。"),
            Schema::object(
                {{QStringLiteral("class_name"), Schema::prop(QStringLiteral("string"), QStringLiteral("类名，如 MyWidget"))},
                 {QStringLiteral("path"), Schema::prop(QStringLiteral("string"), QStringLiteral("目标目录或文件路径，相对项目目录"))},
                 {QStringLiteral("base_class"), Schema::prop(QStringLiteral("string"), QStringLiteral("基类，默认 QObject"))},
                 {QStringLiteral("namespace"), Schema::prop(QStringLiteral("string"), QStringLiteral("命名空间（可选）"))}},
                {QStringLiteral("class_name"), QStringLiteral("path")}),
            false),
        [runtime](const ToolCall &call, PrepareContext &context) -> ToolPlan {
            ToolPlan plan;
            plan.kind = QStringLiteral("project");

            const QString className = argText(call, "class_name").trimmed();
            QString baseClass = argText(call, "base_class").trimmed();
            if (baseClass.isEmpty())
                baseClass = QStringLiteral("QObject");
            const QString nameSpace = argText(call, "namespace").trimmed();

            static const QRegularExpression classNamePattern(
                QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
            if (!classNamePattern.match(className).hasMatch())
                return failure(QStringLiteral("class_name 非法：%1").arg(className));

            QString path = argText(call, "path").trimmed();
            if (path.isEmpty())
                return failure(QStringLiteral("path 不能为空"));

            QString error;
            QString resolved = runtime->project->resolvePath(path, &error);
            if (resolved.isEmpty())
                return failure(error);

            QFileInfo info(resolved);
            QString baseDir;
            if (info.suffix().compare(QLatin1String("h"), Qt::CaseInsensitive) == 0
                || info.suffix().compare(QLatin1String("cpp"), Qt::CaseInsensitive) == 0) {
                baseDir = info.absolutePath();
            } else {
                baseDir = resolved;
            }
            const QString headerPath = QDir(baseDir).absoluteFilePath(className + QStringLiteral(".h"));
            const QString sourcePath = QDir(baseDir).absoluteFilePath(className + QStringLiteral(".cpp"));

            if (context.exists(headerPath) || context.exists(sourcePath))
                return failure(QStringLiteral("目标文件已存在：%1").arg(headerPath));

            context.writeBytes(headerPath, headerTemplate(className, baseClass, nameSpace).toUtf8());
            context.writeBytes(sourcePath, sourceTemplate(className, baseClass, nameSpace).toUtf8());
            plan.changedFiles << headerPath << sourcePath;

            bool proChanged = false;
            QString proPath;
            if (runtime && runtime->project) {
                const ProjectSnapshot snapshot = runtime->project->snapshot();
                if (!snapshot.projectFilePath.isEmpty()) {
                    bool exists = false;
                    QString readError;
                    const QByteArray proContent =
                        context.readBytes(snapshot.projectFilePath, &exists, &readError);
                    if (exists) {
                        bool presentHeader = false;
                        bool presentSource = false;
                        QByteArray updated = ProFileEditor::appendEntry(
                            proContent, QStringLiteral("HEADERS"),
                            ProFileEditor::relativeForPro(snapshot.projectFilePath, headerPath),
                            &presentHeader);
                        updated = ProFileEditor::appendEntry(
                            updated, QStringLiteral("SOURCES"),
                            ProFileEditor::relativeForPro(snapshot.projectFilePath, sourcePath),
                            &presentSource);
                        if (!presentHeader || !presentSource) {
                            context.writeBytes(snapshot.projectFilePath, updated);
                            proChanged = true;
                            proPath = snapshot.projectFilePath;
                        }
                    }
                }
            }
            if (proChanged)
                plan.changedFiles.append(proPath);

            plan.title = QStringLiteral("新建类 %1").arg(className);
            plan.summary = QStringLiteral("生成 %1.h / %1.cpp（基类 %2）→ %3%4")
                               .arg(className, baseClass,
                                    runtime->project->relativeToProject(baseDir),
                                    proChanged ? QStringLiteral("，并更新 .pro 的 HEADERS/SOURCES")
                                               : QString());
            return plan;
        });

    // ---------------------------------------------------------------- insert_code_at_cursor
    registry->addWriteTool(
        ToolRegistry::makeSpec(
            QStringLiteral("insert_code_at_cursor"),
            QStringLiteral("在当前代码编辑器光标处插入代码（等价于用户手敲）。"
                           "快照会记录插入前的编辑器内容，可用“回滚”恢复。必须人工审批。"),
            Schema::object(
                {{QStringLiteral("code"), Schema::prop(QStringLiteral("string"), QStringLiteral("要插入的代码"))}},
                {QStringLiteral("code")}),
            false),
        [runtime](const ToolCall &call, PrepareContext &) -> ToolPlan {
            ToolPlan plan;
            plan.kind = QStringLiteral("editor");
            plan.externalAction = true;

            const QString code = argText(call, "code");
            if (code.trimmed().isEmpty())
                return failure(QStringLiteral("code 不能为空"));

            const QString filePath = runtime->project->currentEditorFilePath();
            if (filePath.isEmpty())
                return failure(QStringLiteral("当前没有打开的代码编辑器，请先在 Creator 中打开目标文件"));

            plan.payload.insert(QStringLiteral("code"), code);
            plan.payload.insert(QStringLiteral("filePath"), filePath);
            plan.title = QStringLiteral("光标处插入代码");
            plan.summary = QStringLiteral("%1：插入 %2 行代码")
                               .arg(runtime->project->relativeToProject(filePath))
                               .arg(code.count(QLatin1Char('\n')) + 1);
            return plan;
        });
}

} // namespace AiAssistant
