#pragma once

#include <QPoint>
#include <QtGlobal>

// Anti-misclick gate for the snip region drag: a press stays a *click* (which adopts
// the highlighted window/element) until it has travelled far enough to be a deliberate
// drag. Without it a pixel of hand-jitter while clicking turns the gesture into a
// useless few-pixel region.
//
// Distance alone can't decide it: a slop radius wide enough to absorb a real slip would
// also forbid deliberately dragging a small region. So the arming distance *decays* the
// longer the button is held — a quick gesture is a click unless it is big, a held one is
// a drag as soon as it moves at all. Time never promotes a motionless press to a drag.
//
//   elapsed <= windowMs   ->  slopPx      (default 30 logical px: absorb the slip)
//   elapsed <= kReleaseMs ->  kFloorPx    (deliberate but unhurried: a small region)
//   after that            ->  kMinPx      (held a full second: any real movement counts)
//
// Callers latch the result: once armed, a gesture stays a drag even if it returns to the
// press point. `slopPx == 0` disables the gate entirely (the first move arms the drag),
// which is the pre-gate behaviour.
namespace click_slop {

inline constexpr int kFloorPx   = 10;    // arming distance once the click window has passed
inline constexpr int kReleaseMs = 1000;  // hold this long and the slop drops to kMinPx
inline constexpr int kMinPx     = 2;     // still ignores the 1px twitch of a hesitant click

// The distance (per axis) a press must travel to count as a drag, at `elapsedMs` after
// the press. Never increases with time, so latching can't fight it.
inline int clickSlopThreshold(qint64 elapsedMs, int slopPx, int windowMs)
{
    if (elapsedMs > kReleaseMs)
        return qMin(slopPx, kMinPx);
    if (elapsedMs > windowMs)
        return qMin(slopPx, kFloorPx);
    return slopPx;
}

// Has the press travelled far enough to be a drag? Per-axis (not diagonal distance), so
// a slip of (n, n) is still a click — the same convention Windows' SM_CXDRAG uses.
inline bool exceedsClickSlop(const QPoint& press, const QPoint& now,
                             qint64 elapsedMs, int slopPx, int windowMs)
{
    const int t = clickSlopThreshold(elapsedMs, slopPx, windowMs);
    return qAbs(now.x() - press.x()) >= t || qAbs(now.y() - press.y()) >= t;
}

}  // namespace click_slop
