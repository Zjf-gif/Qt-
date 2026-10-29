#include "ui/settingsdialog.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

namespace AiAssistant {

SettingsDialog::SettingsDialog(const AgentSettings &settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("AI 助手设置"));
    setModal(true);
    resize(620, 480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("LLM 连接"), this);
    title->setStyleSheet(QStringLiteral("font-weight:600;"));
    layout->addWidget(title);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);

    m_url = new QLineEdit(m_settings.baseUrl, this);
    m_url->setPlaceholderText(QStringLiteral("http://127.0.0.1:11434/v1/chat/completions"));
    form->addRow(tr("LLM 地址"), m_url);

    m_key = new QLineEdit(m_settings.apiKey, this);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(tr("留空表示本地模型无需密钥"));
    m_showKey = new QCheckBox(tr("显示"), this);
    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(m_key, 1);
    keyRow->addWidget(m_showKey);
    form->addRow(tr("API Key"), keyRow);

    m_model = new QLineEdit(m_settings.model, this);
    form->addRow(tr("模型名"), m_model);

    m_proxy = new QLineEdit(m_settings.proxyUrl, this);
    m_proxy->setPlaceholderText(tr("留空使用系统代理，例如 http://127.0.0.1:7890"));
    form->addRow(tr("代理"), m_proxy);

    m_timeout = new QSpinBox(this);
    m_timeout->setRange(5, 3600);
    m_timeout->setSuffix(tr(" 秒"));
    m_timeout->setValue(m_settings.requestTimeoutSec);
    form->addRow(tr("请求超时"), m_timeout);

    m_temperature = new QDoubleSpinBox(this);
    m_temperature->setRange(0.0, 2.0);
    m_temperature->setSingleStep(0.1);
    m_temperature->setDecimals(2);
    m_temperature->setValue(m_settings.temperature);
    form->addRow(tr("temperature"), m_temperature);

    auto *snapshotTitle = new QLabel(tr("快照与上下文"), this);
    snapshotTitle->setStyleSheet(QStringLiteral("font-weight:600; margin-top:6px;"));
    layout->addLayout(form);
    layout->addWidget(snapshotTitle);

    auto *form2 = new QFormLayout;
    form2->setLabelAlignment(Qt::AlignRight);

    m_maxSnapshots = new QSpinBox(this);
    m_maxSnapshots->setRange(1, 5000);
    m_maxSnapshots->setValue(m_settings.maxSnapshots);
    m_maxSnapshots->setToolTip(tr("超出上限时丢弃最早的快照批次"));
    form2->addRow(tr("快照上限"), m_maxSnapshots);

    m_maxIterations = new QSpinBox(this);
    m_maxIterations->setRange(1, 200);
    m_maxIterations->setValue(m_settings.maxToolIterations);
    m_maxIterations->setToolTip(tr("单轮对话中工具调用的最大轮次，防止模型死循环"));
    form2->addRow(tr("工具轮次上限"), m_maxIterations);

    m_charBudget = new QSpinBox(this);
    m_charBudget->setRange(4000, 2000000);
    m_charBudget->setSingleStep(2000);
    m_charBudget->setValue(m_settings.contextCharBudget);
    m_charBudget->setToolTip(tr("超过该字符数时按轮次丢弃最早对话，降低 token 消耗"));
    form2->addRow(tr("上下文预算"), m_charBudget);

    m_persistHistory = new QCheckBox(tr("按项目保存对话历史（关闭则不留痕）"), this);
    m_persistHistory->setChecked(m_settings.persistHistory);
    form2->addRow(QString(), m_persistHistory);

    layout->addLayout(form2);

    auto *security = new QLabel(
        tr("安全说明：本插件不存在“关闭审批 / 自动执行”的配置项。所有修改类操作（UI、代码、"
           "项目文件、构建、运行）都必须经过人工审批，该行为在代码中硬编码。"),
        this);
    security->setWordWrap(true);
    security->setStyleSheet(QStringLiteral("color:#c05050; margin-top:8px;"));
    layout->addWidget(security);

    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color:#c05050;"));
    layout->addWidget(m_status);

    layout->addStretch(1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), this);
    cancel->setAutoDefault(false);
    auto *ok = new QPushButton(tr("保存"), this);
    ok->setAutoDefault(false);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &SettingsDialog::accept);
    connect(m_showKey, &QCheckBox::toggled, this, [this](bool checked) {
        m_key->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
    });
}

AgentSettings SettingsDialog::collect() const
{
    AgentSettings settings = m_settings;
    settings.baseUrl = m_url->text().trimmed();
    settings.apiKey = m_key->text().trimmed();
    settings.model = m_model->text().trimmed();
    settings.proxyUrl = m_proxy->text().trimmed();
    settings.requestTimeoutSec = m_timeout->value();
    settings.temperature = m_temperature->value();
    settings.maxSnapshots = m_maxSnapshots->value();
    settings.maxToolIterations = m_maxIterations->value();
    settings.contextCharBudget = m_charBudget->value();
    settings.persistHistory = m_persistHistory->isChecked();
    return settings;
}

void SettingsDialog::accept()
{
    const AgentSettings settings = collect();
    if (settings.baseUrl.isEmpty()) {
        m_status->setText(tr("LLM 地址不能为空"));
        return;
    }
    const QUrl url(settings.endpoint());
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        m_status->setText(tr("LLM 地址无效：%1").arg(settings.endpoint()));
        return;
    }
    if (settings.model.isEmpty()) {
        m_status->setText(tr("模型名不能为空"));
        return;
    }
    m_settings = settings;
    QDialog::accept();
}

} // namespace AiAssistant
