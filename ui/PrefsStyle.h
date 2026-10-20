#pragma once

#include <QColor>
#include <QString>

// Shared loader for the Preferences theme (res/preferences.qss). Reads the file
// next to the executable and rewrites it twice before handing it to setStyleSheet():
//
//   1. Every `font-size: Npx` rule is scaled so the whole dialog's text tracks the
//      current application font (the interface font): each size is multiplied by
//      (effective app font pt / 9pt design base). Non-font px values (padding, radii,
//      indicator sizes) are left untouched.
//   2. Every `@token` is replaced with a colour from the palette for the active
//      scheme. The stylesheet holds no colour literals of its own — light and dark
//      are two value tables here, over one set of rules, so they cannot drift apart.
//
// Returns an empty string when the file is missing (callers treat that as "no
// stylesheet").
namespace PrefsStyle {

// Which palette the tokens resolve against. `interface/theme` selects it as
// "auto" (default) | "light" | "dark"; Auto asks Qt what the OS is currently using.
enum class Scheme { Light, Dark };

QString load();

// The current UI accent (interface/accentColor, default #2d7dff). load() injects this
// into the stylesheet; other widgets that paint an accent can share the same source.
QColor accentColor();

// `interface/theme` resolved to a concrete palette — "auto" asks QStyleHints for the
// OS scheme. Widgets that paint themselves (rather than being styled by the QSS) can
// use this to pick colours that match the dialogs.
Scheme effectiveScheme();

}
