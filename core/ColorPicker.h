#pragma once

#include <QColor>
#include <QPoint>
#include <QString>
#include <QVector>

class QImage;

// Color sampling + formatting for the snip color-picker workflow (Stage 3).
// Pure logic (no widgets / Win32) so the conversions are unit-tested headlessly;
// the overlay samples from the frozen desktop image and copies the formatted
// string to the clipboard. Color parsing for paste-as-color (Stage 5) shares
// these formats. See claude-plan/Implementation-Plan.md (Stage 3).
namespace ColorPicker {

// Color readout formats offered by the magnifier / color picker. The Interface
// preferences (interface/magnifier/formats) choose which subset is shown; the
// codes below are the stable persisted identifiers (see codeFor/parseFormats).
enum class Format { Hex, Rgb, Hsv, Hsl, Hsi, Cmyk, Dec };

// Format a color as user-facing text:
//   Hex  -> "#rrggbb" (uppercased when uppercaseHex; '#' dropped when noHash)
//   Rgb  -> "r, g, b" (0-255, parseable by the Stage 5 color classifier)
//   Hsv  -> "h, s%, v%"  (hue 0-359; s/v as percentages)
//   Hsl  -> "h, s%, l%"
//   Hsi  -> "h, s%, i%"  (textbook HSI: intensity is the plain RGB mean and the
//                         hue is the arccos form, so it drifts a degree or two
//                         from the hexcone hue Hsv/Hsl report — that difference
//                         is the point of offering HSI separately)
//   Cmyk -> "c%, m%, y%, k%"
//   Dec  -> the 24-bit value r*65536 + g*256 + b as a decimal integer
// noHash/uppercaseHex only affect the Hex format; ignored for the others.
QString format(const QColor& c, Format f, bool uppercaseHex, bool noHash = false);

// Cycle to the next format in the full enum order, for the Shift toggle when no
// explicit enabled-set is in play. (The overlay cycles its own enabled subset.)
Format nextFormat(Format f);

// Stable string code <-> Format mapping used by the persisted settings CSV
// ("rgb,hex,hsv,..."). codeFor() returns the canonical lowercase code;
// parseFormats() maps a CSV into an ordered, de-duplicated Format list
// (unknown codes skipped), preserving the CSV order.
QString codeFor(Format f);
QVector<Format> parseFormats(const QString& csv);

// Short display name for the magnifier's per-format rows ("RGB", "HEX", "Dec",
// …). These are the universal abbreviations, matching the Preferences
// checkboxes; not translated, so they need no i18n key.
QString labelFor(Format f);

// Sample a pixel from a frozen device-pixel image; the point is clamped to the
// image bounds so callers never read out of range.
QColor sampleAt(const QImage& deviceImage, const QPoint& devicePx);

// Put text on the system clipboard. Returns false if no clipboard is available.
bool copyToClipboard(const QString& text);

// --- Color-selector dialog support (ui/ColorDialog) -------------------------
// Pure helpers kept in core so they're unit-testable headlessly.

// The 48-entry "basic colors" palette shown in the selector's top-left grid
// (8 columns x 6 rows), matching the classic system color dialog ordering.
QVector<QColor> basicPalette();

// Parse an HTML-style hex string into a color. Accepts "#rgb" / "#rrggbb" with
// or without the leading '#' (case-insensitive); surrounding whitespace is
// trimmed. Returns an invalid QColor on malformed input. Alpha is never parsed
// here (the dialog edits alpha separately), so the result is always opaque.
QColor parseHtml(const QString& s);

// Format a color as the dialog's HTML field text: "#rrggbb" (lowercase, no
// alpha) — mirrors format(..., Format::Hex, false) but named for the dialog.
QString toHtml(const QColor& c);

} // namespace ColorPicker
