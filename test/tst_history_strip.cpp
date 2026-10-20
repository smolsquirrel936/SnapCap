#include <QtTest>

#include "core/HistoryStrip.h"

// Stage 6: pure windowing math for the in-snip history strip. window() returns the
// visible slice of `count` thumbnails around `selected`, at most `maxVisible` wide,
// centred on the selection then clamped so it never runs past either end.
class TstHistoryStrip : public QObject
{
    Q_OBJECT
private slots:
    void emptyReturnsZero();
    void allFitReturnsWholeRange();
    void centresOnSelection();
    void clampsAtStart();
    void clampsAtEnd();
    void selectionAlwaysVisible();
};

void TstHistoryStrip::emptyReturnsZero()
{
    auto w = HistoryStrip::window(0, 0, 5);
    QCOMPARE(w.start, 0);
    QCOMPARE(w.len, 0);

    auto w2 = HistoryStrip::window(3, 0, 0);   // no slots
    QCOMPARE(w2.len, 0);
}

void TstHistoryStrip::allFitReturnsWholeRange()
{
    auto w = HistoryStrip::window(4, 2, 5);
    QCOMPARE(w.start, 0);
    QCOMPARE(w.len, 4);
}

void TstHistoryStrip::centresOnSelection()
{
    // 10 entries, 5 slots, selection in the middle → window centred on it.
    auto w = HistoryStrip::window(10, 5, 5);
    QCOMPARE(w.len, 5);
    QCOMPARE(w.start, 3);                       // 5 - 5/2 = 3, covers [3..7]
}

void TstHistoryStrip::clampsAtStart()
{
    auto w = HistoryStrip::window(10, 0, 5);
    QCOMPARE(w.start, 0);                       // can't go negative
    QCOMPARE(w.len, 5);
}

void TstHistoryStrip::clampsAtEnd()
{
    auto w = HistoryStrip::window(10, 9, 5);
    QCOMPARE(w.start, 5);                       // last window is [5..9]
    QCOMPARE(w.len, 5);
}

void TstHistoryStrip::selectionAlwaysVisible()
{
    // For every selection across a range that overflows, the chosen window must
    // contain it.
    for (int sel = 0; sel < 20; ++sel) {
        auto w = HistoryStrip::window(20, sel, 6);
        QVERIFY(sel >= w.start && sel < w.start + w.len);
    }
}

QTEST_APPLESS_MAIN(TstHistoryStrip)
#include "tst_history_strip.moc"
