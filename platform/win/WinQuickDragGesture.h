#pragma once

#include "platform/platform.h"

// Windows IQuickDragGesture backend: a WH_MOUSE_LL hook, armed only while the app
// is idle, that turns a "hold modifier(s) + left-drag" desktop gesture into a snip.
//
// State machine (see WinQuickDragGesture.cpp): Idle -> Pressed -> Handed.
//   * WM_LBUTTONDOWN with exactly the required modifiers held: swallow the press,
//     record the start point, and post onPress (so the overlay's desktop freeze
//     happens now, on the press — not mid-drag);
//   * once the pointer moves past the system drag threshold: post onDrag and hand
//     off (pass events through so the revealed overlay, topmost under the cursor,
//     shows live feedback), then post onRelease on the button-up;
//   * a button-up before the threshold means it was a plain click: post onCancel and
//     replay the swallowed click via SendInput so normal modifier+clicks pass through.
// Low-level hooks are process-global and only one gesture runs at a time, so a
// single set of file-static state backs the C-style hook proc.
// (windows.h is kept out of this header.)
class WinQuickDragGesture : public platform::IQuickDragGesture {
public:
    WinQuickDragGesture() = default;
    ~WinQuickDragGesture() override;

    void arm(Qt::KeyboardModifiers required, Callbacks cb) override;
    void disarm() override;
};
