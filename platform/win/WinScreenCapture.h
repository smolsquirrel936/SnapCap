#pragma once

#include "platform/platform.h"

// Windows IScreenCapture backend using GDI BitBlt over the virtual desktop.
// Returns device-pixel images (the process is per-monitor-DPI aware via Qt).
// DXGI Desktop Duplication is deferred (Stage 7) behind this same interface, as an
// opt-in backend only (`general/capture_backend` = bitblt (default) | dxgi, read
// here rather than in core/): BitBlt stays the default, and DDA falls back to it on any
// error (unsupported / access-lost / acquire timeout). It fixes black hardware-overlay
// (video) captures -- it is not a faster path for one-shot grabs.
class WinScreenCapture : public platform::IScreenCapture {
public:
    QImage grabVirtualDesktop() override;
    QImage grabRect(const QRect& rect) override;
    platform::CursorImage grabCursor() override;
};
