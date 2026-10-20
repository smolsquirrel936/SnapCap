#pragma once

#include "platform/platform.h"

// Windows backing for a pin window: always-on-top, click-through, and per-pixel
// opacity via Win32 window styles on the widget's native HWND. The HWND is kept
// out of the header (stored as WId, cast in the .cpp) so includers don't pull in
// <windows.h>. See claude-plan/Architecture-FileMap.md (Stage 5). Stage 5.
class WinPinSurface : public platform::IPinSurface {
public:
    explicit WinPinSurface(WId window);

    void setAlwaysOnTop(bool on) override;
    void setClickThrough(bool on) override;
    void setOpacity(qreal value) override;

private:
    WId m_wid;
};
