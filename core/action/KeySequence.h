#pragma once

#include <QtCore/qnamespace.h>

// A parsed, platform-neutral key combination. The platform layer (e.g.
// platform/win) converts this to OS codes (VK / MOD_*). Kept in core/ so the
// codec is headless-testable and free of any Win32 dependency.
struct KeySequence {
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    int key = 0;            // a Qt::Key value
    bool valid = false;
};
