#pragma once
#include <QSettings>
#include <QString>

class QMainWindow;

namespace Core {

class ICore
{
public:
    static QSettings *settings();
    static QMainWindow *mainWindow();
};

} // namespace Core
