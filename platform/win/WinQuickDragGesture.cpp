#include "platform/win/WinQuickDragGesture.h"

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

// Low-level mouse hooks are process-global and only one gesture runs at a time,
// so the C-style hook proc reads a single set of file-static state.
HHOOK s_hook = nullptr;
Qt::KeyboardModifiers s_required = Qt::NoModifier;
platform::IQuickDragGesture::Callbacks s_cb;

// Post a callback (with its point) to the GUI thread, out of the hook proc.
void postToGui(const std::function<void(QPoint)>& cb, const POINT& pt)
{
    if (!cb)
        return;
    const QPoint p(pt.x, pt.y);
    auto fn = cb;
    QMetaObject::invokeMethod(QCoreApplication::instance(), [fn, p] { fn(p); },
                              Qt::QueuedConnection);
}
void postToGui(const std::function<void()>& cb)
{
    if (!cb)
        return;
    auto fn = cb;
    QMetaObject::invokeMethod(QCoreApplication::instance(), [fn] { fn(); },
                              Qt::QueuedConnection);
}

enum class State { Idle, Pressed, Handed };
State s_state = State::Idle;
POINT s_start = {0, 0};                          // press point, physical desktop px

bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// Exactly the required modifiers must be held — no more, no less — so e.g. a
// Ctrl-only trigger doesn't fire on a Ctrl+Shift drag.
bool requiredHeld()
{
    const bool ctrl  = keyDown(VK_CONTROL);
    const bool alt   = keyDown(VK_MENU);
    const bool win   = keyDown(VK_LWIN) || keyDown(VK_RWIN);
    const bool shift = keyDown(VK_SHIFT);
    return ctrl  == s_required.testFlag(Qt::ControlModifier)
        && alt   == s_required.testFlag(Qt::AltModifier)
        && win   == s_required.testFlag(Qt::MetaModifier)
        && shift == s_required.testFlag(Qt::ShiftModifier);
}

bool pastDragThreshold(const POINT& p)
{
    return std::abs(p.x - s_start.x) > GetSystemMetrics(SM_CXDRAG)
        || std::abs(p.y - s_start.y) > GetSystemMetrics(SM_CYDRAG);
}

// Re-issue the swallowed click so a plain modifier+click still reaches the app
// underneath. Our own injected events re-enter the hook flagged LLMHF_INJECTED and
// pass straight through.
void replayClick()
{
    INPUT in[2] = {};
    in[0].type = INPUT_MOUSE;  in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE;  in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
}

LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION) {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        const bool injected = (ms->flags & LLMHF_INJECTED) != 0;
        if (!injected) {
            switch (wParam) {
            case WM_LBUTTONDOWN:
                if (s_state == State::Idle && requiredHeld()) {
                    s_state = State::Pressed;
                    s_start = ms->pt;
                    postToGui(s_cb.onPress, s_start);   // build the overlay now (freeze on press)
                    return 1;                    // swallow the press
                }
                break;
            case WM_MOUSEMOVE:
                if (s_state == State::Pressed) {
                    if (pastDragThreshold(ms->pt)) {
                        s_state = State::Handed;
                        postToGui(s_cb.onDrag, s_start);   // reveal the overlay, seeded at press
                        // fall through to pass this move on: the overlay is topmost under
                        // the cursor and shows live feedback during the drag.
                        break;
                    }
                    return 1;                    // swallow pre-threshold jitter
                }
                break;
            case WM_LBUTTONUP:
                if (s_state == State::Pressed) { // never dragged -> it was a click
                    s_state = State::Idle;
                    postToGui(s_cb.onCancel);    // discard the pre-built overlay
                    replayClick();
                    return 1;                    // swallow the physical up (re-injected)
                }
                if (s_state == State::Handed) {   // drag finished: the overlay finalizes on
                    s_state = State::Idle;        // this release (Qt may not deliver it natively)
                    postToGui(s_cb.onRelease, ms->pt);
                }
                break;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

} // namespace

WinQuickDragGesture::~WinQuickDragGesture() { disarm(); }

void WinQuickDragGesture::arm(Qt::KeyboardModifiers required, Callbacks cb)
{
    disarm();                                    // never stack hooks
    if (required == Qt::NoModifier || !cb.onDrag)
        return;
    s_required = required;
    s_cb = std::move(cb);
    s_state = State::Idle;
    s_hook = SetWindowsHookExW(WH_MOUSE_LL, hookProc, GetModuleHandleW(nullptr), 0);
}

void WinQuickDragGesture::disarm()
{
    if (s_hook) {
        UnhookWindowsHookEx(s_hook);
        s_hook = nullptr;
    }
    s_required = Qt::NoModifier;
    s_cb = Callbacks{};
    s_state = State::Idle;
}
