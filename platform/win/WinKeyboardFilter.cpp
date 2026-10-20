#include "platform/win/WinKeyboardFilter.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

namespace {

// Low-level keyboard hooks are process-global and only one snip runs at a time,
// so the C-style hook proc reads a single set of file-static state.
HHOOK s_hook = nullptr;
platform::IKeyboardFilter::Options s_opt;

// Physical Win-key state, tracked to tell a lone tap (Start-menu trigger) from a
// Win+X shortcut. Reset on each Win keydown.
bool s_lwinDown = false, s_lwinUsed = false;
bool s_rwinDown = false, s_rwinUsed = false;

// An unassigned virtual key. Injected between a lone Win down/up pair it makes the
// shell see the Win press as "used", so the Start menu stays closed — the same
// inert filler AutoHotkey uses for remapped Win keys. Genuine Win+X shortcuts,
// which already contain a real intervening key, are left untouched.
constexpr WORD kInertVk = 0xE8;

// Release the swallowed lone Win keyup, prefaced by the inert filler so the shell
// doesn't open the Start menu. Our own injected events re-enter the hook flagged
// LLKHF_INJECTED and pass straight through.
void suppressLoneWin(DWORD winVk)
{
    INPUT in[3] = {};
    in[0].type = INPUT_KEYBOARD;  in[0].ki.wVk = kInertVk;
    in[1].type = INPUT_KEYBOARD;  in[1].ki.wVk = kInertVk;  in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    in[2].type = INPUT_KEYBOARD;  in[2].ki.wVk = WORD(winVk); in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(3, in, sizeof(INPUT));
}

LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool injected = (k->flags & LLKHF_INJECTED) != 0;
        const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool up   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);
        const DWORD vk  = k->vkCode;

        // --- Ignore simulated "Copy" ---------------------------------------
        // Swallow an injected Ctrl+C / Ctrl+Alt+C (down and up) so a mouse
        // driver / macro can't silently complete the snip; a real keyboard
        // Ctrl+C is not injected, so it still reaches the overlay and copies.
        if (s_opt.ignoreSimCopy && injected && vk == 'C'
            && (GetAsyncKeyState(VK_CONTROL) & 0x8000))
            return 1;

        // --- Ignore lone Win key (left / right) ----------------------------
        // Only physical events drive the bookkeeping; our own injected filler
        // and re-issued Win-up fall through to CallNextHookEx untouched.
        if (!injected) {
            const bool isLWin = (vk == VK_LWIN);
            const bool isRWin = (vk == VK_RWIN);
            const bool watchL = s_opt.ignoreWinLeft  && isLWin;
            const bool watchR = s_opt.ignoreWinRight && isRWin;

            if (watchL || watchR) {
                bool& winDown = isLWin ? s_lwinDown : s_rwinDown;
                bool& winUsed = isLWin ? s_lwinUsed : s_rwinUsed;
                if (down) {
                    winDown = true;
                    winUsed = false;         // let it pass so Win+X can still form
                } else if (up) {
                    winDown = false;
                    if (!winUsed) {          // lone tap: kill only the Start menu
                        suppressLoneWin(vk);
                        return 1;            // swallow the physical up (re-injected)
                    }
                }
            } else if (down) {
                // Any other physical key press marks a held Win as part of a combo.
                if (s_lwinDown) s_lwinUsed = true;
                if (s_rwinDown) s_rwinUsed = true;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

} // namespace

WinKeyboardFilter::~WinKeyboardFilter() { uninstall(); }

void WinKeyboardFilter::install(const Options& opt)
{
    uninstall();                                  // never stack hooks
    if (!opt.ignoreSimCopy && !opt.ignoreWinLeft && !opt.ignoreWinRight)
        return;                                   // nothing to enforce
    s_opt = opt;
    s_lwinDown = s_lwinUsed = s_rwinDown = s_rwinUsed = false;
    s_hook = SetWindowsHookExW(WH_KEYBOARD_LL, hookProc, GetModuleHandleW(nullptr), 0);
}

void WinKeyboardFilter::uninstall()
{
    if (s_hook) {
        UnhookWindowsHookEx(s_hook);
        s_hook = nullptr;
    }
    s_opt = Options{};
}
