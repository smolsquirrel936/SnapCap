#pragma once

#include <QString>
#include "core/action/KeySequence.h"

// Pure conversion between portable key-sequence strings ("Ctrl+Shift+S", "F1")
// and the neutral KeySequence struct. No Qt GUI / no Win32 — unit-tested in
// test/tst_keyseq_codec.cpp.
namespace KeySequenceCodec {

// Parse "Ctrl+Shift+S" -> KeySequence. Modifier order is irrelevant; tokens are
// case-insensitive. Returns an invalid KeySequence on empty/unknown/multi-key.
KeySequence parse(const QString& text);

// Canonical string form: modifiers in Ctrl, Shift, Alt, Win order + key name.
QString toString(const KeySequence& seq);

// parse() then toString(); "" if the input is not a valid sequence. Used to
// normalize keys before storing/comparing.
QString normalize(const QString& text);

// --- Modifier-only triggers (Quick snip hold-and-drag) ----------------------
// A modifier-only string ("Ctrl+Alt") is not a valid KeySequence (it has no key),
// so it needs its own conversions. Used by the Quick-snip trigger box and the app.

// "Ctrl+Alt" -> Qt::ControlModifier|Qt::AltModifier. Recognizes the same modifier
// tokens as parse() (Ctrl/Shift/Alt/Win + aliases); non-modifier tokens are ignored.
Qt::KeyboardModifiers modsFromString(const QString& text);

// Canonical string form of a modifier set: Ctrl, Shift, Alt, Win order (no key).
// Empty for Qt::NoModifier.
QString modsToString(Qt::KeyboardModifiers mods);

} // namespace KeySequenceCodec
