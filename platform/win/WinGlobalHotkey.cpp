#include "platform/win/WinGlobalHotkey.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>

#include "core/action/KeySequence.h"
#include "core/action/KeySequenceCodec.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

namespace {

// Qt::Key -> Win32 virtual-key code; 0 if unmappable.
UINT vkFromQtKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return static_cast<UINT>('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return static_cast<UINT>('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return VK_F1 + (key - Qt::Key_F1);
    switch (key) {
        case Qt::Key_Escape:   return VK_ESCAPE;
        case Qt::Key_Tab:      return VK_TAB;
        case Qt::Key_Space:    return VK_SPACE;
        case Qt::Key_Return:
        case Qt::Key_Enter:    return VK_RETURN;
        case Qt::Key_Backspace:return VK_BACK;
        case Qt::Key_Delete:   return VK_DELETE;
        case Qt::Key_Insert:   return VK_INSERT;
        case Qt::Key_Home:     return VK_HOME;
        case Qt::Key_End:      return VK_END;
        case Qt::Key_PageUp:   return VK_PRIOR;
        case Qt::Key_PageDown: return VK_NEXT;
        case Qt::Key_Up:       return VK_UP;
        case Qt::Key_Down:     return VK_DOWN;
        case Qt::Key_Left:     return VK_LEFT;
        case Qt::Key_Right:    return VK_RIGHT;
        case Qt::Key_Comma:    return VK_OEM_COMMA;
        case Qt::Key_Period:   return VK_OEM_PERIOD;
    }
    return 0;
}

// Exe basename (lowercased, e.g. "chrome.exe") of the current foreground window's
// owning process; empty if none, or if the process can't be opened (an elevated
// app we lack rights to query — treated as "not ignored", i.e. fail open).
QString foregroundExeBasename()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return QString();
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return QString();
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return QString();
    wchar_t buf[MAX_PATH];
    DWORD sz = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(h, 0, buf, &sz))
        name = QFileInfo(QString::fromWCharArray(buf, int(sz))).fileName().toLower();
    CloseHandle(h);
    return name;
}

// The one live instance, so the parameter-less WinEvent callback can reach it.
// Hotkeys are thread-level and there is a single WinGlobalHotkey in the app.
WinGlobalHotkey* g_self = nullptr;

void CALLBACK foregroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
{
    if (g_self)
        g_self->evaluateForeground();
}

} // namespace

WinGlobalHotkey::WinGlobalHotkey()
{
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installNativeEventFilter(this);
}

WinGlobalHotkey::~WinGlobalHotkey()
{
    if (m_winEventHook) {
        ::UnhookWinEvent(static_cast<HWINEVENTHOOK>(m_winEventHook));
        m_winEventHook = nullptr;
    }
    if (g_self == this)
        g_self = nullptr;
    const auto ids = m_callbacks.keys();
    for (int id : ids)
        ::UnregisterHotKey(nullptr, id);
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

int WinGlobalHotkey::registerHotkey(const QString& sequence, std::function<void()> onActivated)
{
    const KeySequence ks = KeySequenceCodec::parse(sequence);
    if (!ks.valid)
        return -1;

    UINT mods = 0;
    if (ks.mods.testFlag(Qt::ControlModifier)) mods |= MOD_CONTROL;
    if (ks.mods.testFlag(Qt::ShiftModifier))   mods |= MOD_SHIFT;
    if (ks.mods.testFlag(Qt::AltModifier))     mods |= MOD_ALT;
    if (ks.mods.testFlag(Qt::MetaModifier))    mods |= MOD_WIN;

    const UINT vk = vkFromQtKey(ks.key);
    if (vk == 0)
        return -1;

    const int id = m_nextId++;
    // While an ignored app holds the foreground we deliberately don't grab the
    // combo (so the key reaches that app) — record the desired reg and restore()
    // will register it once focus leaves. Otherwise register now.
    if (!m_suppressed && !::RegisterHotKey(nullptr, id, mods | MOD_NOREPEAT, vk))
        return -1;                              // already taken by another app

    m_callbacks.insert(id, onActivated);
    m_regs.insert(id, Reg{mods, vk, std::move(onActivated)});
    return id;
}

void WinGlobalHotkey::unregister(int id)
{
    m_regs.remove(id);
    if (m_callbacks.remove(id) > 0)
        ::UnregisterHotKey(nullptr, id);        // harmless no-op if currently suppressed
}

void WinGlobalHotkey::unregisterAll()
{
    const auto ids = m_callbacks.keys();
    for (int id : ids)
        ::UnregisterHotKey(nullptr, id);
    m_callbacks.clear();
    m_regs.clear();
    m_suppressed = false;   // no regs left; a fresh registerHotkey should grab for real
}

void WinGlobalHotkey::setIgnoredApps(const QStringList& exeBasenames)
{
    m_ignored.clear();
    for (const QString& e : exeBasenames) {
        const QString n = e.trimmed().toLower();
        if (!n.isEmpty())
            m_ignored.insert(n);
    }

    if (m_ignored.isEmpty()) {
        // Nothing to watch: drop the hook and make sure hotkeys are live.
        if (m_winEventHook) {
            ::UnhookWinEvent(static_cast<HWINEVENTHOOK>(m_winEventHook));
            m_winEventHook = nullptr;
        }
        if (g_self == this)
            g_self = nullptr;
        if (m_suppressed)
            restore();
        return;
    }

    if (!m_winEventHook) {
        g_self = this;
        // OUTOFCONTEXT → the callback runs on this (the hooking) thread's message
        // loop, no DLL injection. Range covers the single EVENT_SYSTEM_FOREGROUND.
        // (Not SKIPOWNPROCESS: focusing our own window must also re-evaluate, so
        // hotkeys restore when the user clicks a SnapCap pin after an excluded app.)
        m_winEventHook = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                           nullptr, &foregroundChanged, 0, 0,
                                           WINEVENT_OUTOFCONTEXT);
    }
    evaluateForeground();   // apply the (possibly changed) list to the current foreground now
}

void WinGlobalHotkey::evaluateForeground()
{
    const bool ignore = m_ignored.contains(foregroundExeBasename());
    if (ignore && !m_suppressed)
        suppress();
    else if (!ignore && m_suppressed)
        restore();
}

void WinGlobalHotkey::suppress()
{
    for (auto it = m_regs.constBegin(); it != m_regs.constEnd(); ++it)
        ::UnregisterHotKey(nullptr, it.key());
    m_suppressed = true;
}

void WinGlobalHotkey::restore()
{
    for (auto it = m_regs.constBegin(); it != m_regs.constEnd(); ++it)
        ::RegisterHotKey(nullptr, it.key(), it.value().mods | MOD_NOREPEAT, it.value().vk);
    m_suppressed = false;
}

bool WinGlobalHotkey::probe(const QString& sequence)
{
    const KeySequence ks = KeySequenceCodec::parse(sequence);
    if (!ks.valid)
        return false;

    UINT mods = 0;
    if (ks.mods.testFlag(Qt::ControlModifier)) mods |= MOD_CONTROL;
    if (ks.mods.testFlag(Qt::ShiftModifier))   mods |= MOD_SHIFT;
    if (ks.mods.testFlag(Qt::AltModifier))     mods |= MOD_ALT;
    if (ks.mods.testFlag(Qt::MetaModifier))    mods |= MOD_WIN;

    const UINT vk = vkFromQtKey(ks.key);
    if (vk == 0)
        return false;

    // Test-register on a scratch id well clear of the live-registration range,
    // then release immediately — a successful grab means the combo is free.
    const int scratchId = 0x7fff;
    if (!::RegisterHotKey(nullptr, scratchId, mods | MOD_NOREPEAT, vk))
        return false;
    ::UnregisterHotKey(nullptr, scratchId);
    return true;
}

bool WinGlobalHotkey::nativeEventFilter(const QByteArray& /*eventType*/, void* message, qintptr* /*result*/)
{
    auto* msg = static_cast<MSG*>(message);
    if (msg && msg->message == WM_HOTKEY) {
        const int id = static_cast<int>(msg->wParam);
        const auto it = m_callbacks.constFind(id);
        if (it != m_callbacks.constEnd() && it.value())
            it.value()();
    }
    return false;                                // don't consume
}
