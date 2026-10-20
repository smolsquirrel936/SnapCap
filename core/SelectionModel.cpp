#include "core/SelectionModel.h"

#include <algorithm>

SelectionModel::SelectionModel(QObject* parent) : QObject(parent) {}

void SelectionModel::setBounds(const QRect& bounds)
{
    m_bounds = bounds.normalized();
    if (!m_rect.isNull())
        setRectInternal(m_rect);          // re-clamp to new bounds
}

void SelectionModel::beginAt(const QPoint& p)
{
    m_anchor = p;
    m_dragging = true;
    setRectInternal(QRect(p, QSize(0, 0)));
}

void SelectionModel::dragTo(const QPoint& p)
{
    if (!m_dragging)
        return;
    // Build an inclusive rect from the two points explicitly. (Avoids a
    // QRect::normalized() off-by-one when the drag direction is inverted.)
    const int x = std::min(m_anchor.x(), p.x());
    const int y = std::min(m_anchor.y(), p.y());
    const int w = std::abs(p.x() - m_anchor.x()) + 1;
    const int h = std::abs(p.y() - m_anchor.y()) + 1;
    setRectInternal(QRect(x, y, w, h));
}

void SelectionModel::commit()
{
    m_dragging = false;
    if (isValid())
        m_lastCommitted = m_rect;
}

void SelectionModel::moveBy(int dx, int dy)
{
    if (m_rect.isNull())
        return;
    setRectInternal(clampInside(m_rect.translated(dx, dy)));
}

void SelectionModel::moveTopLeftTo(const QPoint& tl)
{
    if (m_rect.isNull())
        return;
    QRect r = m_rect;
    r.moveTo(tl);
    // clampInside shifts the rect back inside the bounds while preserving its size
    // (so dragging against an edge stops there instead of shrinking the region).
    setRectInternal(clampInside(r));
}

void SelectionModel::grow(Edge edge)
{
    QRect r = m_rect;
    switch (edge) {
        case Edge::Left:   r.setLeft(std::max(m_bounds.left(),   r.left() - 1)); break;
        case Edge::Right:  r.setRight(std::min(m_bounds.right(),  r.right() + 1)); break;
        case Edge::Top:    r.setTop(std::max(m_bounds.top(),     r.top() - 1)); break;
        case Edge::Bottom: r.setBottom(std::min(m_bounds.bottom(), r.bottom() + 1)); break;
    }
    setRectInternal(r);
}

void SelectionModel::shrink(Edge edge)
{
    QRect r = m_rect;
    // Keep at least a 1x1 selection (QRect width = right-left+1).
    switch (edge) {
        case Edge::Left:   if (r.width()  > 1) r.setLeft(r.left() + 1);   break;
        case Edge::Right:  if (r.width()  > 1) r.setRight(r.right() - 1); break;
        case Edge::Top:    if (r.height() > 1) r.setTop(r.top() + 1);     break;
        case Edge::Bottom: if (r.height() > 1) r.setBottom(r.bottom() - 1); break;
    }
    setRectInternal(r);
}

void SelectionModel::selectFullscreen()
{
    setRectInternal(m_bounds);
}

void SelectionModel::repeatLast()
{
    if (m_lastCommitted.isValid())
        setRectInternal(m_lastCommitted);
}

void SelectionModel::setRect(const QRect& r)
{
    setRectInternal(r.normalized());
}

QRect SelectionModel::clampInside(QRect r) const
{
    if (m_bounds.isNull())
        return r;
    if (r.left() < m_bounds.left())     r.moveLeft(m_bounds.left());
    if (r.top() < m_bounds.top())       r.moveTop(m_bounds.top());
    if (r.right() > m_bounds.right())   r.moveRight(m_bounds.right());
    if (r.bottom() > m_bounds.bottom()) r.moveBottom(m_bounds.bottom());
    return r;
}

void SelectionModel::setRectInternal(const QRect& r)
{
    QRect clamped = r;
    if (!m_bounds.isNull())
        clamped = r.intersected(m_bounds);  // never extend past the desktop
    if (clamped == m_rect)
        return;
    m_rect = clamped;
    emit changed();
}
