#pragma once

#include <QObject>
#include <QRect>
#include <QPoint>

// The snip selection rectangle + its interaction state machine. Pure logic
// (QRect math only) so it is unit-tested headlessly; the overlay drives it from
// mouse/keyboard and reads back rect() to paint. All operations clamp to the
// virtual-desktop bounds. See claude-plan/Implementation-Plan.md (Stage 2).
class SelectionModel : public QObject {
    Q_OBJECT
public:
    enum class Edge { Left, Right, Top, Bottom };

    explicit SelectionModel(QObject* parent = nullptr);

    // The clamp region (union of all screens, in logical coords).
    void setBounds(const QRect& bounds);
    QRect bounds() const { return m_bounds; }

    // Mouse drag: press, move, release.
    void beginAt(const QPoint& p);
    void dragTo(const QPoint& p);
    void commit();                       // finishes a drag; remembers the rect
    bool isDragging() const { return m_dragging; }
    QPoint anchor() const { return m_anchor; }   // the fixed corner of the in-flight drag

    // Keyboard adjustment (all 1px steps, all clamped).
    void moveBy(int dx, int dy);         // translate, keeping size, inside bounds
    void moveTopLeftTo(const QPoint& tl); // reposition to an absolute top-left, keeping size (clamped)
    void grow(Edge edge);                // push an edge outward by 1px
    void shrink(Edge edge);             // pull an edge inward by 1px (min 1x1)

    void selectFullscreen();             // rect = bounds
    void repeatLast();                   // restore the last committed rect

    void setRect(const QRect& r);        // set explicitly (clamped)
    QRect rect() const { return m_rect; }
    bool isValid() const { return m_rect.width() > 0 && m_rect.height() > 0; }

signals:
    void changed();

private:
    QRect clampInside(QRect r) const;    // shift a rect to fit within bounds
    void setRectInternal(const QRect& r);

    QRect m_bounds;
    QRect m_rect;
    QRect m_lastCommitted;
    QPoint m_anchor;
    bool m_dragging = false;
};
