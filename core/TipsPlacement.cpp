#include "core/TipsPlacement.h"

namespace TipsPlacement {

QRect rectForCorner(const QRect& host, const QSize& panel, Corner corner, int margin)
{
    const int w = panel.width(), h = panel.height();
    int x = host.left() + margin;                 // default: left edge
    int y = host.top() + margin;                  // default: top edge
    if (corner == Corner::BottomLeft || corner == Corner::BottomRight)
        y = host.bottom() - margin - h + 1;
    if (corner == Corner::BottomRight || corner == Corner::TopRight)
        x = host.right() - margin - w + 1;
    return QRect(x, y, w, h);
}

QRect place(const QRect& host, const QSize& panel, const QVector<QRect>& obstacles, int margin,
            Corner& chosen)
{
    // Order matters: first non-overlapping corner wins (BL -> BR -> TL -> TR).
    static const Corner order[] = {Corner::BottomLeft, Corner::BottomRight,
                                   Corner::TopLeft, Corner::TopRight};
    Corner best = Corner::BottomLeft;
    qint64 bestArea = -1;
    for (Corner c : order) {
        const QRect r = rectForCorner(host, panel, c, margin);
        qint64 covered = 0;
        for (const QRect& o : obstacles) {
            if (!o.isValid())                       // invalid/empty obstacles don't count
                continue;
            const QRect hit = r.intersected(o);
            if (!hit.isEmpty())
                covered += qint64(hit.width()) * hit.height();
        }
        if (covered == 0) {                         // fully clear: take it, look no further
            chosen = c;
            return r;
        }
        if (bestArea < 0 || covered < bestArea) {   // ties keep the earlier corner
            bestArea = covered;
            best = c;
        }
    }
    // No corner is fully clear. That is the NORMAL case while hovering before a drag: the
    // auto-detect candidate is a whole window, often maximized, so it covers all four
    // corners at once. Falling back to a fixed corner there would let that one big rect
    // swamp the small obstacles that actually matter — the cursor and the magnifier sit on
    // the panel and it would never move. Taking the least-covered corner keeps them
    // decisive, since a screen-filling obstacle costs every corner the same.
    chosen = best;
    return rectForCorner(host, panel, best, margin);
}

QRect place(const QRect& host, const QSize& panel, const QVector<QRect>& obstacles, int margin)
{
    Corner ignored;
    return place(host, panel, obstacles, margin, ignored);
}

} // namespace TipsPlacement
