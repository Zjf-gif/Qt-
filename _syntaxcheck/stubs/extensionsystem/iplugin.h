#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

namespace ExtensionSystem {

class IPlugin : public QObject
{
    Q_OBJECT
public:
    enum ShutdownFlag {
        SynchronousShutdown,
        AsynchronousShutdown
    };

    IPlugin();
    ~IPlugin() override;

    virtual bool initialize(const QStringList &arguments, QString *errorString) = 0;
    virtual void extensionsInitialized() {}
    virtual bool delayedInitialize() { return false; }
    virtual ShutdownFlag aboutToShutdown() { return SynchronousShutdown; }
};

} // namespace ExtensionSystem
