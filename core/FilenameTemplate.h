#pragma once

#include <QString>
#include <QDateTime>
#include <QMap>

// Filename templating. A template is literal text with optional
// substitution spans; everything outside a span is copied verbatim.
//
//   "$...$"  — the text inside a pair of '$' is a Qt date/time format string
//              (see QDateTime::toString), substituted with `when` in that format.
//   "%...%"  — the text inside a pair of '%' names a system environment variable
//              (e.g. %os%, %computername%, %username%); it is substituted with
//              that variable's value. Lookup is case-insensitive on every
//              platform (Windows env names are). An unset variable resolves to
//              the empty string. "Special variables" (currently %title% /
//              %title:N%, N = max length) are NOT environment variables; they are
//              resolved from the `specials` map passed to the overload below
//              (keyed by lowercase base name, e.g. "title"). The plain expand()
//              passes no specials, so %title% resolves to empty there. A special
//              value has characters illegal in a filename (< > : " / \ | ? *, control
//              chars) replaced with '_' (and trailing dots/spaces trimmed) so e.g. a
//              window title can't break the save; the literal template is untouched.
//
//   "SnapCap_$yyyy-MM-dd_HH-mm-ss$.png"  ->  "SnapCap_2026-07-02_18-15-37.png"
//   "%username%_$yyyy$.png"              ->  "squirrel_2026.png"
//
// Rules:
//   - "$$" is an escaped literal '$'; "%%" is an escaped literal '%'.
//   - A dangling '$' or '%' with no closing partner is emitted literally.
// Pure Qt-Core logic — no widgets — so it is headless unit-testable in core/.
namespace FilenameTemplate {

// Expand with a set of "special variables" (e.g. {"title": "..."}). A %name% or
// %name:N% whose base name (lowercased) is a special key substitutes that value,
// truncated to N characters when the ":N" length suffix is present; every other
// %name% falls back to the environment lookup.
QString expand(const QString& templ, const QDateTime& when,
               const QMap<QString, QString>& specials);

// Overload with no special variables (%title% etc. resolve to empty).
QString expand(const QString& templ, const QDateTime& when);

// Convenience: expand with the current local date/time (no specials).
inline QString expandNow(const QString& templ) { return expand(templ, QDateTime::currentDateTime()); }

// Convenience: expand with the current local date/time and special variables.
inline QString expandNow(const QString& templ, const QMap<QString, QString>& specials)
{ return expand(templ, QDateTime::currentDateTime(), specials); }

} // namespace FilenameTemplate
