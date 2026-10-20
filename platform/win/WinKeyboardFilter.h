#pragma once

#include "platform/platform.h"

// Windows IKeyboardFilter backend: a WH_KEYBOARD_LL hook installed only while a
// snip overlay is active. Enforces the two Snip → Keys policies that can't be
// done from Qt key events:
//   * ignoreSimCopy — swallows an *injected* Ctrl+C / Ctrl+Alt+C (LLKHF_INJECTED)
//     so a mouse driver / macro tool can't silently complete the snip, while a
//     real keyboard Ctrl+C still copies;
//   * ignoreWinLeft/Right — suppresses only the *lone* Left/Right Windows key
//     (its Start-menu trigger); genuine Win+X shortcuts pass through.
// Low-level hooks are process-global and only one snip runs at a time, so a
// single static self-pointer backs the C-style hook proc.
// (windows.h is kept out of this header.)
class WinKeyboardFilter : public platform::IKeyboardFilter {
public:
    WinKeyboardFilter() = default;
    ~WinKeyboardFilter() override;

    void install(const Options& opt) override;
    void uninstall() override;
};
