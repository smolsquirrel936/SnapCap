#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

#include "core/annotation/Annotation.h"   // ToolId

// Pure geometry for "shifted" shape drawing (hold Shift): rect/ellipse → square/
// circle, line/arrow → snap to the nearest 45° direction. Kept out of the shape
// classes so the constraint math is headlessly unit-testable (tst_shifted_shapes)
// and reused by the overlay's drawing state machine. Stage 4 (Features.md §2).
namespace ShapeConstraints {

// Constrain the dragged corner to make a square (equal extent on both axes),
// preserving the drag direction. `anchor` is the fixed first corner.
QPoint squareCorner(const QPoint& anchor, const QPoint& cur);

// Snap the segment anchor→cur to the nearest of the 8 principal directions
// (0/45/90/…°), keeping the cursor's distance along that direction. Used for
// horizontal/vertical/diagonal lines & arrows.
QPoint snapLine(const QPoint& anchor, const QPoint& cur);

// Constrain `cur` so the *inclusive* rect anchor->cur keeps the aspect `ratio`
// (w:h), scaling it down as needed so the rect never leaves `bounds` (pass a
// null QRect for no clamp). `anchor` is the fixed corner. Unlike squareCorner()
// this works in inclusive-rect units (width = right-left+1), matching
// SelectionModel, so a locked drag reproduces the ratio exactly. Used by the
// snip overlay's Shift (keep ratio) / Ctrl (square) corner resize.
QPoint ratioCorner(const QPoint& anchor, const QPoint& cur,
                   const QSize& ratio, const QRect& bounds);

// The modifier lock for a two-point shape drag, shared by the snip overlay and the
// pin editor so they can't drift apart: Ctrl gives the rect family equal sides,
// Shift snaps a line/arrow segment to the nearest 45°. Returns `cur` unchanged when
// no modifier applies to `tool`.
QPoint constrainedEnd(annotation::ToolId tool, const QPoint& anchor,
                      const QPoint& cur, Qt::KeyboardModifiers mods);

} // namespace ShapeConstraints
