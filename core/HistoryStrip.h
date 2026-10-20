#pragma once

// Pure windowing math for the in-snip history strip (Stage 6). The strip shows
// thumbnails of recent snips; when there are more entries than fit on screen it
// shows a sliding window that keeps the selected thumbnail visible (centred when
// possible). This is the only non-trivial logic behind the strip, so it lives here
// as a pure function → headlessly unit-tested (tst_history_strip). The rendering /
// hit-testing lives in ui/ (SnipOverlay + SnipScreenView).
namespace HistoryStrip {

struct Window {
    int start = 0;   // index of the first visible thumbnail
    int len   = 0;   // number of visible thumbnails
};

// Compute the visible slice of `count` thumbnails around `selected`, showing at
// most `maxVisible` at once. The window is centred on the selection where it can
// be, then clamped so it never runs past either end. Returns {0,0} when empty.
Window window(int count, int selected, int maxVisible);

}  // namespace HistoryStrip
