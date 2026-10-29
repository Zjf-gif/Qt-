#include "ui/approvaldialog.h"

#include "ui/diffview.h"
#include "uidom/uidocument.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

namespace AiAssistant {

namespace {

QByteArray compactJson(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString kindLabel(const QString &kind)
{
    if (kind == QLatin1String("ui"))
        return QObject::tr("UI 修改");
    if (kind == QLatin1String("code"))
        return QObject::tr("代码修改");
    if (kind == QLatin1String("ui+code"))
        return QObject::tr("UI + 代码修改");
    if (kind == QLatin1String("project"))
        return QObject::tr("工程文件修改");
    if (kind == QLatin1String("build"))
        return QObject::tr("构建");
    if (kind == QLatin1String("run"))
        return QObject::tr("运行");
    if (kind == QLatin1String("editor"))
        return QObject::tr("编辑器插入");
    return QObject::tr("修改类操作");
}

} // namespace

ApprovalDialog::ApprovalDialog(const ApprovalRequest &request, QWidget *parent)
    : QDialog(parent)
    , m_request(request)
{
    setWindowTitle(tr("AI 操作审批 — 必须人工确认"));
    setModal(true);
    resize(1020, 720);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // ---------------- 头部 ----------------
    auto *title = new QLabel(this);
    title->setTextFormat(Qt::RichText);
    title->setText(QStringLiteral("<span style=\"font-size:14pt; font-weight:600;\">%1</span>")
                       .arg(request.title.toHtmlEscaped()));
    layout->addWidget(title);

    QStringList files;
    for (const FileChange &change : request.changes) {
        const QString name = QFileInfo(change.path).fileName();
        if (!name.isEmpty() && !files.contains(name))
            files.append(name);
    }
    if (files.isEmpty()) {
        for (const ToolPlan &plan : request.plans) {
            const QString uiFile = plan.payload.value(QStringLiteral("uiFile")).toString();
            if (!uiFile.isEmpty()) {
                const QString name = QFileInfo(uiFile).fileName();
                if (!files.contains(name))
                    files.append(name);
            }
        }
    }

    auto *meta = new QLabel(this);
    meta->setWordWrap(true);
    meta->setTextFormat(Qt::RichText);
    meta->setText(QStringLiteral(
                      "<span style=\"background-color:rgba(230,160,60,0.28); padding:2px 6px;\">%1</span>"
                      "　<span style=\"color:#7a7a7a;\">%2</span>")
                      .arg(kindLabel(request.kind).toHtmlEscaped(),
                           files.isEmpty() ? tr("（不涉及文件写入）")
                                           : files.join(QStringLiteral(", ")).toHtmlEscaped()));
    layout->addWidget(meta);

    if (!request.summary.trimmed().isEmpty()) {
        auto *summary = new QLabel(request.summary, this);
        summary->setWordWrap(true);
        summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(summary);
    }

    // ---------------- 每调用一个页签 ----------------
    m_tabs = new QTabWidget(this);
    for (int i = 0; i < request.plans.size(); ++i)
        m_tabs->addTab(createPage(i), request.plans.at(i).title.trimmed().isEmpty()
                                          ? request.plans.at(i).toolName
                                          : request.plans.at(i).title.trimmed());
    layout->addWidget(m_tabs, 1);

    // ---------------- 底部 ----------------
    auto *foot = new QHBoxLayout;
    auto *hint = new QLabel(tr("审批不可关闭：写入文件 / 新建文件 / 构建 / 运行都必须在这里人工确认。"
                               "关闭窗口等于拒绝。"),
                            this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#c05050;"));
    foot->addWidget(hint, 1);

    auto *commentLabel = new QLabel(tr("备注："), this);
    m_comment = new QLineEdit(this);
    m_comment->setPlaceholderText(tr("可选，会随结果反馈给 AI"));
    m_comment->setMinimumWidth(220);
    foot->addWidget(commentLabel);
    foot->addWidget(m_comment);
    layout->addLayout(foot);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_rejectButton = new QPushButton(tr("拒绝本次操作"), this);
    m_modifyButton = new QPushButton(tr("修改指令后执行"), this);
    m_approveButton = new QPushButton(tr("同意执行"), this);

    const QList<QPushButton *> allButtons = {m_rejectButton, m_modifyButton, m_approveButton};
    for (QPushButton *button : allButtons) {
        button->setAutoDefault(false);   // 防止误按回车直接同意
        button->setDefault(false);
    }
    m_approveButton->setToolTip(tr("按当前参数立即写入本地文件（写入前会自动打快照，可回滚）"));
    m_modifyButton->setToolTip(tr("使用你编辑后的参数重新生成变更并执行"));

    buttons->addWidget(m_rejectButton);
    buttons->addWidget(m_modifyButton);
    buttons->addWidget(m_approveButton);
    layout->addLayout(buttons);

    connect(m_rejectButton, &QPushButton::clicked, this, &ApprovalDialog::rejectAll);
    connect(m_approveButton, &QPushButton::clicked, this, &ApprovalDialog::acceptApproved);
    connect(m_modifyButton, &QPushButton::clicked, this, &ApprovalDialog::acceptModified);
}

QWidget *ApprovalDialog::createPage(int index)
{
    const ToolPlan &plan = m_request.plans.at(index);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(6);

    if (!plan.summary.trimmed().isEmpty()) {
        auto *summary = new QLabel(plan.summary, page);
        summary->setWordWrap(true);
        summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(summary);
    }

    const QVector<FileChange> changes = changesForPlan(plan);
    if (!changes.isEmpty()) {
        auto *splitter = new QSplitter(Qt::Horizontal, page);

        QString beforeAll;
        QString afterAll;
        const FileChange *uiChange = nullptr;
        for (const FileChange &change : changes) {
            const QString name = QFileInfo(change.path).fileName();
            beforeAll += QStringLiteral("=== %1 ===\n").arg(name) + QString::fromUtf8(change.before)
                         + QStringLiteral("\n");
            afterAll += QStringLiteral("=== %1 ===\n").arg(name) + QString::fromUtf8(change.after)
                        + QStringLiteral("\n");
            if (!uiChange && change.path.endsWith(QLatin1String(".ui"), Qt::CaseInsensitive))
                uiChange = &change;
        }

        auto *diffView = new DiffView(splitter);
        diffView->showDiff(beforeAll, afterAll,
                           tr("变更对比（%1 个文件）").arg(changes.size()));
        splitter->addWidget(diffView);

        if (uiChange) {
            auto *preview = new UiPreviewWidget(splitter);
            QString previewError;
            if (preview->loadXml(uiChange->after, &previewError)) {
                QVector<UiPreviewWidget::Mark> marks;
                QStringList removed;
                computeUiMarks(uiChange->before, uiChange->after, &marks, &removed);
                preview->setMarks(marks);
                preview->setRemovedWidgets(removed);
            } else {
                preview->clear();
                auto *errorLabel = new QLabel(previewError, splitter);
                errorLabel->setWordWrap(true);
                errorLabel->setStyleSheet(QStringLiteral("color:#c05050;"));
                splitter->addWidget(errorLabel);
            }
            splitter->setStretchFactor(0, 3);
            splitter->setStretchFactor(1, 2);
        } else {
            splitter->setStretchFactor(0, 1);
        }
        layout->addWidget(splitter, 1);
    } else if (plan.externalAction) {
        QString note = tr("该操作会在 Creator 中立即执行，不会写入文件。");
        if (!plan.summary.trimmed().isEmpty())
            note += QStringLiteral("\n\n") + plan.summary;
        if (plan.kind == QLatin1String("build"))
            note += QStringLiteral("\n\n") + tr("构建是异步的：结束后结果会自动回填给 AI。");
        auto *label = new QLabel(note, page);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(label, 1);
    } else {
        auto *label = new QLabel(tr("（没有可展示的变更内容）"), page);
        layout->addWidget(label, 1);
    }

    auto *paramLabel = new QLabel(
        tr("工具参数（JSON，可编辑；点“同意执行 / 修改指令后执行”时按这里的内容执行）"), page);
    paramLabel->setStyleSheet(QStringLiteral("color:#7a7a7a;"));
    layout->addWidget(paramLabel);

    auto *editor = new QPlainTextEdit(page);
    // 标记：这不是代码编辑器，ProjectService 记录“最近聚焦的编辑器”时会跳过它
    editor->setProperty("aiassistantParamEditor", true);
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setPlainText(QString::fromUtf8(
                             QJsonDocument(plan.call.arguments).toJson(QJsonDocument::Indented))
                             .trimmed());
    editor->setMinimumHeight(90);
    editor->setMaximumHeight(140);
    layout->addWidget(editor);
    m_editors.append(editor);

    return page;
}

QVector<FileChange> ApprovalDialog::changesForPlan(const ToolPlan &plan) const
{
    QVector<FileChange> result;
    for (const FileChange &change : m_request.changes) {
        if (plan.changedFiles.contains(change.path))
            result.append(change);
    }
    // 单调用时展示聚合视图，避免因 changedFiles 未填而什么都看不到
    if (result.isEmpty() && m_request.plans.size() == 1 && !m_request.changes.isEmpty())
        result = m_request.changes;
    return result;
}

void ApprovalDialog::computeUiMarks(const QByteArray &before, const QByteArray &after,
                                    QVector<UiPreviewWidget::Mark> *marks,
                                    QStringList *removed) const
{
    if (!marks || !removed)
        return;

    UiDom::UiDocument beforeDoc;
    UiDom::UiDocument afterDoc;
    QString error;
    if (!beforeDoc.parse(before, &error) || !afterDoc.parse(after, &error))
        return;

    QMap<QString, UiDom::WidgetNode> beforeNodes;
    const QVector<UiDom::WidgetNode> beforeWidgets = beforeDoc.widgets();
    for (const UiDom::WidgetNode &node : beforeWidgets)
        beforeNodes.insert(node.objectName, node);

    const QString formObject = afterDoc.formObjectName();
    const QVector<UiDom::WidgetNode> afterWidgets = afterDoc.widgets();
    for (const UiDom::WidgetNode &node : afterWidgets) {
        auto it = beforeNodes.constFind(node.objectName);
        if (it == beforeNodes.constEnd()) {
            UiPreviewWidget::Mark mark;
            mark.objectName = node.objectName;
            mark.kind = UiPreviewWidget::Added;
            mark.note = node.className;
            marks->append(mark);
            continue;
        }
        if (node.objectName == formObject)
            continue;

        if (it.value().geometry != node.geometry) {
            UiPreviewWidget::Mark mark;
            mark.objectName = node.objectName;
            mark.kind = UiPreviewWidget::Moved;
            mark.note = QStringLiteral("%1,%2 %3x%4")
                            .arg(node.geometry.x())
                            .arg(node.geometry.y())
                            .arg(node.geometry.width())
                            .arg(node.geometry.height());
            marks->append(mark);
        } else if (compactJson(it.value().properties) != compactJson(node.properties)) {
            UiPreviewWidget::Mark mark;
            mark.objectName = node.objectName;
            mark.kind = UiPreviewWidget::Changed;
            marks->append(mark);
        }
    }

    for (auto it = beforeNodes.constBegin(); it != beforeNodes.constEnd(); ++it) {
        if (!afterDoc.contains(it.key()))
            removed->append(it.key());
    }
}

bool ApprovalDialog::collectCalls(QVector<ToolCall> *calls, bool *modified)
{
    if (!calls)
        return false;

    QVector<ToolCall> result;
    bool anyModified = false;

    for (int i = 0; i < m_request.calls.size(); ++i) {
        ToolCall call = m_request.calls.at(i);
        QPlainTextEdit *editor = i < m_editors.size() ? m_editors.at(i) : nullptr;
        if (editor) {
            const QString text = editor->toPlainText().trimmed();
            QJsonParseError error;
            const QJsonDocument document =
                QJsonDocument::fromJson(text.isEmpty() ? QByteArray("{}") : text.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
                QMessageBox::warning(this, tr("参数 JSON 无效"),
                                     tr("第 %1 个操作（%2）的参数不是合法 JSON 对象：\n%3")
                                         .arg(i + 1)
                                         .arg(call.name, error.errorString()));
                if (m_tabs)
                    m_tabs->setCurrentIndex(i);
                editor->setFocus();
                return false;
            }
            const QJsonObject object = document.object();
            if (compactJson(object) != compactJson(call.arguments)) {
                anyModified = true;
                call.arguments = object;
            }
        }
        result.append(call);
    }

    *calls = result;
    if (modified)
        *modified = anyModified;
    return true;
}

void ApprovalDialog::acceptApproved()
{
    QVector<ToolCall> calls;
    bool modified = false;
    if (!collectCalls(&calls, &modified))
        return;

    m_decision.kind = modified ? ApprovalDecision::Modified : ApprovalDecision::Approved;
    m_decision.calls = calls;
    m_decision.comment = m_comment ? m_comment->text().trimmed() : QString();
    accept();
}

void ApprovalDialog::acceptModified()
{
    QVector<ToolCall> calls;
    bool modified = false;
    if (!collectCalls(&calls, &modified))
        return;

    m_decision.kind = ApprovalDecision::Modified;
    m_decision.calls = calls;
    m_decision.comment = m_comment ? m_comment->text().trimmed() : QString();
    accept();
}

void ApprovalDialog::rejectAll()
{
    m_decision.kind = ApprovalDecision::Rejected;
    m_decision.calls = m_request.calls;
    m_decision.comment = m_comment ? m_comment->text().trimmed() : QString();
    reject();
}

} // namespace AiAssistant
