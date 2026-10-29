/****************************************************************************
** 设置对话框
**
** 只暴露：LLM 地址 / 密钥 / 模型 / 代理 / 超时 / 快照上限 / 上下文预算 / 历史开关。
** 没有任何“关闭审批 / 自动执行 / 免确认”之类的开关（设计上不存在该配置项）。
****************************************************************************/

#pragma once

#include "settings/agentsettings.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace AiAssistant {

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(const AgentSettings &settings, QWidget *parent = nullptr);

    AgentSettings settings() const { return m_settings; }

protected:
    void accept() override;

private:
    AgentSettings collect() const;

    AgentSettings m_settings;

    QLineEdit *m_url = nullptr;
    QLineEdit *m_key = nullptr;
    QLineEdit *m_model = nullptr;
    QLineEdit *m_proxy = nullptr;
    QSpinBox *m_timeout = nullptr;
    QSpinBox *m_maxSnapshots = nullptr;
    QSpinBox *m_maxIterations = nullptr;
    QSpinBox *m_charBudget = nullptr;
    QDoubleSpinBox *m_temperature = nullptr;
    QCheckBox *m_persistHistory = nullptr;
    QCheckBox *m_showKey = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace AiAssistant
