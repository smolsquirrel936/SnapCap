#pragma once

#include <QImage>
#include <QVector>
#include <climits>

// Stitches successive grabs of a FIXED screen region into one tall image as the user
// scrolls the window underneath it. Pure logic — no GUI, no platform: the caller does
// the grabbing and the drawing.
//
// A scrolled window is a pure VERTICAL TRANSLATION of the previous frame, so a frame is
// placed by finding the offset that best explains it in terms of the last accepted one.
// Matching is two-stage because a one-stage search is either too slow or too brittle:
//   1. row hashes narrow ~2H offsets down to a shortlist (cheap, exact-equality);
//   2. each shortlisted offset is re-scored in pixel space with a per-row tolerance,
//      which is what lets a moving scrollbar thumb — a handful of differing pixels on
//      an otherwise identical row — stop invalidating the whole row.
// Both stages score an offset by its LONGEST CONTIGUOUS RUN of matching rows rather
// than by a total or a fraction: a real capture region usually holds bands that do not
// scroll (tabs, an address bar, a pinned header), and under a "most rows must agree"
// rule those bands and the scrolling body veto each other's offset until nothing is
// accepted at all.
namespace scroll {

class ScrollStitcher {
public:
    enum class Result {
        Appended,    // matched below: the canvas grew downward
        Prepended,   // matched above: the canvas grew upward
        Unchanged,   // matched, but added no new rows (no movement, or the frame lies
                     // wholly inside what is already captured). liveOffset() may still
                     // have moved — scrolling back through captured content lands here.
        NoOverlap,   // nothing matched: the frame is dropped, the caller warns the user
        Capped,      // accepting it would exceed maxHeight: dropped, canvas untouched
        Rejected,    // null or wrong-sized frame (a failed grab) — not a user-facing state
    };

    static constexpr int kDefaultMaxHeight = 30000;

    explicit ScrollStitcher(int maxHeight = kDefaultMaxHeight);

    // `frame` must be the region's size (after the first one, which fixes that size).
    Result addFrame(const QImage& frame);

    // Which side of the live frame to throw away. The live frame itself always survives:
    // it is what the user aimed the cut with, and it is the reference the next frame is
    // matched against (see trim()).
    enum class Side {
        Above,   // rows before the live frame — the top of a ↕ capture, the left of a ↔ one
        Below,   // rows after it
    };
    // Discard everything on one side of the live frame and return how many rows went (0
    // when that side was already empty). The cut is plain removal: `m_lastFrame` and its
    // hashes are untouched, so the incremental matcher carries on exactly as before, and
    // scrolling back into the discarded ground simply re-captures it the ordinary way.
    int trim(Side side);

    const QImage& canvas() const { return m_canvas; }
    // The last frame accepted — what the next one is matched against.
    const QImage& lastFrame() const { return m_lastFrame; }
    bool isEmpty() const { return m_canvas.isNull(); }
    // Y of the last accepted frame's top row within the canvas — drives the preview
    // strip's viewport indicator.
    int liveOffset() const { return m_liveOffset; }
    int frameCount() const { return m_frames; }
    void reset();

private:
    struct Match {
        int  dy = 0;
        bool ok = false;
    };
    // Best vertical offset of `frame` relative to the last accepted frame.
    Match findOffset(const QImage& frame, const QVector<quint64>& hashes) const;
    // Longest contiguous run of overlapping rows that match within tolerance, for one
    // candidate offset. Runs rather than totals, so a non-scrolling band (app chrome,
    // a pinned header) cannot veto the offset the scrolling body agrees on.
    int pixelRun(const QImage& frame, int dy) const;
    // Longest run of matching rows between `frame` and the CANVAS when the frame's top
    // row sits at canvas row `top`. Same tolerant per-row test as pixelRun.
    int pixelRunAgainstCanvas(const QImage& frame, int top) const;
    // Locate `frame` anywhere in the canvas, without reference to the previous frame.
    // Returns kNoPosition when it cannot be placed confidently.
    static constexpr int kNoPosition = INT_MIN;
    int relocate(const QImage& frame, const QVector<quint64>& hashes) const;

    QImage m_canvas;                  // Format_RGB32, grows vertically
    QImage m_lastFrame;               // last accepted frame — the matching reference
    QVector<quint64> m_lastHashes;    // its row hashes
    // Strip hashes of every canvas row, grown alongside the canvas from the frame hashes
    // already computed. This is what lets a frame be found in the canvas outright when it
    // cannot be matched against its predecessor — a big jump, most often a flick back up.
    QVector<quint64> m_canvasHashes;
    int m_liveOffset = 0;
    int m_frames = 0;
    int m_maxHeight;
};

} // namespace scroll
