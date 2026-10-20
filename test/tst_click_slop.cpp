#include <QtTest>

#include "core/ClickSlop.h"

using namespace click_slop;

// The anti-misclick gate on the snip region drag: how far a press must travel before it
// stops being a click and becomes a drag. Pure arithmetic, so it is tested exhaustively
// here rather than by hand in the overlay.
class TstClickSlop : public QObject
{
    Q_OBJECT
private slots:
    void thresholdDecaysWithHoldTime();
    void thresholdNeverIncreases();
    void quickSlipIsAClick();
    void quickLargeDragArms();
    void unhurriedSmallDragArms();
    void hesitantClickTwitchIsAClick();
    void heldPressArmsOnAnyRealMovement();
    void perAxisNotDiagonal();
    void zeroSlopDisablesTheGate();

private:
    // The shipped defaults (snip/behavior/clickSlopPx, snip/behavior/clickWindowMs).
    static constexpr int kSlop = 30;
    static constexpr int kWindow = 300;
};

void TstClickSlop::thresholdDecaysWithHoldTime()
{
    QCOMPARE(clickSlopThreshold(0, kSlop, kWindow), 30);
    QCOMPARE(clickSlopThreshold(kWindow, kSlop, kWindow), 30);        // boundary: still inside
    QCOMPARE(clickSlopThreshold(kWindow + 1, kSlop, kWindow), kFloorPx);
    QCOMPARE(clickSlopThreshold(kReleaseMs, kSlop, kWindow), kFloorPx); // boundary: still floored
    QCOMPARE(clickSlopThreshold(kReleaseMs + 1, kSlop, kWindow), kMinPx);
    QCOMPARE(clickSlopThreshold(60000, kSlop, kWindow), kMinPx);
}

void TstClickSlop::thresholdNeverIncreases()
{
    // Latching assumes the bar only ever drops. Hold that even for a configured slop
    // smaller than the floor (clickSlopPx=5), where the tiers would otherwise invert.
    for (int slop : {0, 1, 5, 10, 30, 200}) {
        int prev = clickSlopThreshold(0, slop, kWindow);
        for (qint64 t : {1, 299, 300, 301, 999, 1000, 1001, 5000}) {
            const int now = clickSlopThreshold(t, slop, kWindow);
            QVERIFY2(now <= prev, qPrintable(QStringLiteral("slop=%1 t=%2").arg(slop).arg(t)));
            prev = now;
        }
    }
}

void TstClickSlop::quickSlipIsAClick()
{
    // The case this whole gate exists for: a jitter while clicking a highlighted window.
    QVERIFY(!exceedsClickSlop({500, 500}, {503, 498}, 90, kSlop, kWindow));
    QVERIFY(!exceedsClickSlop({500, 500}, {529, 471}, 200, kSlop, kWindow));  // 29px, just under
}

void TstClickSlop::quickLargeDragArms()
{
    // A deliberate fast drag must never be swallowed, however brief.
    QVERIFY(exceedsClickSlop({500, 500}, {900, 700}, 40, kSlop, kWindow));
    QVERIFY(exceedsClickSlop({500, 500}, {530, 500}, 10, kSlop, kWindow));   // exactly 30px
}

void TstClickSlop::unhurriedSmallDragArms()
{
    // Past the click window a small region is reachable again: 12px is under the 30px
    // slop but over the 10px floor.
    QVERIFY(!exceedsClickSlop({500, 500}, {512, 500}, 250, kSlop, kWindow));
    QVERIFY(exceedsClickSlop({500, 500}, {512, 500}, 400, kSlop, kWindow));
}

void TstClickSlop::hesitantClickTwitchIsAClick()
{
    // Press, hesitate, twitch on release. The floor is what keeps this a click — with a
    // 4px system drag threshold here it would produce a garbage region instead.
    QVERIFY(!exceedsClickSlop({500, 500}, {506, 503}, 400, kSlop, kWindow));
    QVERIFY(!exceedsClickSlop({500, 500}, {509, 500}, 999, kSlop, kWindow));
    // ...and a 1px twitch stays a click even after a full second of holding.
    QVERIFY(!exceedsClickSlop({500, 500}, {501, 501}, 3000, kSlop, kWindow));
}

void TstClickSlop::heldPressArmsOnAnyRealMovement()
{
    // Hold a full second and even a 2px region becomes selectable.
    QVERIFY(exceedsClickSlop({500, 500}, {502, 500}, 1500, kSlop, kWindow));
    // Holding without moving is never a drag, no matter how long.
    QVERIFY(!exceedsClickSlop({500, 500}, {500, 500}, 30000, kSlop, kWindow));
}

void TstClickSlop::perAxisNotDiagonal()
{
    // (29,29) is 58px of travel summed, but neither axis reaches 30 — still a click.
    QVERIFY(!exceedsClickSlop({0, 0}, {29, 29}, 0, kSlop, kWindow));
    // Either axis on its own is enough, so a thin wide strip still drags.
    QVERIFY(exceedsClickSlop({0, 0}, {200, 4}, 0, kSlop, kWindow));
    QVERIFY(exceedsClickSlop({0, 0}, {4, 200}, 0, kSlop, kWindow));
    // Direction is irrelevant.
    QVERIFY(exceedsClickSlop({500, 500}, {470, 500}, 0, kSlop, kWindow));
}

void TstClickSlop::zeroSlopDisablesTheGate()
{
    // clickSlopPx=0 is the documented escape hatch: behave exactly as before the gate,
    // where the first move event began the drag.
    QVERIFY(exceedsClickSlop({500, 500}, {500, 500}, 0, 0, kWindow));
    QVERIFY(exceedsClickSlop({500, 500}, {501, 500}, 5000, 0, kWindow));
}

QTEST_APPLESS_MAIN(TstClickSlop)
#include "tst_click_slop.moc"
