#pragma once

#include <QObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QThread>
#include <mmdeviceapi.h>

// This class will run on a separate thread to safely enumerate audio devices.
class AudioDeviceFetcher : public QObject
{
    Q_OBJECT

public:
    explicit AudioDeviceFetcher(QObject *parent = nullptr);

public slots:
    void fetchOutputDevices(); // For speakers/headphones
    void fetchInputDevices();  // For microphones

signals:
    // These signals will be emitted when the list is ready.
    void outputDevicesFetched(const QList<QPair<QString, QString>> &devices);
    void inputDevicesFetched(const QList<QPair<QString, QString>> &devices);
    void finished();

private:
    void fetchDevices(EDataFlow dataFlow); // Generic internal method
};