#pragma once

#include <QString>
#include <functional>

// Display logic for the snip code hint (the third box on the position/size
// readout row above the selection). Pure logic — no widgets, no font classes —
// so the elision is unit-tested headlessly: callers hand in a `Measure` that
// reports the pixel width of a candidate string (the view wraps QFontMetrics).
// The decoding itself lives behind platform::IBarcodeDecode.
// See claude-plan/Features.md (snip code hint).
namespace BarcodeHint {

// Width of a candidate string in the hint's font, in pixels.
using Measure = std::function<int(const QString&)>;

// Can Ctrl+click hand this payload to a browser? Only http/https qualify — a QR
// code carrying WiFi credentials, a vCard or plain text is copyable but not
// openable, and mailto:/tel: are deliberately excluded (the user chose "copy
// only" for everything that isn't a web link).
bool isOpenableUrl(const QString& payload);

// The payload reduced to one line: runs of whitespace (vCard / WiFi payloads are
// multi-line) collapse to single spaces and the ends are trimmed. Used for both
// the elided box text and the hover tooltip, so neither can break the layout.
QString singleLine(const QString& payload);

// Fit the payload into `maxWidthPx`, preferring to keep the domain and the tail:
//
//   https://github.com/anthropics/claude-code/issues/12345
//     -> "github.com/…issues/12345"
//
// Stages, each used only if the previous one doesn't fit:
//   1. the payload as-is
//   2. the scheme dropped ("https://" / "http://")
//   3. host + "/…" + as much of the tail as fits (the documented look)
//   4. a plain tail-clipped string with a trailing "…" — for budgets too small
//      for even "host/…", and for non-URL payloads, which have no host to keep
//
// Returns an empty string when not even one character plus the ellipsis fits.
QString elide(const QString& payload, int maxWidthPx, const Measure& measure);

} // namespace BarcodeHint
