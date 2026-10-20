#pragma once

#include "platform/platform.h"

// Windows IWheelWatch backend: a WH_MOUSE_LL hook that reports WM_MOUSEWHEEL and
// WM_MOUSEHWHEEL and passes them straight on.
//
// This is the deliberate opposite of WinQuickDragGesture, which swallows the events
// it acts on. Here the window under the cursor MUST keep scrolling — the scrolling
// capture session is watching it scroll — so the hook proc never returns non-zero.
// Injected events are reported too: an automated scroll moves the content just as a
// physical one does, and the session only cares that the content moved.
// Low-level hooks are process-global and only one session runs at a time, so a single
// set of file-static state backs the C-style hook proc.
// (windows.h is kept out of this header.)
class WinWheelWatch : public platform::IWheelWatch {
public:
    WinWheelWatch() = default;
    ~WinWheelWatch() override;

    void arm(std::function<void(QPoint posCapturePx, bool injected,
                                bool horizontalWheel)> onWheel) override;
    void disarm() override;
};
