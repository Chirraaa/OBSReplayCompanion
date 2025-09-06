#include "AudioDeviceFetcher.h"
#include <QDebug>
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

AudioDeviceFetcher::AudioDeviceFetcher(QObject *parent) : QObject(parent) {}


void AudioDeviceFetcher::fetchOutputDevices()
{
    fetchDevices(eRender);
}

void AudioDeviceFetcher::fetchInputDevices()
{
    fetchDevices(eCapture);
}


void AudioDeviceFetcher::fetchDevices(EDataFlow dataFlow)
{
    qDebug() << "AudioDeviceFetcher: Running on thread" << QThread::currentThreadId();
    QList<QPair<QString, QString>> devices;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        qDebug() << "AudioDeviceFetcher: CoInitializeEx failed, HRESULT:" << Qt::hex << hr;
        if (dataFlow == eRender)
            emit outputDevicesFetched(devices);
        else
            emit inputDevicesFetched(devices);

        emit finished();
        return;
    }

    if (dataFlow == eRender)
    {
        devices.append(qMakePair(QString("default"), QString("Default")));
    }
    else
    {
        devices.append(qMakePair(QString("default"), QString("Default Microphone")));
    }

    IMMDeviceEnumerator *pEnumerator = NULL;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void **)&pEnumerator);
    if (FAILED(hr))
    {
        qDebug() << "AudioDeviceFetcher: CoCreateInstance failed, HRESULT:" << Qt::hex << hr;
    }
    else
    {
        IMMDeviceCollection *pCollection = NULL;
        hr = pEnumerator->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, &pCollection);
        if (SUCCEEDED(hr))
        {
            UINT count = 0;
            pCollection->GetCount(&count);
            qDebug() << "AudioDeviceFetcher: Found" << count << "active audio endpoint(s) for flow" << dataFlow;

            for (UINT i = 0; i < count; i++)
            {
                IMMDevice *pDevice = NULL;
                if (SUCCEEDED(pCollection->Item(i, &pDevice)))
                {
                    LPWSTR pwszID = NULL;
                    if (SUCCEEDED(pDevice->GetId(&pwszID)))
                    {
                        IPropertyStore *pProps = NULL;
                        if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps)))
                        {
                            PROPVARIANT varName;
                            PropVariantInit(&varName);
                            if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName)))
                            {
                                QString deviceId = QString::fromWCharArray(pwszID);
                                QString deviceName = QString::fromWCharArray(varName.pwszVal);
                                qDebug() << "AudioDeviceFetcher: Found device -> Name:" << deviceName << ", ID:" << deviceId;
                                devices.append(qMakePair(deviceId, deviceName));
                            }
                            PropVariantClear(&varName);
                            pProps->Release();
                        }
                        CoTaskMemFree(pwszID);
                    }
                    pDevice->Release();
                }
            }
            pCollection->Release();
        }
        else
        {
            qDebug() << "AudioDeviceFetcher: EnumAudioEndpoints failed, HRESULT:" << Qt::hex << hr;
        }
        pEnumerator->Release();
    }

    qDebug() << "AudioDeviceFetcher: Enumeration finished. Emitting results for flow" << dataFlow;
    if (dataFlow == eRender)
    {
        emit outputDevicesFetched(devices);
    }
    else
    {
        emit inputDevicesFetched(devices);
    }

    CoUninitialize();

    emit finished();
}