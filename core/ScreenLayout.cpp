#include "core/ScreenLayout.h"

#include <cmath>

ScreenLayout::ScreenLayout(QList<ScreenLayoutEntry> screens)
    : m_screens(std::move(screens))
{
    // Derive each screen's capture-per-logical scale from the real captured pixel
    // size (robust to the small rounding differences between logical*dpr and the
    // grabbed image), guarding against a degenerate zero-width logical geometry.
    for (ScreenLayoutEntry& e : m_screens) {
        const int lw = e.logicalGeometry.width();
        const int lh = e.logicalGeometry.height();
        e.scaleX = lw > 0 ? qreal(e.capturedPixelSize.width())  / lw : 1.0;
        e.scaleY = lh > 0 ? qreal(e.capturedPixelSize.height()) / lh : 1.0;
        if (e.scaleX <= 0.0) e.scaleX = 1.0;
        if (e.scaleY <= 0.0) e.scaleY = 1.0;
    }
}

QRect ScreenLayout::captureBounds() const
{
    QRect b;
    for (const ScreenLayoutEntry& e : m_screens)
        b = b.united(e.captureGeometry);
    return b;
}

QRegion ScreenLayout::coveredCaptureRegion() const
{
    QRegion r;
    for (const ScreenLayoutEntry& e : m_screens)
        r += e.captureGeometry;
    return r;
}

QPoint ScreenLayout::localLogicalToCapture(int i, QPointF local) const
{
    if (i < 0 || i >= m_screens.size())
        return local.toPoint();
    const ScreenLayoutEntry& e = m_screens.at(i);
    return QPoint(int(std::lround(e.captureGeometry.left() + local.x() * e.scaleX)),
                  int(std::lround(e.captureGeometry.top()  + local.y() * e.scaleY)));
}

QRect ScreenLayout::captureRectToLocalLogical(int i, const QRect& cap) const
{
    if (i < 0 || i >= m_screens.size())
        return cap;
    const ScreenLayoutEntry& e = m_screens.at(i);
    const qreal sx = e.scaleX, sy = e.scaleY;
    // Map both EXCLUSIVE edges independently, then subtract for the size. This avoids
    // the inclusive-edge (QRect::right()) off-by-one that fractional scales introduce.
    const long capL = e.captureGeometry.left(), capT = e.captureGeometry.top();
    const int l = int(std::lround((cap.left()               - capL) / sx));
    const int r = int(std::lround((cap.left() + cap.width() - capL) / sx));
    const int t = int(std::lround((cap.top()                - capT) / sy));
    const int b = int(std::lround((cap.top()  + cap.height() - capT) / sy));
    return QRect(l, t, r - l, b - t);
}

int ScreenLayout::indexForCapture(QPoint cap) const
{
    for (int i = 0; i < m_screens.size(); ++i)
        if (m_screens.at(i).captureGeometry.contains(cap))
            return i;
    int best = m_screens.isEmpty() ? -1 : 0;
    qint64 bestD = -1;
    for (int i = 0; i < m_screens.size(); ++i) {
        const QPoint c = m_screens.at(i).captureGeometry.center();
        const qint64 d = qint64(std::abs(c.x() - cap.x())) + std::abs(c.y() - cap.y());
        if (bestD < 0 || d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int ScreenLayout::indexForLogical(QPoint g) const
{
    for (int i = 0; i < m_screens.size(); ++i)
        if (m_screens.at(i).logicalGeometry.contains(g))
            return i;
    int best = m_screens.isEmpty() ? -1 : 0;
    qint64 bestD = -1;
    for (int i = 0; i < m_screens.size(); ++i) {
        const QPoint c = m_screens.at(i).logicalGeometry.center();
        const qint64 d = qint64(std::abs(c.x() - g.x())) + std::abs(c.y() - g.y());
        if (bestD < 0 || d < bestD) { bestD = d; best = i; }
    }
    return best;
}

qreal ScreenLayout::scaleAt(QPoint cap) const
{
    const int i = indexForCapture(cap);
    return i >= 0 ? m_screens.at(i).scaleX : 1.0;
}

QPoint ScreenLayout::captureToGlobalLogical(QPoint cap) const
{
    const int i = indexForCapture(cap);
    if (i < 0)
        return cap;
    const ScreenLayoutEntry& e = m_screens.at(i);
    return QPoint(int(std::lround(e.logicalGeometry.left() + (cap.x() - e.captureGeometry.left()) / e.scaleX)),
                  int(std::lround(e.logicalGeometry.top()  + (cap.y() - e.captureGeometry.top())  / e.scaleY)));
}

QPoint ScreenLayout::globalLogicalToCapture(QPointF g) const
{
    const int i = indexForLogical(g.toPoint());
    if (i < 0)
        return g.toPoint();
    const ScreenLayoutEntry& e = m_screens.at(i);
    return localLogicalToCapture(i, g - QPointF(e.logicalGeometry.topLeft()));
}

QRect ScreenLayout::captureRectToGlobalLogical(const QRect& cap) const
{
    const QPoint tl = captureToGlobalLogical(cap.topLeft());
    const QPoint br = captureToGlobalLogical(cap.topLeft() + QPoint(cap.width(), cap.height()));
    return QRect(tl, QSize(br.x() - tl.x(), br.y() - tl.y()));
}

QRect ScreenLayout::globalLogicalRectToCapture(const QRect& g) const
{
    const QPoint tl = globalLogicalToCapture(g.topLeft());
    const QPoint br = globalLogicalToCapture(g.topLeft() + QPoint(g.width(), g.height()));
    return QRect(tl, QSize(br.x() - tl.x(), br.y() - tl.y()));
}
