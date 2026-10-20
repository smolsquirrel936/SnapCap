#pragma once

#include "platform/platform.h"

// Windows window/UI-element detection under the cursor (Stage 3).
//
// windowAt() walks the top-level window Z-order (skipping our own overlay,
// invisible and DWM-cloaked windows) and returns the first window containing the
// point. elementChainAt() resolves that target window, hit-tests it with MSAA
// (IAccessible::accHitTest — subtree-scoped, so our overlay is out of scope) and
// walks accParent upward to build the innermost-first ancestor chain; it falls
// back to a UI Automation Control-View descent, then to windowAt().
//
// Why MSAA and not UIA for the hit test: UIA has no subtree-scoped point API, and
// its global ElementFromPoint returns our own topmost frozen overlay every time
// (measured 207/207). A geometric descent is unsound for web content anyway: Firefox
// reports nodes with their full SCROLLABLE bounds (a body 7315 px tall in a 1278 px
// viewport), so the specific child is not nested in its parent and an area guard
// rejects it. That is why every chain entry is clamped to the window's frame.
//
// Not done yet: the accessibility calls run synchronously on the GUI thread (STA),
// whereas Microsoft recommends a dedicated MTA worker for desktop-wide UIA clients.
// Measured at 25–35 ms a hover, so it waits until hovering is seen to stall.
//
// All points/rects cross this boundary in GLOBAL LOGICAL coordinates (Qt screen
// space). Win32 reports physical pixels, so we map through a per-monitor affine
// table built from QScreen geometry/DPR. See claude-plan/Implementation-Plan.md.
class WinElementDetect : public platform::IElementDetect {
public:
    WinElementDetect();
    ~WinElementDetect() override;

    QRect windowAt(const QPoint& globalPos) override;
    QRect elementAt(const QPoint& globalPos) override;
    QList<QRect> elementChainAt(const QPoint& globalPos) override;

private:
    // Both public element queries; `wantChain` adds the accParent ancestor walk,
    // which is the expensive half. Returns innermost-first, always frame-terminated.
    QList<QRect> resolve(const QPoint& globalPos, bool wantChain);

    void ensureUia();

    void* m_uia = nullptr;        // IUIAutomation* (void* keeps this header COM-free)
    bool  m_comInited = false;    // we called CoInitializeEx on this thread
};
