#ifndef PROCESSMONITOR_H
#define PROCESSMONITOR_H

#include <QObject>
#include <QStringList>
#include <QTimer>

// Forward declarations for WMI interfaces
struct IWbemServices;
struct IWbemLocator;
struct IWbemObjectSink;

class ProcessMonitor : public QObject
{
    Q_OBJECT

public:
    explicit ProcessMonitor(QObject *parent = nullptr);
    ~ProcessMonitor();

public slots:
    void startMonitoring();
    void stopMonitoring();

signals:
    void processStarted(const QString &exeName);
    void processStopped(const QString &exeName);

private:
    bool m_isMonitoring;
    
    // WMI interface pointers
    IWbemServices* m_pSvc;
    IWbemLocator* m_pLoc;
    IWbemObjectSink* m_pSink;
};

#endif // PROCESSMONITOR_H