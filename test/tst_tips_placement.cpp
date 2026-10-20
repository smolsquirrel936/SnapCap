#include <QtTest>

#include "core/TipsPlacement.h"

using TipsPlacement::Corner;

// Pure corner-placement math for the snip key-tips panel: default bottom-left,
// auto-flip (BL -> BR -> TL -> TR) away from everything on the screen.
class TstTipsPlacement : public QObject
{
    Q_OBJECT
private slots:
    void cornerRectsInsetByMargin();
    void noObstaclesStaysBottomLeft();
    void flipsToBottomRightWhenBottomLeftCovered();
    void flipsToTopWhenWholeBottomCovered();
    void fullCoverFallsBackToBottomLeft();
    void avoidsEveryObstacleAtOnce();
    void invalidObstaclesAreIgnored();
    void allCornersBlockedByDifferentObstacles();
    void screenFillingObstacleDoesNotSwampSmallOnes();
    void leastCoveredCornerWinsWhenNoneAreClear();
};

static const QRect kHost(0, 0, 1920, 1080);
static const QSize kPanel(300, 180);
static const int   kMargin = 12;

void TstTipsPlacement::cornerRectsInsetByMargin()
{
    // Bottom edge sits `margin` px above host bottom; right edge `margin` left of host right.
    const QRect bl = TipsPlacement::rectForCorner(kHost, kPanel, Corner::BottomLeft, kMargin);
    QCOMPARE(bl, QRect(12, 888, 300, 180));       // left=12, bottom=1067 (=1079-12)
    const QRect br = TipsPlacement::rectForCorner(kHost, kPanel, Corner::BottomRight, kMargin);
    QCOMPARE(br, QRect(1608, 888, 300, 180));      // right=1907 (=1919-12)
    const QRect tl = TipsPlacement::rectForCorner(kHost, kPanel, Corner::TopLeft, kMargin);
    QCOMPARE(tl, QRect(12, 12, 300, 180));
    const QRect tr = TipsPlacement::rectForCorner(kHost, kPanel, Corner::TopRight, kMargin);
    QCOMPARE(tr, QRect(1608, 12, 300, 180));
}

void TstTipsPlacement::noObstaclesStaysBottomLeft()
{
    Corner c;
    const QRect r = TipsPlacement::place(kHost, kPanel, {}, kMargin, c);
    QCOMPARE(c, Corner::BottomLeft);
    QCOMPARE(r, TipsPlacement::rectForCorner(kHost, kPanel, Corner::BottomLeft, kMargin));
}

void TstTipsPlacement::flipsToBottomRightWhenBottomLeftCovered()
{
    Corner c;
    const QRect sel(0, 800, 400, 280);            // bottom-left region only
    const QRect r = TipsPlacement::place(kHost, kPanel, {sel}, kMargin, c);
    QCOMPARE(c, Corner::BottomRight);
    QVERIFY(!r.intersects(sel));
}

void TstTipsPlacement::flipsToTopWhenWholeBottomCovered()
{
    Corner c;
    const QRect sel(0, 700, 1920, 380);           // full-width bottom band
    const QRect r = TipsPlacement::place(kHost, kPanel, {sel}, kMargin, c);
    QCOMPARE(c, Corner::TopLeft);                  // BL, BR covered -> TL next
    QVERIFY(!r.intersects(sel));
}

void TstTipsPlacement::fullCoverFallsBackToBottomLeft()
{
    Corner c;
    const QRect r = TipsPlacement::place(kHost, kPanel, {kHost}, kMargin, c);
    // One screen-filling obstacle costs every corner the same, so the tie goes to the
    // first in order -> bottom-left, i.e. the panel stays put.
    QCOMPARE(c, Corner::BottomLeft);
    QCOMPARE(r, TipsPlacement::rectForCorner(kHost, kPanel, Corner::BottomLeft, kMargin));
}

void TstTipsPlacement::avoidsEveryObstacleAtOnce()
{
    // Cursor sitting in the bottom-left corner + a selection in the bottom-right:
    // neither bottom corner is usable, so the panel goes top-left.
    Corner c;
    const QRect cursor(100, 950, 48, 48);
    const QRect sel(1500, 850, 400, 220);
    const QRect r = TipsPlacement::place(kHost, kPanel, {sel, cursor}, kMargin, c);
    QCOMPARE(c, Corner::TopLeft);
    QVERIFY(!r.intersects(cursor));
    QVERIFY(!r.intersects(sel));
}

void TstTipsPlacement::invalidObstaclesAreIgnored()
{
    // A hidden magnifier / absent selection arrives as a default-constructed QRect and
    // must not block the preferred corner.
    Corner c;
    const QRect r = TipsPlacement::place(kHost, kPanel, {QRect(), QRect(), QRect()}, kMargin, c);
    QCOMPARE(c, Corner::BottomLeft);
    QCOMPARE(r, TipsPlacement::rectForCorner(kHost, kPanel, Corner::BottomLeft, kMargin));
}

void TstTipsPlacement::allCornersBlockedByDifferentObstacles()
{
    // One obstacle per corner (no single one covers the screen): still falls back to BL.
    Corner c;
    const QVector<QRect> obstacles{
        {0, 900, 400, 180},        // bottom-left
        {1500, 900, 420, 180},     // bottom-right
        {0, 0, 400, 200},          // top-left
        {1500, 0, 420, 200},       // top-right
    };
    const QRect r = TipsPlacement::place(kHost, kPanel, obstacles, kMargin, c);
    QCOMPARE(c, Corner::BottomLeft);
    QCOMPARE(r, TipsPlacement::rectForCorner(kHost, kPanel, Corner::BottomLeft, kMargin));
}

void TstTipsPlacement::screenFillingObstacleDoesNotSwampSmallOnes()
{
    // The bug this guards: hovering before a drag, the auto-detect candidate is a whole
    // (often maximized) window covering all four corners. A fixed fallback would pin the
    // panel to bottom-left and the cursor sitting on it would never move it.
    Corner c;
    const QRect windowUnderCursor = kHost;        // maximized window candidate
    const QRect cursor(100, 950, 48, 48);         // pointer parked on the bottom-left panel
    const QRect r = TipsPlacement::place(kHost, kPanel, {windowUnderCursor, cursor}, kMargin, c);
    QCOMPARE(c, Corner::BottomRight);             // the corner the cursor is NOT in
    QVERIFY(!r.intersects(cursor));
}

void TstTipsPlacement::leastCoveredCornerWinsWhenNoneAreClear()
{
    // Every corner is covered, by decreasing amounts: TR is grazed least and wins even
    // though it is last in the preference order.
    Corner c;
    const QVector<QRect> obstacles{
        kHost,                                     // equal cost to all four corners
        {0, 800, 500, 280},                        // buries bottom-left
        {1400, 800, 520, 280},                     // buries bottom-right
        {0, 0, 500, 300},                          // buries top-left
        {1890, 180, 30, 20},                       // barely clips top-right
    };
    const QRect r = TipsPlacement::place(kHost, kPanel, obstacles, kMargin, c);
    QCOMPARE(c, Corner::TopRight);
    QCOMPARE(r, TipsPlacement::rectForCorner(kHost, kPanel, Corner::TopRight, kMargin));
}

QTEST_APPLESS_MAIN(TstTipsPlacement)
#include "tst_tips_placement.moc"
