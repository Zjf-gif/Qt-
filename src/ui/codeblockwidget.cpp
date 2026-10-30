#include "ui/codeblockwidget.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

namespace AiAssistant {

namespace {
const int kMaxVisibleLines = 16;   // 超过后卡片内部滚动
}

CodeBlockWidget::CodeBlockWidget(QWidget *parent)
    : QFrame(parent)
{
    // 外框：用 QFrame 的 StyledPanel，不依赖 stylesheet
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, palette().color(QPalette::Base));
    setPalette(pal);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---- 顶部条 ----
    auto *header = new QWidget(this);
    header->setAutoFillBackground(true);
    {
        QPalette headerPal = header->palette();
        const QColor base = palette().color(QPalette::Base);
        headerPal.setColor(QPalette::Window,
                           (base.lightness() > 128) ? base.darker(108) : base.lighter(130));
        header->setPalette(headerPal);
    }
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 3, 5, 3);
    headerLayout->setSpacing(6);

    m_languageLabel = new QLabel(QStringLiteral("code"), header);
    {
        QPalette labelPal = m_languageLabel->palette();
        labelPal.setColor(QPalette::WindowText, palette().color(QPalette::Text));
        m_languageLabel->setPalette(labelPal);
    }
    QFont small = m_languageLabel->font();
    small.setPointSizeF(qMax(7.5, small.pointSizeF() - 1.0));
    m_languageLabel->setFont(small);

    m_copyButton = new QPushButton(tr("复制"), header);
    m_copyButton->setAutoDefault(false);
    m_copyButton->setFlat(true);
    m_copyButton->setCursor(Qt::PointingHandCursor);
    m_copyButton->setToolTip(tr("复制这段代码"));
    m_copyButton->setFont(small);
    m_copyButton->setFixedHeight(20);

    headerLayout->addWidget(m_languageLabel, 1);
    headerLayout->addWidget(m_copyButton, 0, Qt::AlignRight);
    layout->addWidget(header);

    // ---- 代码区 ----
    m_edit = new QPlainTextEdit(this);
    m_edit->setReadOnly(true);
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_edit->setFrameShape(QFrame::NoFrame);
    m_edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_edit->document()->setDocumentMargin(8);
    m_edit->setTabStopDistance(
        QFontMetricsF(m_edit->font()).horizontalAdvance(QLatin1Char(' ')) * 4);
    m_edit->setFocusPolicy(Qt::ClickFocus);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(qMax(8.5, mono.pointSizeF()));
    m_edit->setFont(mono);

    QPalette editPal = m_edit->palette();
    editPal.setColor(QPalette::Base, palette().color(QPalette::Base));
    editPal.setColor(QPalette::Text, palette().color(QPalette::Text));
    m_edit->setPalette(editPal);

    layout->addWidget(m_edit, 1);

    connect(m_copyButton, &QPushButton::clicked, this, &CodeBlockWidget::copyToClipboard);
}

void CodeBlockWidget::setCode(const QString &language, const QString &code)
{
    m_language = language.trimmed();
    m_languageLabel->setText(m_language.isEmpty() ? QStringLiteral("code") : m_language);
    m_edit->setPlainText(code);
    updateHeight();
}

QString CodeBlockWidget::code() const
{
    return m_edit->toPlainText();
}

QString CodeBlockWidget::language() const
{
    return m_language;
}

void CodeBlockWidget::copyToClipboard()
{
    const QString text = m_edit->toPlainText();
    if (text.isEmpty())
        return;

    if (QClipboard *clipboard = QApplication::clipboard())
        clipboard->setText(text);

    m_copyButton->setText(tr("已复制"));
    m_copyButton->setEnabled(false);
    QTimer::singleShot(1200, this, [this] {
        m_copyButton->setText(tr("复制"));
        m_copyButton->setEnabled(true);
    });
    emit copied();
}

void CodeBlockWidget::updateHeight()
{
    const QFontMetrics metrics(m_edit->font());
    const int lineHeight = qMax(1, metrics.lineSpacing());
    const int lines = qMax(1, m_edit->document()->blockCount());
    const int visible = qMin(lines, kMaxVisibleLines);

    const int headerHeight = m_languageLabel ? qMax(22, m_languageLabel->sizeHint().height() + 10) : 24;
    const int editHeight = visible * lineHeight + 22;
    const int total = headerHeight + editHeight;

    setMinimumHeight(total);
    setMaximumHeight(total);
    m_edit->setVerticalScrollBarPolicy(lines > kMaxVisibleLines ? Qt::ScrollBarAsNeeded
                                                               : Qt::ScrollBarAlwaysOff);
}

} // namespace AiAssistant