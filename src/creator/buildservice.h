/****************************************************************************
** 构建 / 运行 适配（Qt Creator 4.15）
**
** 4.15 与新版在这里差异最大，因此本类做两层适配：
**   1) 反射定位 ProjectExplorer::BuildManager（按类名查找），
**      再用 QMetaObject::invokeMethod("buildProject") 触发构建；
**      找不到时退化为触发 Creator 已注册的 ProjectExplorer.Build 命令。
**   2) 构建是否结束：优先连接 BuildManager 的 buildQueueFinished(bool) 信号
**      （4.15 的旧式 SIGNAL/SLOT 字符串连接），否则用“编译输出不再变化”轮询兜底。
**
** 报错采集：抓取 Creator 的“编译输出”面板（QPlainTextEdit），
** 用正则解析 MSVC / GCC-Clang 两种诊断格式，供 Agent 回填给 LLM。
****************************************************************************/

#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace AiAssistant {

class BuildService : public QObject
{
    Q_OBJECT
public:
    explicit BuildService(QObject *parent = nullptr);
    ~BuildService() override;

    bool startBuild(const QString &targetHint, QString *error);
    bool startRun(QString *error);
    bool stopRun(QString *error);

    bool isBuildInProgress() const { return m_building; }
    QJsonObject lastBuildReport() const;   // {errors[], warnings[], tail, inProgress}
    QString compileOutputText() const;
    QString applicationOutputText() const;

    static QJsonObject parseDiagnostics(const QString &text);

signals:
    void buildStarted(const QString &title);
    void buildFinished(bool success, const QString &summary);
    void runOutputCaptured(const QString &output);

private slots:
    void onBuildQueueFinished();
    void pollBuild();

private:
    void attachBuildManager();
    void finishBuild(bool success, const QString &summary);

    QObject *m_buildManager = nullptr;
    QTimer m_pollTimer;
    QElapsedTimer m_buildClock;
    bool m_building = false;
    QString m_outputBaseline;
    int m_unchangedPolls = 0;
    QElapsedTimer m_runClock;
};

} // namespace AiAssistant
