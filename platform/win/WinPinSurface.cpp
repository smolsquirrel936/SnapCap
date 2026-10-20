#include "platform/win/WinPinSurface.h"

#include <QtGlobal>
#include <windows.h>

namespace {
inline HWND asHwnd(WId w) { return reinterpret_cast<HWND>(w); }
}

WinPinSurface::WinPinSurface(WId window) : m_wid(window) {}

void WinPinSurface::setAlwaysOnTop(bool on)
{
    SetWindowPos(asHwnd(m_wid), on ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void WinPinSurface::setClickThrough(bool on)
{
    HWND h = asHwnd(m_wid);
    LONG_PTR ex = GetWindowLongPtr(h, GWL_EXSTYLE);
    // Qt's frameless translucent window is already WS_EX_LAYERED; toggling
    // WS_EX_TRANSPARENT alone makes the mouse pass through (or stop) without
    // disturbing the per-pixel alpha that LAYERED provides.
    if (on)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    SetWindowLongPtr(h, GWL_EXSTYLE, ex);
}

void WinPinSurface::setOpacity(qreal value)
{
    // PinWindow drives opacity through QWidget::setWindowOpacity (cross-platform);
    // this is provided for interface completeness / non-Qt callers.
    HWND h = asHwnd(m_wid);
    LONG_PTR ex = GetWindowLongPtr(h, GWL_EXSTYLE);
    SetWindowLongPtr(h, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    const BYTE a = static_cast<BYTE>(qBound(0, int(value * 255.0), 255));
    SetLayeredWindowAttributes(h, 0, a, LWA_ALPHA);
}
