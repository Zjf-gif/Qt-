#pragma once
#include <QKeySequence>
#include <QObject>
#include <QStringList>

class QAction;

namespace Core {

class Command : public QObject
{
    Q_OBJECT
public:
    QAction *action() const;
    void setDefaultKeySequence(const QKeySequence &sequence);
};

} // namespace Core
