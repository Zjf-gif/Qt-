#include "ui/diffview.h"

#include <QFontDatabase>
#include <QPalette>
#include <QStringList>
#include <QVector>

namespace AiAssistant {

namespace {

struct DiffOp
{
    QChar type;   // ' ', '-', '+'
    QString text;
};

const int kMaxLines = 6000;
const int kMaxDpCells = 4000000;

QVector<DiffOp> computeDiff(const QStringList &a, const QStringList &b)
{
    QVector<DiffOp> ops;

    int prefix = 0;
    while (prefix < a.size() && prefix < b.size() && a.at(prefix) == b.at(prefix))
        ++prefix;

    int suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix
           && a.at(a.size() - 1 - suffix) == b.at(b.size() - 1 - suffix)) {
        ++suffix;
    }

    const int n = a.size() - prefix - suffix;
    const int m = b.size() - prefix - suffix;

    for (int i = 0; i < prefix; ++i) {
        DiffOp op;
        op.type = QLatin1Char(' ');
        op.text = a.at(i);
        ops.append(op);
    }

    if (n > 0 && m > 0 && qint64(n) * qint64(m) <= kMaxDpCells) {
        QVector<QVector<int>> dp(n + 1, QVector<int>(m + 1, 0));
        for (int i = n - 1; i >= 0; --i) {
            for (int j = m - 1; j >= 0; --j) {
                if (a.at(prefix + i) == b.at(prefix + j))
                    dp[i][j] = dp[i + 1][j + 1] + 1;
                else
                    dp[i][j] = qMax(dp[i + 1][j], dp[i][j + 1]);
            }
        }

        int i = 0;
        int j = 0;
        while (i < n && j < m) {
            if (a.at(prefix + i) == b.at(prefix + j)) {
                DiffOp op;
                op.type = QLatin1Char(' ');
                op.text = a.at(prefix + i);
                ops.append(op);
                ++i;
                ++j;
            } else if (dp[i + 1][j] >= dp[i][j + 1]) {
                DiffOp op;
                op.type = QLatin1Char('-');
                op.text = a.at(prefix + i);
                ops.append(op);
                ++i;
            } else {
                DiffOp op;
                op.type = QLatin1Char('+');
                op.text = b.at(prefix + j);
                ops.append(op);
                ++j;
            }
        }
        while (i < n) {
            DiffOp op;
            op.type = QLatin1Char('-');
            op.text = a.at(prefix + i);
            ops.append(op);
            ++i;
        }
        while (j < m) {
            DiffOp op;
            op.type = QLatin1Char('+');
            op.text = b.at(prefix + j);
            ops.append(op);
            ++j;
        }
    } else {
        for (int i = 0; i < n; ++i) {
            DiffOp op;
            op.type = QLatin1Char('-');
            op.text = a.at(prefix + i);
            ops.append(op);
        }
        for (int j = 0; j < m; ++j) {
            DiffOp op;
            op.type = QLatin1Char('+');
            op.text = b.at(prefix + j);
            ops.append(op);
        }
    }

    for (int i = 0; i < suffix; ++i) {
        DiffOp op;
        op.type = QLatin1Char(' ');
        op.text = a.at(a.size() - suffix + i);
        ops.append(op);
    }
    return ops;
}

} // namespace

DiffView::DiffView(QWidget *parent)
    : QTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
}

void DiffView::showDiff(const QString &before, const QString &after, const QString &title)
{
    int added = 0;
    int removed = 0;
    const QString body = buildDiffHtml(before, after, &added, &removed);

    QString header;
    if (!title.isEmpty()) {
        header = QStringLiteral("<div style=\"font-weight:bold; margin-bottom:4px;\">%1</div>")
                     .arg(title.toHtmlEscaped());
    }
    const QString summary =
        QStringLiteral("<div style=\"color:#7a7a7a; margin-bottom:6px;\">差异："
                       "<span style=\"color:#2f9e5f;\">+%1</span> / "
                       "<span style=\"color:#c05050;\">-%2</span> 行</div>")
            .arg(added)
            .arg(removed);

    if (added == 0 && removed == 0)
        setPlainText(tr("（内容没有变化）"));
    else
        setHtml(header + summary + body);
}

void DiffView::showMessage(const QString &text, bool isError)
{
    const QString color = isError ? QStringLiteral("#c05050") : QStringLiteral("#7a7a7a");
    setHtml(QStringLiteral("<div style=\"color:%1; white-space:pre-wrap;\">%2</div>")
                .arg(color, text.toHtmlEscaped()));
}

QString DiffView::buildDiffHtml(const QString &before, const QString &after,
                                int *addedLines, int *removedLines)
{
    QStringList a = before.split(QLatin1Char('\n'));
    QStringList b = after.split(QLatin1Char('\n'));

    bool truncated = false;
    if (a.size() > kMaxLines) {
        a = a.mid(0, kMaxLines);
        truncated = true;
    }
    if (b.size() > kMaxLines) {
        b = b.mid(0, kMaxLines);
        truncated = true;
    }

    const QVector<DiffOp> ops = computeDiff(a, b);

    int added = 0;
    int removed = 0;
    QString html = QStringLiteral(
        "<pre style=\"font-family:Consolas,'Courier New',monospace; font-size:9pt; margin:0;\">");

    for (const DiffOp &op : ops) {
        QString background = QStringLiteral("transparent");
        QChar marker = QLatin1Char(' ');
        QString markerColor = QStringLiteral("#8a8a8a");
        if (op.type == QLatin1Char('+')) {
            background = QStringLiteral("rgba(60,180,110,0.25)");
            marker = QLatin1Char('+');
            markerColor = QStringLiteral("#2f9e5f");
            ++added;
        } else if (op.type == QLatin1Char('-')) {
            background = QStringLiteral("rgba(220,80,80,0.25)");
            marker = QLatin1Char('-');
            markerColor = QStringLiteral("#c05050");
            ++removed;
        }

        QString text = op.text;
        if (text.isEmpty())
            text = QStringLiteral("&nbsp;");
        else
            text = text.toHtmlEscaped();

        html += QStringLiteral("<span style=\"background-color:%1;\">"
                               "<span style=\"color:%2;\">%3</span> %4</span><br/>")
                    .arg(background, markerColor, QString(marker), text);
    }

    if (truncated) {
        html += QStringLiteral("<span style=\"color:#c05050;\">…（文件超过 %1 行，diff 已截断）</span>")
                    .arg(kMaxLines);
    }
    html += QStringLiteral("</pre>");

    if (addedLines)
        *addedLines = added;
    if (removedLines)
        *removedLines = removed;
    return html;
}

} // namespace AiAssistant
