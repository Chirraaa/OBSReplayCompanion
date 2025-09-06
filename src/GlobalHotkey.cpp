#include "GlobalHotkey.h"
#include <QDebug>

GlobalHotkey::GlobalHotkey(QObject* parent)
    : QObject(parent), m_hwnd(nullptr)
{
    // Create a hidden window to receive hotkey messages
    WNDCLASS wc = {};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"GlobalHotkeyWindow";
    
    if (RegisterClass(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        m_hwnd = CreateWindow(L"GlobalHotkeyWindow", L"", 0, 0, 0, 0, 0, 
                             HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr);
    }

    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkey::~GlobalHotkey()
{
    unregisterAllHotkeys();
    QCoreApplication::instance()->removeNativeEventFilter(this);
    
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        UnregisterClass(L"GlobalHotkeyWindow", GetModuleHandle(nullptr));
    }
}

bool GlobalHotkey::registerHotkey(int id, const QKeySequence& keySequence)
{
    if (!m_hwnd || keySequence.isEmpty()) {
        return false;
    }

    UINT modifiers, virtualKey;
    if (!parseKeySequence(keySequence, modifiers, virtualKey)) {
        return false;
    }

    // Unregister if already registered
    unregisterHotkey(id);

    if (RegisterHotKey(m_hwnd, id, modifiers, virtualKey)) {
        HotkeyInfo info;
        info.id = id;
        info.modifiers = modifiers;
        info.virtualKey = virtualKey;
        m_registeredHotkeys.append(info);
        return true;
    }

    return false;
}

bool GlobalHotkey::unregisterHotkey(int id)
{
    if (!m_hwnd) {
        return false;
    }

    for (auto it = m_registeredHotkeys.begin(); it != m_registeredHotkeys.end(); ++it) {
        if (it->id == id) {
            bool success = UnregisterHotKey(m_hwnd, id);
            m_registeredHotkeys.erase(it);
            return success;
        }
    }
    return false;
}

void GlobalHotkey::unregisterAllHotkeys()
{
    if (!m_hwnd) {
        return;
    }

    for (const auto& hotkey : m_registeredHotkeys) {
        UnregisterHotKey(m_hwnd, hotkey.id);
    }
    m_registeredHotkeys.clear();
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)

    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        
        if (msg->message == WM_HOTKEY && msg->hwnd == m_hwnd) {
            int hotkeyId = msg->wParam;
            emit hotkeyPressed(hotkeyId);
            return true;
        }
    }
    
    return false;
}

bool GlobalHotkey::parseKeySequence(const QKeySequence& keySequence, UINT& modifiers, UINT& virtualKey)
{
    if (keySequence.isEmpty()) {
        return false;
    }

    int key = keySequence[0];
    
    modifiers = 0;
    virtualKey = 0;

    // Extract modifiers
    if (key & Qt::ControlModifier) {
        modifiers |= MOD_CONTROL;
    }
    if (key & Qt::AltModifier) {
        modifiers |= MOD_ALT;
    }
    if (key & Qt::ShiftModifier) {
        modifiers |= MOD_SHIFT;
    }
    if (key & Qt::MetaModifier) {
        modifiers |= MOD_WIN;
    }

    // Extract the actual key
    int qtKey = key & ~(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    virtualKey = qtKeyToWin32Key(qtKey);

    return virtualKey != 0;
}

UINT GlobalHotkey::qtKeyToWin32Key(int qtKey)
{
    // Function keys
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) {
        return VK_F1 + (qtKey - Qt::Key_F1);
    }

    // Number keys
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        return '0' + (qtKey - Qt::Key_0);
    }

    // Letter keys
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        return 'A' + (qtKey - Qt::Key_A);
    }

    // Special keys
    switch (qtKey) {
        case Qt::Key_Space: return VK_SPACE;
        case Qt::Key_Enter: return VK_RETURN;
        case Qt::Key_Return: return VK_RETURN;
        case Qt::Key_Escape: return VK_ESCAPE;
        case Qt::Key_Tab: return VK_TAB;
        case Qt::Key_Backspace: return VK_BACK;
        case Qt::Key_Delete: return VK_DELETE;
        case Qt::Key_Insert: return VK_INSERT;
        case Qt::Key_Home: return VK_HOME;
        case Qt::Key_End: return VK_END;
        case Qt::Key_PageUp: return VK_PRIOR;
        case Qt::Key_PageDown: return VK_NEXT;
        case Qt::Key_Up: return VK_UP;
        case Qt::Key_Down: return VK_DOWN;
        case Qt::Key_Left: return VK_LEFT;
        case Qt::Key_Right: return VK_RIGHT;
        case Qt::Key_Print: return VK_SNAPSHOT;
        case Qt::Key_Pause: return VK_PAUSE;
        case Qt::Key_CapsLock: return VK_CAPITAL;
        case Qt::Key_NumLock: return VK_NUMLOCK;
        case Qt::Key_ScrollLock: return VK_SCROLL;
        default: return 0;
    }
}

UINT GlobalHotkey::qtModifierToWin32Modifier(int qtModifier)
{
    UINT modifiers = 0;
    if (qtModifier & Qt::ControlModifier) modifiers |= MOD_CONTROL;
    if (qtModifier & Qt::AltModifier) modifiers |= MOD_ALT;
    if (qtModifier & Qt::ShiftModifier) modifiers |= MOD_SHIFT;
    if (qtModifier & Qt::MetaModifier) modifiers |= MOD_WIN;
    return modifiers;
}