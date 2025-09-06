#include "ProcessMonitor.h"
#include <QDebug>
#include <QThread>
#include <Windows.h>
#include <Wbemidl.h>
#include <comdef.h>

#pragma comment(lib, "wbemuuid.lib")

// Forward declaration for our internal EventSink class
class EventSink;

// --- Implementation of the ProcessMonitor class ---

ProcessMonitor::ProcessMonitor(QObject *parent)
    : QObject(parent),
      m_isMonitoring(false),
      m_pSvc(nullptr),
      m_pLoc(nullptr),
      m_pSink(nullptr)
{
}

ProcessMonitor::~ProcessMonitor()
{
    // Ensure monitoring is stopped
    if (m_isMonitoring)
    {
        stopMonitoring();
    }
}

// --- The internal EventSink class required for WMI callbacks ---
// It inherits from the IWbemObjectSink COM interface.
class EventSink : public IWbemObjectSink
{
    LONG m_lRef;
    ProcessMonitor *m_monitor; // Pointer back to our ProcessMonitor

public:
    EventSink(ProcessMonitor *monitor) : m_lRef(0), m_monitor(monitor) {}
    ~EventSink() {}

    // --- IUnknown methods ---
    virtual ULONG STDMETHODCALLTYPE AddRef()
    {
        return InterlockedIncrement(&m_lRef);
    }

    virtual ULONG STDMETHODCALLTYPE Release()
    {
        LONG lRef = InterlockedDecrement(&m_lRef);
        if (lRef == 0)
            delete this;
        return lRef;
    }

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv)
    {
        if (riid == IID_IUnknown || riid == IID_IWbemObjectSink)
        {
            *ppv = (IWbemObjectSink *)this;
            AddRef();
            return WBEM_S_NO_ERROR;
        }
        else
        {
            return E_NOINTERFACE;
        }
    }

    // --- IWbemObjectSink method ---
    // This is the callback that WMI will call with event information.
    virtual HRESULT STDMETHODCALLTYPE Indicate(long lObjectCount, IWbemClassObject **apObjArray)
    {
        for (long i = 0; i < lObjectCount; i++)
        {
            IWbemClassObject *pObj = apObjArray[i];

            // Get the TargetInstance from the event object
            _variant_t vtInstance;
            HRESULT hr = pObj->Get(_bstr_t(L"TargetInstance"), 0, &vtInstance, 0, 0);

            if (SUCCEEDED(hr) && vtInstance.vt == VT_UNKNOWN)
            {
                // The TargetInstance is another WMI object, so we need to query it
                IUnknown *pUnk = vtInstance;
                IWbemClassObject *pTargetInstance = nullptr;
                hr = pUnk->QueryInterface(IID_IWbemClassObject, (void **)&pTargetInstance);

                if (SUCCEEDED(hr))
                {
                    // Now get the process name from the TargetInstance
                    _variant_t vtProcName;
                    if (SUCCEEDED(pTargetInstance->Get(L"Name", 0, &vtProcName, 0, 0)))
                    {
                        // We finally have the exe name
                        QString exeName = QString::fromWCharArray(vtProcName.bstrVal);

                        // Check if this is a creation or deletion event
                        _variant_t vtClass;
                        pObj->Get(L"__CLASS", 0, &vtClass, 0, 0);
                        QString eventClass = QString::fromWCharArray(vtClass.bstrVal);

                        // Emit signals using Qt's queued connection for thread safety
                        if (eventClass == "__InstanceCreationEvent")
                        {
                            QMetaObject::invokeMethod(m_monitor, "processStarted",
                                                      Qt::QueuedConnection, Q_ARG(QString, exeName));
                        }
                        else if (eventClass == "__InstanceDeletionEvent")
                        {
                            QMetaObject::invokeMethod(m_monitor, "processStopped",
                                                      Qt::QueuedConnection, Q_ARG(QString, exeName));
                        }
                    }
                    pTargetInstance->Release();
                }
            }
        }
        return WBEM_S_NO_ERROR;
    }

    virtual HRESULT STDMETHODCALLTYPE SetStatus(LONG lFlags, HRESULT hResult, BSTR strParam, IWbemClassObject __RPC_FAR *pObjParam)
    {
        Q_UNUSED(lFlags);
        Q_UNUSED(hResult);
        Q_UNUSED(strParam);
        Q_UNUSED(pObjParam);
        return WBEM_S_NO_ERROR;
    }
};

// --- Main logic for ProcessMonitor ---

void ProcessMonitor::startMonitoring()
{
    if (m_isMonitoring)
    {
        qDebug() << "Process monitor already running";
        return;
    }

    m_isMonitoring = true;
    qDebug() << "Process monitor thread started. Initializing WMI...";

    HRESULT hr;

    // 1. Initialize COM for this thread
    hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        qCritical() << "Failed to initialize COM library. Error code:" << hr;
        return;
    }

    // 2. Set general COM security levels
    hr = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL);
    if (FAILED(hr))
    {
        qDebug() << "CoInitializeSecurity failed (may already be set):" << hr;
        // Don't return here - this often fails if already initialized
    }

    // 3. Obtain the initial locator to WMI
    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, (LPVOID *)&m_pLoc);
    if (FAILED(hr))
    {
        qCritical() << "Failed to create IWbemLocator object. Error code:" << hr;
        CoUninitialize();
        return;
    }

    // 4. Connect to WMI through the IWbemLocator::ConnectServer method
    hr = m_pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0,
                               NULL, 0, 0, &m_pSvc);
    if (FAILED(hr))
    {
        qCritical() << "Could not connect to WMI. Error code:" << hr;
        m_pLoc->Release();
        m_pLoc = nullptr;
        CoUninitialize();
        return;
    }
    qDebug() << "WMI connection successful.";

    // 5. Set security levels on the proxy
    hr = CoSetProxyBlanket(
        m_pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE);
    if (FAILED(hr))
    {
        qCritical() << "Could not set proxy blanket. Error code:" << hr;
        m_pSvc->Release();
        m_pSvc = nullptr;
        m_pLoc->Release();
        m_pLoc = nullptr;
        CoUninitialize();
        return;
    }

    qDebug() << "Scanning for existing processes...";
    IEnumWbemClassObject *pEnumerator = nullptr;
    hr = m_pSvc->ExecQuery(
        _bstr_t(L"WQL"),
        _bstr_t(L"SELECT Name FROM Win32_Process"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL,
        &pEnumerator);

    if (SUCCEEDED(hr))
    {
        IWbemClassObject *pclsObj = nullptr;
        ULONG uReturn = 0;

        while (pEnumerator)
        {
            pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
            if (0 == uReturn)
            {
                break; // No more processes
            }

            _variant_t vtProp;
            // Get the value of the Name property
            if (SUCCEEDED(pclsObj->Get(L"Name", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BSTR)
            {
                QString exeName = QString::fromWCharArray(vtProp.bstrVal);
                // Emit signal for MainWindow to check if it's a monitored game
                QMetaObject::invokeMethod(this, "processStarted",
                                          Qt::QueuedConnection, Q_ARG(QString, exeName));
            }
            pclsObj->Release();
        }
        pEnumerator->Release();
    }
    else
    {
        qCritical() << "Failed to query for existing processes. Error code:" << hr;
    }
    qDebug() << "Initial process scan complete.";
    
    // 6. Create an EventSink for WMI to call back to
    m_pSink = new EventSink(this);
    m_pSink->AddRef();

    // 7. Register the queries for process creation and deletion events
    BSTR queryLanguage = SysAllocString(L"WQL");

    // Query for process creation
    hr = m_pSvc->ExecNotificationQueryAsync(
        queryLanguage,
        _bstr_t(L"SELECT * FROM __InstanceCreationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'"),
        WBEM_FLAG_SEND_STATUS,
        NULL,
        m_pSink);

    if (SUCCEEDED(hr))
    {
        // Query for process deletion
        hr = m_pSvc->ExecNotificationQueryAsync(
            queryLanguage,
            _bstr_t(L"SELECT * FROM __InstanceDeletionEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'"),
            WBEM_FLAG_SEND_STATUS,
            NULL,
            m_pSink);
    }

    SysFreeString(queryLanguage);

    if (SUCCEEDED(hr))
    {
        qDebug() << "WMI event queries registered. Monitoring for process changes...";

        // Start the COM message pump in this thread
        MSG msg;
        while (m_isMonitoring && GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        qDebug() << "Process monitor message loop ended";
    }
    else
    {
        qCritical() << "Failed to register WMI queries. Error code:" << hr;
        stopMonitoring();
    }
}

void ProcessMonitor::stopMonitoring()
{
    if (!m_isMonitoring)
    {
        return;
    }

    qDebug() << "Stopping process monitor...";
    m_isMonitoring = false;

    // Post a quit message to break the message loop
    PostThreadMessage(GetCurrentThreadId(), WM_QUIT, 0, 0);

    // Clean up WMI objects
    if (m_pSvc)
    {
        // Cancel the async queries
        m_pSvc->CancelAsyncCall(m_pSink);
    }

    if (m_pSink)
    {
        m_pSink->Release();
        m_pSink = nullptr;
    }

    if (m_pSvc)
    {
        m_pSvc->Release();
        m_pSvc = nullptr;
    }

    if (m_pLoc)
    {
        m_pLoc->Release();
        m_pLoc = nullptr;
    }

    // Uninitialize COM for this thread
    CoUninitialize();

    qDebug() << "Process monitor stopped";
}