#include "platform/win/WinWheelWatch.h"

#include <QCoreApplication>
#include <QPoint>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

namespace {

// Process-global hook, one session at a time (see the header).
HHOOK s_hook = nullptr;
std::function<void(QPoint, bool, bool)> s_onWheel;
// Bumped by both arm() and disarm(). Deliveries are QUEUED to the GUI thread, so
// unhooking does not stop the ones already in flight — and those carry their own copy
// of the callback, which would happily run against a session that has since ended (a
// torn-down overlay). Each queued delivery therefore carries the generation it was
// posted under and drops itself if that no longer matches. Everything here runs on the
// GUI thread: a low-level hook is dispatched on the thread that installed it, and
// arm()/disarm() are called from there too, so no synchronisation is needed.
quint64 s_generation = 0;

LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && (wParam == WM_MOUSEWHEEL || wParam == WM_MOUSEHWHEEL)
        && s_onWheel) {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        const QPoint p(ms->pt.x, ms->pt.y);       // virtual-desktop physical px
        const bool injected = (ms->flags & LLMHF_INJECTED) != 0;
        const bool horizontal = (wParam == WM_MOUSEHWHEEL);
        auto fn = s_onWheel;
        const quint64 gen = s_generation;
        // Out of the hook proc before doing anything: a low-level hook that dawdles is
        // skipped by Windows once it exceeds LowLevelHooksTimeout, so bursts arrive
        // partially and late under load. That is fine for the one consumer — an idle
        // debounce, which only needs to see that scrolling happened.
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  [fn, p, gen, injected, horizontal] {
            if (gen != s_generation)
                return;                           // disarmed (or re-armed) meanwhile
            fn(p, injected, horizontal);
        }, Qt::QueuedConnection);
    }
    // Never swallow: the window under the cursor has to scroll.
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

} // namespace

WinWheelWatch::~WinWheelWatch() { disarm(); }

void WinWheelWatch::arm(std::function<void(QPoint posCapturePx, bool injected,
                                           bool horizontalWheel)> onWheel)
{
    disarm();                                     // never stack hooks (also bumps the generation)
    if (!onWheel)
        return;
    s_onWheel = std::move(onWheel);
    s_hook = SetWindowsHookExW(WH_MOUSE_LL, hookProc, GetModuleHandleW(nullptr), 0);
}

void WinWheelWatch::disarm()
{
    if (s_hook) {
        UnhookWindowsHookEx(s_hook);
        s_hook = nullptr;
    }
    s_onWheel = nullptr;
    ++s_generation;          // strands any delivery already queued to the GUI thread
}
