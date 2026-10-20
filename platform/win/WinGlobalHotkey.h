#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QSet>
#include <QString>
#include <functional>

#include "platform/platform.h"

// Windows IGlobalHotkey backend: RegisterHotKey + a native event filter that
// translates WM_HOTKEY into the registered callback. Registers thread-level
// hotkeys (hwnd = nullptr); Qt's event dispatcher delivers WM_HOTKEY here.
// (windows.h is kept out of this header.)
//
// App exclusions (setIgnoredApps) UNREGISTER every hotkey while a listed app is
// foreground rather than ignoring WM_HOTKEY: a registered combo is consumed by the OS
// and never reaches the foreground app, so skipping the callback would swallow the key
// instead of passing it through. Quick snip is a separate hook (WinQuickDragGesture)
// and is not muted by the list.
class WinGlobalHotkey : public platform::IGlobalHotkey, public QAbstractNativeEventFilter {
public:
    WinGlobalHotkey();
    ~WinGlobalHotkey() override;

    int registerHotkey(const QString& sequence, std::function<void()> onActivated) override;
    void unregister(int id) override;
    void unregisterAll() override;
    bool probe(const QString& sequence) override;
    void setIgnoredApps(const QStringList& exeBasenames) override;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // Re-check the foreground app and suppress/restore as needed. Public because the
    // WinEvent callback (a plain C function, no user pointer) drives it via g_self.
    void evaluateForeground();

private:
    // One desired registration; the modifiers/vk let us re-RegisterHotKey after a
    // suppression, keeping the same id. mods here is the raw Win32 MOD_* value.
    struct Reg { unsigned int mods; unsigned int vk; std::function<void()> cb; };

    void suppress();             // UnregisterHotKey every live reg (excluded app is foreground)
    void restore();              // re-RegisterHotKey every reg (focus left the excluded app)

    QHash<int, std::function<void()>> m_callbacks;   // hotkey id -> callback (fast WM_HOTKEY lookup)
    QHash<int, Reg> m_regs;      // hotkey id -> desired registration (source of truth for restore)
    QSet<QString> m_ignored;     // lowercased exe basenames to ignore hotkeys for
    bool m_suppressed = false;   // true while an ignored app holds the foreground
    void* m_winEventHook = nullptr;   // HWINEVENTHOOK (opaque; watches EVENT_SYSTEM_FOREGROUND)
    int m_nextId = 1;
};
