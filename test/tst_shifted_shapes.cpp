#include <QtTest>
#include <QPoint>
#include <QRect>

#include "core/annotation/ShapeConstraints.h"

// Stage 4: the "hold Shift" constraint math — square/circle from a free drag and
// line/arrow snapped to the nearest 45° direction. Pure arithmetic.
class TstShiftedShapes : public QObject
{
    Q_OBJECT
private slots:
    void squareUsesLongerAxis();
    void squarePreservesDirection();
    void lineSnapsHorizontal();
    void lineSnapsVertical();
    void lineSnapsDiagonal();
    void zeroDragIsStable();
    void ratioKeepsAspectExactly();
    void ratioUsesDominantAxis();
    void ratioPreservesDirection();
    void ratioStopsAtBoundsKeepingAspect();
    void ratioSquareIsUnitRatio();
    void endCtrlSquaresRectFamily();
    void endShiftDoesNotSquare();
    void endShiftSnapsLineAndArrow();
    void endCtrlDoesNotSnapLine();
    void endUnconstrainedToolsPassThrough();
};

void TstShiftedShapes::squareUsesLongerAxis()
{
    // Wider than tall → square side = width; taller than wide → side = height.
    QCOMPARE(ShapeConstraints::squareCorner(QPoint(0, 0), QPoint(10, 4)), QPoint(10, 10));
    QCOMPARE(ShapeConstraints::squareCorner(QPoint(0, 0), QPoint(3, 8)),  QPoint(8, 8));
}

void TstShiftedShapes::squarePreservesDirection()
{
    QCOMPARE(ShapeConstraints::squareCorner(QPoint(0, 0), QPoint(-5, 3)),  QPoint(-5, 5));
    QCOMPARE(ShapeConstraints::squareCorner(QPoint(10, 10), QPoint(2, 4)), QPoint(2, 2));
}

void TstShiftedShapes::lineSnapsHorizontal()
{
    const QPoint e = ShapeConstraints::snapLine(QPoint(0, 0), QPoint(10, 2));
    QCOMPARE(e.y(), 0);
    QVERIFY(e.x() > 9);                              // length preserved along the axis
}

void TstShiftedShapes::lineSnapsVertical()
{
    const QPoint e = ShapeConstraints::snapLine(QPoint(0, 0), QPoint(2, 10));
    QCOMPARE(e.x(), 0);
    QVERIFY(e.y() > 9);
}

void TstShiftedShapes::lineSnapsDiagonal()
{
    const QPoint e = ShapeConstraints::snapLine(QPoint(0, 0), QPoint(10, 9));
    QCOMPARE(e.x(), e.y());                          // exact 45°
    QVERIFY(e.x() > 0);
}

void TstShiftedShapes::zeroDragIsStable()
{
    QCOMPARE(ShapeConstraints::snapLine(QPoint(7, 7), QPoint(7, 7)), QPoint(7, 7));
    QCOMPARE(ShapeConstraints::squareCorner(QPoint(7, 7), QPoint(7, 7)), QPoint(7, 7));
}

// ratioCorner: the snip overlay's Shift (keep ratio) / Ctrl (square) corner lock.
// It works in inclusive-rect units, so the checks below assert on the rect the
// selection actually gets (width = right - left + 1), not on the raw delta.
static QRect inclusiveRect(const QPoint& anchor, const QPoint& corner)
{
    // Built the way SelectionModel::dragTo does — QRect(a, b).normalized() would
    // lose a pixel per side when the drag direction is inverted.
    return QRect(QPoint(qMin(anchor.x(), corner.x()), qMin(anchor.y(), corner.y())),
                 QPoint(qMax(anchor.x(), corner.x()), qMax(anchor.y(), corner.y())));
}

void TstShiftedShapes::ratioKeepsAspectExactly()
{
    // 2:1 lock — the resulting rect is exactly twice as wide as it is tall.
    const QPoint a(0, 0);
    const QPoint c = ShapeConstraints::ratioCorner(a, QPoint(99, 10), QSize(2, 1), QRect());
    const QRect r = inclusiveRect(a, c);
    QCOMPARE(r.width(), 100);
    QCOMPARE(r.height(), 50);
}

void TstShiftedShapes::ratioUsesDominantAxis()
{
    // The axis the cursor pulled furthest (relative to the ratio) drives the size.
    const QPoint a(0, 0);
    const QRect r = inclusiveRect(a, ShapeConstraints::ratioCorner(a, QPoint(10, 99),
                                                                   QSize(2, 1), QRect()));
    QCOMPARE(r.width(), 200);
    QCOMPARE(r.height(), 100);
}

void TstShiftedShapes::ratioPreservesDirection()
{
    // Dragging up-left of the anchor keeps the rect on that side.
    const QPoint a(100, 100);
    const QPoint c = ShapeConstraints::ratioCorner(a, QPoint(60, 90), QSize(1, 1), QRect());
    QCOMPARE(c, QPoint(60, 60));
    const QRect r = inclusiveRect(a, c);
    QCOMPARE(r.width(), r.height());
    QCOMPARE(r.bottomRight(), QPoint(100, 100));      // anchor stayed put
}

void TstShiftedShapes::ratioStopsAtBoundsKeepingAspect()
{
    // Past the edge the drag stops with the ratio still exact — never cropped.
    const QRect bounds(0, 0, 100, 100);
    const QPoint a(0, 0);

    const QRect sq = inclusiveRect(a, ShapeConstraints::ratioCorner(a, QPoint(200, 50),
                                                                    QSize(1, 1), bounds));
    QCOMPARE(sq, QRect(0, 0, 100, 100));              // fills the bounds, still square

    const QRect wide = inclusiveRect(a, ShapeConstraints::ratioCorner(a, QPoint(200, 200),
                                                                      QSize(2, 1), bounds));
    QCOMPARE(wide.width(), 100);
    QCOMPARE(wide.height(), 50);                      // 2:1 held, inside the bounds
    QVERIFY(bounds.contains(wide));
}

void TstShiftedShapes::ratioSquareIsUnitRatio()
{
    // Ctrl (square) is just a 1:1 ratio, and a zero drag stays put.
    const QPoint a(0, 0);
    QCOMPARE(ShapeConstraints::ratioCorner(a, QPoint(10, 4), QSize(1, 1), QRect()),
             QPoint(10, 10));
    QCOMPARE(ShapeConstraints::ratioCorner(QPoint(7, 7), QPoint(7, 7), QSize(1, 1), QRect()),
             QPoint(7, 7));
}

// constrainedEnd: the shared modifier dispatch used by both the snip overlay and the
// pin editor. Ctrl = equal sides (rect family), Shift = 45° snap (line/arrow).
void TstShiftedShapes::endCtrlSquaresRectFamily()
{
    using annotation::ToolId;
    const QPoint a(0, 0), c(10, 4);
    for (ToolId t : {ToolId::Rect, ToolId::Ellipse, ToolId::Mosaic,
                     ToolId::Blur, ToolId::EraserRect, ToolId::MarkerRect})
        QCOMPARE(ShapeConstraints::constrainedEnd(t, a, c, Qt::ControlModifier),
                 QPoint(10, 10));
}

void TstShiftedShapes::endShiftDoesNotSquare()
{
    // Shift no longer constrains the rect family — Ctrl took that over.
    const QPoint a(0, 0), c(10, 4);
    QCOMPARE(ShapeConstraints::constrainedEnd(annotation::ToolId::Rect, a, c,
                                              Qt::ShiftModifier), c);
    QCOMPARE(ShapeConstraints::constrainedEnd(annotation::ToolId::Ellipse, a, c,
                                              Qt::ShiftModifier), c);
}

void TstShiftedShapes::endShiftSnapsLineAndArrow()
{
    const QPoint a(0, 0);
    // Near-horizontal locks flat; near-vertical locks upright.
    QCOMPARE(ShapeConstraints::constrainedEnd(annotation::ToolId::Line, a, QPoint(10, 2),
                                              Qt::ShiftModifier).y(), 0);
    QCOMPARE(ShapeConstraints::constrainedEnd(annotation::ToolId::Arrow, a, QPoint(2, 10),
                                              Qt::ShiftModifier).x(), 0);
    // A near-45° drag still snaps to the diagonal (8 directions, not just the axes).
    const QPoint d = ShapeConstraints::constrainedEnd(annotation::ToolId::Line, a,
                                                      QPoint(10, 9), Qt::ShiftModifier);
    QCOMPARE(d.x(), d.y());
}

void TstShiftedShapes::endCtrlDoesNotSnapLine()
{
    // Ctrl is the rect-family modifier; it must leave a line alone.
    const QPoint a(0, 0), c(10, 2);
    QCOMPARE(ShapeConstraints::constrainedEnd(annotation::ToolId::Line, a, c,
                                              Qt::ControlModifier), c);
}

void TstShiftedShapes::endUnconstrainedToolsPassThrough()
{
    // No modifier, and tools with no two-point constraint, return the cursor as-is.
    const QPoint a(0, 0), c(10, 4);
    QCOMPARE(ShapeConstraints::constrainedEnd(annotation::ToolId::Rect, a, c,
                                              Qt::NoModifier), c);
    QCOMPARE(ShapeConstraints::constrainedEnd(annotation::ToolId::Pencil, a, c,
                                              Qt::ControlModifier), c);
    QCOMPARE(ShapeConstraints::constrainedEnd(annotation::ToolId::Marker, a, c,
                                              Qt::ShiftModifier), c);
}

QTEST_GUILESS_MAIN(TstShiftedShapes)
#include "tst_shifted_shapes.moc"
