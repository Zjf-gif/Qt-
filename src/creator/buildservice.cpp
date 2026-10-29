#include "creator/buildservice.h"

#include "creator/creatorcompat.h"

#include <projectexplorer/project.h>
#include <projectexplorer/session.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QMetaMethod>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QWidget>

namespace AiAssistant {

namespace {

const int kMaxDiagnostics = 60;
const int kMaxTailLines = 40;
const int kBuildTimeoutMs = 20 * 60 * 1000;   // 20 分钟硬超时

QStringList widgetChain(QWidget *widget)
{
    QStringList chain;
    for (QWidget *w = widget; w; w = w->parentWidget()) {
        if (!w->objectName().isEmpty())
            chain.append(w->objectName());
        chain.append(QString::fromLatin1(w->metaObject()->className()));
    }
    return chain;
}

QPlainTextEdit *findOutputPane(const QStringList &hints)
{
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets) {
        QPlainTextEdit *plain = qobject_cast<QPlainTextEdit *>(widget);
        if (!plain)
            continue;
        const QStringList chain = widgetChain(widget);
        for (const QString &name : chain) {
            for (const QString &hint : hints) {
                if (name.contains(hint, Qt::CaseInsensitive))
                    return plain;
            }
        }
    }
    return nullptr;
}

QString tailOf(const QString &text, int lineCount)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    const int start = qMax(0, lines.size() - lineCount);
    return lines.mid(start).join(QLatin1Char('\n'));
}

} // namespace

BuildService::BuildService(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(800);
    connect(&m_pollTimer, &QTimer::timeout, this, &BuildService::pollBuild);
    attachBuildManager();
}

BuildService::~BuildService() = default;

void BuildService::attachBuildManager()
{
    if (m_buildManager)
        return;

    const QStringList names = {QStringLiteral("ProjectExplorer::BuildManager"),
                               QStringLiteral("BuildManager")};
    m_buildManager = Compat::findObjectByClassName(names);
    if (!m_buildManager)
        return;

    // 4.15 的旧式字符串连接：不依赖头文件里的 C++ 签名，规避版本差异
    if (m_buildManager->metaObject()->indexOfSignal("buildQueueFinished(bool)") >= 0)
        connect(m_buildManager, SIGNAL(buildQueueFinished(bool)), this, SLOT(onBuildQueueFinished()));
}

QString BuildService::compileOutputText() const
{
    static const QStringList hints = {QStringLiteral("compileoutput"), QStringLiteral("CompileOutput"),
                                      QStringLiteral("compileroutput"), QStringLiteral("BuildOutput")};
    if (QPlainTextEdit *pane = findOutputPane(hints))
        return pane->toPlainText();
    return QString();
}

QString BuildService::applicationOutputText() const
{
    static const QStringList hints = {QStringLiteral("applicationoutput"),
                                      QStringLiteral("ApplicationOutput"),
                                      QStringLiteral("appoutput")};
    if (QPlainTextEdit *pane = findOutputPane(hints))
        return pane->toPlainText();
    return QString();
}

QJsonObject BuildService::parseDiagnostics(const QString &text)
{
    QStringList errors;
    QStringList warnings;

    static const QRegularExpression msvcPattern(QStringLiteral(
        "^\\s*(.+?)\\((\\d+)(?:,\\d+)?\\)\\s*:\\s*(fatal error|error|warning)\\s*([A-Za-z]+\\d+)?\\s*:\\s*(.*)$"));
    static const QRegularExpression gccPattern(QStringLiteral(
        "^\\s*(.+?):(\\d+)(?::\\d+)?\\s*:\\s*(fatal error|error|warning)\\s*:\\s*(.*)$"));

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        QString kind;
        QString entry;

        QRegularExpressionMatch match = msvcPattern.match(line);
        if (match.hasMatch()) {
            kind = match.captured(3);
            entry = QStringLiteral("%1:%2: %3 %4")
                        .arg(match.captured(1), match.captured(2), kind, match.captured(5))
                        .trimmed();
            if (!match.captured(4).isEmpty())
                entry = QStringLiteral("%1 [%2]").arg(entry, match.captured(4));
        } else {
            match = gccPattern.match(line);
            if (!match.hasMatch())
                continue;
            kind = match.captured(3);
            entry = QStringLiteral("%1:%2: %3: %4")
                        .arg(match.captured(1), match.captured(2), kind, match.captured(4))
                        .trimmed();
        }

        if (kind.contains(QLatin1String("warning"))) {
            if (warnings.size() < kMaxDiagnostics)
                warnings.append(entry);
        } else {
            if (errors.size() < kMaxDiagnostics)
                errors.append(entry);
        }
    }

    QJsonObject report;
    report.insert(QStringLiteral("errors"), QJsonArray::fromStringList(errors));
    report.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
    report.insert(QStringLiteral("errorCount"), errors.size());
    report.insert(QStringLiteral("warningCount"), warnings.size());
    return report;
}

QJsonObject BuildService::lastBuildReport() const
{
    const QString output = compileOutputText();
    QJsonObject report = parseDiagnostics(output);
    report.insert(QStringLiteral("tail"), tailOf(output, kMaxTailLines));
    report.insert(QStringLiteral("inProgress"), m_building);
    report.insert(QStringLiteral("outputLength"), output.size());
    if (output.isEmpty())
        report.insert(QStringLiteral("note"), QStringLiteral("未找到“编译输出”面板或面板为空，请先在 Creator 中执行一次构建"));
    return report;
}

bool BuildService::startBuild(const QString &targetHint, QString *error)
{
    if (m_building) {
        if (error)
            *error = QStringLiteral("已有一个构建正在进行中，请等待其结束");
        return false;
    }

    ProjectExplorer::Project *project = ProjectExplorer::SessionManager::startupProject();
    if (!project) {
        if (error)
            *error = QStringLiteral("当前没有启动项目，无法构建");
        return false;
    }

    attachBuildManager();

    bool triggered = false;
    if (m_buildManager) {
        // 反射调用：不同版本签名不一致时只会返回 false，不会编译失败或崩溃
        triggered = QMetaObject::invokeMethod(m_buildManager, "buildProject", Qt::DirectConnection,
                                              Q_ARG(ProjectExplorer::Project*, project));
    }
    if (!triggered)
        triggered = Compat::triggerCommand(Compat::buildCommandCandidates());

    if (!triggered) {
        if (error) {
            *error = QStringLiteral("无法触发构建：既没有找到可用的 BuildManager::buildProject，"
                                    "也没有找到 ProjectExplorer.Build 命令。"
                                    "请在 creator/creatorcompat.cpp 中补充本机 Creator 的命令 id");
        }
        return false;
    }

    const QString title = targetHint.trimmed().isEmpty() ? project->displayName() : targetHint.trimmed();
    m_building = true;
    m_outputBaseline = compileOutputText();
    m_unchangedPolls = 0;
    m_buildClock.start();
    m_pollTimer.start();

    emit buildStarted(title);
    return true;
}

void BuildService::pollBuild()
{
    if (!m_building)
        return;

    const QString current = compileOutputText();
    if (current == m_outputBaseline) {
        ++m_unchangedPolls;
    } else {
        m_outputBaseline = current;
        m_unchangedPolls = 0;
    }

    // 输出 3 次轮询（约 2.4 秒）不再变化 → 认为构建结束（无信号可连时的兜底）
    const bool stalled = m_unchangedPolls >= 3 && m_buildClock.elapsed() > 2000;
    const bool timedOut = m_buildClock.elapsed() > kBuildTimeoutMs;
    if (!stalled && !timedOut)
        return;

    const QJsonObject report = parseDiagnostics(current);
    const int errorCount = report.value(QStringLiteral("errorCount")).toInt();
    if (timedOut) {
        finishBuild(false, QStringLiteral("构建超时（超过 20 分钟），已停止等待"));
        return;
    }
    finishBuild(errorCount == 0, QStringLiteral("构建结束：%1 个错误，%2 个警告")
                                    .arg(errorCount)
                                    .arg(report.value(QStringLiteral("warningCount")).toInt()));
}

void BuildService::onBuildQueueFinished()
{
    if (!m_building)
        return;
    pollBuild();
    // 若输出尚未刷新完成，再给一次机会
    if (m_building)
        QTimer::singleShot(600, this, &BuildService::pollBuild);
}

void BuildService::finishBuild(bool success, const QString &summary)
{
    if (!m_building)
        return;
    m_building = false;
    m_pollTimer.stop();
    emit buildFinished(success, summary);
}

bool BuildService::startRun(QString *error)
{
    if (!Compat::triggerCommand(Compat::runCommandCandidates())) {
        if (error) {
            *error = QStringLiteral("无法触发运行：未找到 ProjectExplorer.Run 命令。"
                                    "请在 creator/creatorcompat.cpp 中补充本机 Creator 的命令 id");
        }
        return false;
    }

    m_runClock.start();
    // 应用输出面板在启动后才有内容，延迟抓取一次
    QTimer::singleShot(2500, this, [this] {
        emit runOutputCaptured(applicationOutputText());
    });
    return true;
}

bool BuildService::stopRun(QString *error)
{
    if (Compat::triggerCommand(Compat::stopCommandCandidates()))
        return true;
    if (error)
        *error = QStringLiteral("未找到停止运行的命令（ProjectExplorer.Stop）");
    return false;
}

} // namespace AiAssistant
