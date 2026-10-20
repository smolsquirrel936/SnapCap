#include <QtTest>
#include <QRect>

#include "core/AreaHistory.h"

// Stage 6: the in-memory snip-area ring backing the in-snip `R` / `Shift+R` reuse-area
// replay. record() prepends the newest area (newest first); the ring caps at max
// (oldest evicted); step() walks older then newer, clamping at the ends unless loop
// wraps around.
class TstAreaHistory : public QObject
{
    Q_OBJECT
private:
    static QRect rc(int x) { return QRect(x, x, 10, 10); }   // distinct rect per x

private slots:
    void recordPrependsNewestFirst();
    void capsAtMaxEvictingOldest();
    void skipsDuplicateOfNewest();
    void ignoresEmptyRect();
    void stepWalksOlderThenNewer();
    void clampsAtEndsWhenLoopOff();
    void wrapsWhenLoopOn();
    void setMaxShrinkEvictsAndResetsCursor();
    void maxZeroDisables();
};

void TstAreaHistory::recordPrependsNewestFirst()
{
    AreaHistory h(10);
    h.record(rc(1));
    h.record(rc(2));
    QCOMPARE(h.count(), 2);
    QCOMPARE(h.areas().at(0), rc(2));   // newest first
    QCOMPARE(h.areas().at(1), rc(1));
}

void TstAreaHistory::capsAtMaxEvictingOldest()
{
    AreaHistory h(2);
    h.record(rc(1));
    h.record(rc(2));
    h.record(rc(3));
    QCOMPARE(h.count(), 2);
    QCOMPARE(h.areas().at(0), rc(3));   // newest
    QCOMPARE(h.areas().at(1), rc(2));   // rc(1) evicted
}

void TstAreaHistory::skipsDuplicateOfNewest()
{
    AreaHistory h(5);
    h.record(rc(1));
    h.record(rc(1));                    // identical to newest — no-op
    QCOMPARE(h.count(), 1);
}

void TstAreaHistory::ignoresEmptyRect()
{
    AreaHistory h(5);
    h.record(QRect());                  // null
    h.record(QRect(0, 0, 0, 5));        // zero width
    QCOMPARE(h.count(), 0);
}

void TstAreaHistory::stepWalksOlderThenNewer()
{
    AreaHistory h(5);
    h.record(rc(1));                    // oldest
    h.record(rc(2));
    h.record(rc(3));                    // newest

    QCOMPARE(h.step(-1), rc(3));        // first older → newest
    QCOMPARE(h.step(-1), rc(2));
    QCOMPARE(h.step(-1), rc(1));        // oldest
    QCOMPARE(h.step(+1), rc(2));        // walk back toward newer
    QCOMPARE(h.step(+1), rc(3));
}

void TstAreaHistory::clampsAtEndsWhenLoopOff()
{
    AreaHistory h(5);
    h.record(rc(1));
    h.record(rc(2));
    h.step(-1);                         // → rc(2) (newest)
    h.step(-1);                         // → rc(1) (oldest)
    QVERIFY(h.step(-1).isNull());       // clamped at the oldest end
    QCOMPARE(h.step(+1), rc(2));        // cursor stayed at oldest, so newer works
    QVERIFY(h.step(+1).isNull());       // clamped at the newest end
}

void TstAreaHistory::wrapsWhenLoopOn()
{
    AreaHistory h(5);
    h.setLoop(true);
    h.record(rc(1));                    // oldest
    h.record(rc(2));                    // newest
    h.step(-1);                         // → rc(2)
    h.step(-1);                         // → rc(1) (oldest)
    QCOMPARE(h.step(-1), rc(2));        // older past oldest wraps to newest
    QCOMPARE(h.step(+1), rc(1));        // newer past newest wraps to oldest
}

void TstAreaHistory::setMaxShrinkEvictsAndResetsCursor()
{
    AreaHistory h(5);
    h.record(rc(1));
    h.record(rc(2));
    h.record(rc(3));
    h.step(-1);                         // cursor now at newest
    h.setMax(1);
    QCOMPARE(h.count(), 1);
    QCOMPARE(h.areas().at(0), rc(3));   // only newest survives
    QCOMPARE(h.cursor(), -1);           // cursor reset
}

void TstAreaHistory::maxZeroDisables()
{
    AreaHistory h(0);
    h.record(rc(1));
    QCOMPARE(h.count(), 0);
    QVERIFY(h.step(-1).isNull());
}

QTEST_APPLESS_MAIN(TstAreaHistory)
#include "tst_area_history.moc"
