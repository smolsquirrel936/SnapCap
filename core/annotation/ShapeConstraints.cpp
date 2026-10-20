#include "core/annotation/ShapeConstraints.h"

#include <QtMath>
#include <cmath>

namespace ShapeConstraints {

QPoint squareCorner(const QPoint& anchor, const QPoint& cur)
{
    const int dx = cur.x() - anchor.x();
    const int dy = cur.y() - anchor.y();
    const int side = qMax(qAbs(dx), qAbs(dy));
    const int sx = dx < 0 ? -1 : 1;
    const int sy = dy < 0 ? -1 : 1;
    return anchor + QPoint(sx * side, sy * side);
}

QPoint snapLine(const QPoint& anchor, const QPoint& cur)
{
    const double dx = cur.x() - anchor.x();
    const double dy = cur.y() - anchor.y();
    const double len = std::hypot(dx, dy);
    if (len <= 0.0)
        return anchor;

    constexpr double step = M_PI / 4.0;                 // 45°
    const double snapped = std::round(std::atan2(dy, dx) / step) * step;
    return anchor + QPoint(int(std::lround(len * std::cos(snapped))),
                           int(std::lround(len * std::sin(snapped))));
}

QPoint ratioCorner(const QPoint& anchor, const QPoint& cur,
                   const QSize& ratio, const QRect& bounds)
{
    const double rw = ratio.width(), rh = ratio.height();
    if (rw <= 0.0 || rh <= 0.0)
        return cur;

    const int sx = cur.x() < anchor.x() ? -1 : 1;    // drag direction, kept
    const int sy = cur.y() < anchor.y() ? -1 : 1;
    // Inclusive extents: a drag that hasn't left the anchor is still 1px wide.
    const double w0 = qAbs(cur.x() - anchor.x()) + 1;
    const double h0 = qAbs(cur.y() - anchor.y()) + 1;

    // Scale the ratio to the axis the cursor pulled furthest (so the constrained
    // corner tracks the dominant direction, as squareCorner does).
    double s = qMax(w0 / rw, h0 / rh);

    // Cap the scale — not the axes independently — so hitting a bound stops the
    // drag with the ratio still exact instead of cropping one side.
    if (!bounds.isNull()) {
        const double maxW = sx > 0 ? bounds.right() - anchor.x() + 1
                                   : anchor.x() - bounds.left() + 1;
        const double maxH = sy > 0 ? bounds.bottom() - anchor.y() + 1
                                   : anchor.y() - bounds.top() + 1;
        s = qMin(s, qMin(maxW / rw, maxH / rh));
    }

    const int w = qMax(1, int(std::lround(rw * s)));
    const int h = qMax(1, int(std::lround(rh * s)));
    return anchor + QPoint(sx * (w - 1), sy * (h - 1));
}

QPoint constrainedEnd(annotation::ToolId tool, const QPoint& anchor,
                      const QPoint& cur, Qt::KeyboardModifiers mods)
{
    using annotation::ToolId;
    // Ctrl → equal sides (square / circle) for the rectangle-drag family.
    if ((mods & Qt::ControlModifier)
        && (tool == ToolId::Rect || tool == ToolId::Ellipse || tool == ToolId::Mosaic
            || tool == ToolId::Blur || tool == ToolId::EraserRect || tool == ToolId::MarkerRect))
        return squareCorner(anchor, cur);
    // Shift → snap the segment to the nearest 45° (so it locks horizontal/vertical).
    if ((mods & Qt::ShiftModifier) && (tool == ToolId::Line || tool == ToolId::Arrow))
        return snapLine(anchor, cur);
    return cur;
}

} // namespace ShapeConstraints
