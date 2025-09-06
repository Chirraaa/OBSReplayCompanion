#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <QObject>
#include <QKeySequence>
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDebug>

class GlobalHotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey();

    bool registerHotkey(int id, const QKeySequence& keySequence);
    bool unregisterHotkey(int id);
    void unregisterAllHotkeys();

signals:
    void hotkeyPressed(int id);

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    struct HotkeyInfo {
        int id;
        UINT modifiers;
        UINT virtualKey;
    };

    bool parseKeySequence(const QKeySequence& keySequence, UINT& modifiers, UINT& virtualKey);
    UINT qtKeyToWin32Key(int qtKey);
    UINT qtModifierToWin32Modifier(int qtModifier);

    QList<HotkeyInfo> m_registeredHotkeys;
    HWND m_hwnd;
};