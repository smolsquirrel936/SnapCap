#pragma once

#include <QRect>
#include <QSize>
#include <QVector>

// Pure placement math for the snip key-binding tips panel. Given a host rect (the
// screen the panel lives on) and whatever is currently on it, pick a corner so the
// panel stays out of the way. No Qt GUI — unit-tested in test/tst_tips_placement.cpp.
namespace TipsPlacement {

enum class Corner { BottomLeft, BottomRight, TopLeft, TopRight };

// The panel rect for one corner: `panel` sized, inset by `margin` from that corner
// of `host`. Coordinates are in `host`'s own space.
QRect rectForCorner(const QRect& host, const QSize& panel, Corner corner, int margin);

// Choose the panel rect. Tries corners in order BottomLeft, BottomRight, TopLeft,
// TopRight and returns the first whose rect intersects NONE of `obstacles` — the
// selection, the auto-detect candidate, the cursor, the magnifier, the toolbars.
// Empty/invalid obstacles are ignored. When no corner is fully clear (routine: the
// auto-detect candidate is a whole window and can cover all four), returns the corner
// with the SMALLEST total overlap area, ties going to the earlier corner in that order
// — so one screen-filling obstacle, which costs every corner alike, can't drown out the
// small ones. `obstacles` share `host`'s coordinate space.
QRect place(const QRect& host, const QSize& panel, const QVector<QRect>& obstacles, int margin);

// place(), also reporting which corner was chosen.
QRect place(const QRect& host, const QSize& panel, const QVector<QRect>& obstacles, int margin,
            Corner& chosen);

} // namespace TipsPlacement
