#include "ui/uipreviewwidget.h"

#include <QBuffer>
#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QUiLoader>
#include <QVBoxLayout>

namespace AiAssistant {

// ---------------------------------------------------------------------------
// 覆盖层：绘制新增/移动标记
// ---------------------------------------------------------------------------
class UiPreviewWidget::Overlay : public QWidget
{
public:
    explicit Overlay(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

    void setState(QWidget *form, const QVector<Mark> &marks)
    {
        m_form = form;
        m_marks = marks;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_form)
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        for (const Mark &mark : m_marks) {
            QWidget *target = m_form->findChild<QWidget *>(mark.objectName);
            if (!target || target == m_form)
                continue;

            // target 一定是 form 的后代，所以 mapTo(form) 安全
            const QRect rect(target->mapTo(m_form, QPoint(0, 0)), target->size());
            if (rect.width() <= 0 || rect.height() <= 0)
                continue;

            QColor color;
            Qt::PenStyle style = Qt::SolidLine;
            QString label;
            switch (mark.kind) {
            case UiPreviewWidget::Added:
                color = QColor(70, 190, 120);
                label = tr("新增");
                break;
            case UiPreviewWidget::Moved:
                color = QColor(80, 150, 240);
                style = Qt::DashLine;
                label = tr("移动");
                break;
            case UiPreviewWidget::Changed:
            default:
                color = QColor(230, 160, 60);
                style = Qt::DotLine;
                label = tr("属性变更");
                break;
            }

            painter.setPen(QPen(color, 2, style));
            QColor fill = color;
            fill.setAlpha(40);
            painter.setBrush(fill);
            painter.drawRect(rect.adjusted(0, 0, -1, -1));

            painter.setPen(color);
            painter.drawText(rect.adjusted(3, 1, 0, 0), Qt::AlignLeft | Qt::AlignTop,
                             mark.note.isEmpty() ? label
                                                 : QStringLiteral("%1 %2").arg(label, mark.note));
        }
    }

private:
    QWidget *m_form = nullptr;
    QVector<Mark> m_marks;
};

// ---------------------------------------------------------------------------

UiPreviewWidget::UiPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_legend = new QLabel(tr("修改后预览"), this);
    m_legend->setWordWrap(true);
    m_legend->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_legend);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::StyledPanel);

    m_canvas = new QWidget;
    auto *canvasLayout = new QVBoxLayout(m_canvas);
    canvasLayout->setContentsMargins(12, 12, 12, 12);
    canvasLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_scrollArea->setWidget(m_canvas);
    layout->addWidget(m_scrollArea, 1);
}

void UiPreviewWidget::clear()
{
    if (m_overlay) {
        m_overlay->hide();
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }
    if (m_form) {
        m_form->removeEventFilter(this);
        if (m_canvas->layout())
            m_canvas->layout()->removeWidget(m_form);
        m_form->hide();
        m_form->setParent(nullptr);
        m_form->deleteLater();
        m_form = nullptr;
    }
    m_marks.clear();
    m_removed.clear();
    updateLegend();
}

bool UiPreviewWidget::loadXml(const QByteArray &xml, QString *error)
{
    clear();
    if (xml.trimmed().isEmpty()) {
        if (error)
            *error = tr("没有可预览的 .ui 内容");
        return false;
    }

    QUiLoader loader;
    QBuffer buffer;
    buffer.setData(xml);
    if (!buffer.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("无法读取 .ui 内容");
        return false;
    }

    QWidget *form = loader.load(&buffer, m_canvas);
    buffer.close();

    if (!form) {
        if (error) {
            *error = tr("QUiLoader 无法实例化该窗体（可能包含自定义控件或动态属性）：%1")
                         .arg(loader.errorString());
        }
        return false;
    }

    m_canvas->layout()->addWidget(form);
    m_form = form;
    m_form->installEventFilter(this);

    m_overlay = new Overlay(m_canvas);
    syncOverlayGeometry();
    m_overlay->raise();
    return true;
}

void UiPreviewWidget::syncOverlayGeometry()
{
    if (!m_overlay || !m_form)
        return;
    m_overlay->setGeometry(m_form->geometry());
    m_overlay->raise();
    m_overlay->setState(m_form, m_marks);
}

bool UiPreviewWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_form && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        syncOverlayGeometry();
    return QWidget::eventFilter(watched, event);
}

void UiPreviewWidget::setMarks(const QVector<Mark> &marks)
{
    m_marks = marks;
    updateLegend();
    syncOverlayGeometry();
}

void UiPreviewWidget::setRemovedWidgets(const QStringList &names)
{
    m_removed = names;
    updateLegend();
}

void UiPreviewWidget::updateLegend()
{
    if (!m_legend)
        return;

    int added = 0;
    int moved = 0;
    int changed = 0;
    for (const Mark &mark : m_marks) {
        if (mark.kind == Added)
            ++added;
        else if (mark.kind == Moved)
            ++moved;
        else
            ++changed;
    }

    QStringList parts;
    parts.append(tr("<span style=\"color:#46be78;\">■ 新增 %1</span>").arg(added));
    parts.append(tr("<span style=\"color:#5096f0;\">▨ 移动 %1</span>").arg(moved));
    if (changed > 0)
        parts.append(tr("<span style=\"color:#e6a03c;\">▤ 属性变更 %1</span>").arg(changed));
    if (!m_removed.isEmpty()) {
        parts.append(tr("<span style=\"color:#c05050;\">✕ 删除 %1：%2</span>")
                         .arg(m_removed.size())
                         .arg(m_removed.join(QStringLiteral(", "))));
    }
    m_legend->setText(tr("修改后预览　%1").arg(parts.join(QStringLiteral("　"))));
}

} // namespace AiAssistant
