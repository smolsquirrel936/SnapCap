#include <QtTest>

#include "core/ScreenLayout.h"

// Mixed-DPI snip coordinate model (headless).
class TstScreenLayout : public QObject
{
    Q_OBJECT
private slots:
    void realMixedDpiBoundsHaveNoGap();
    void mixedDpiPointMapping();
    void rectRoundTripExactScale();
    void monitorLeftOfPrimary();
    void monitorAbovePrimary();
    void coveredRegionExcludesOffsetGap();
    void fractionalScaleRectMapping();
    void indexForCaptureSeam();

private:
    // Build an entry; the captured pixel size defaults to the capture rect size (the
    // real relationship on Windows: a monitor's grabbed image == its physical rect).
    static ScreenLayoutEntry entry(const QRect& logical, const QRect& capture)
    {
        return ScreenLayoutEntry{logical, capture, capture.size(), 1.0, 1.0};
    }
};

// Confirmed dev rig: A left/primary 3840x2160 physical @150% (logical 2560x1440),
// B right 2560x1440 physical @100%, sitting 353px lower. In capture (physical) px
// the two pack edge-adjacent at x=3840 — the 1280px logical phantom band is gone.
void TstScreenLayout::realMixedDpiBoundsHaveNoGap()
{
    const ScreenLayout lay({
        entry(QRect(0, 0, 2560, 1440),     QRect(0, 0, 3840, 2160)),
        entry(QRect(3840, 353, 2560, 1440), QRect(3840, 353, 2560, 1440)),
    });
    // Union spans 0..6400 in x with no gap (A ends and B begins at x=3840).
    QCOMPARE(lay.captureBounds(), QRect(0, 0, 6400, 2160));
    QCOMPARE(lay.screens().at(0).scaleX, 1.5);
    QCOMPARE(lay.screens().at(1).scaleX, 1.0);
}

void TstScreenLayout::mixedDpiPointMapping()
{
    const ScreenLayout lay({
        entry(QRect(0, 0, 2560, 1440),      QRect(0, 0, 3840, 2160)),
        entry(QRect(3840, 353, 2560, 1440), QRect(3840, 353, 2560, 1440)),
    });
    // Screen A (1.5x): local (100,200) -> capture (150,300).
    QCOMPARE(lay.localLogicalToCapture(0, QPointF(100, 200)), QPoint(150, 300));
    // Screen B (1.0x): local origin -> its capture top-left.
    QCOMPARE(lay.localLogicalToCapture(1, QPointF(0, 0)), QPoint(3840, 353));
    QCOMPARE(lay.localLogicalToCapture(1, QPointF(10, 20)), QPoint(3850, 373));
}

void TstScreenLayout::rectRoundTripExactScale()
{
    const ScreenLayout lay({
        entry(QRect(0, 0, 2560, 1440), QRect(0, 0, 3840, 2160)),
    });
    // The whole screen's capture rect maps back to the full local-logical rect.
    QCOMPARE(lay.captureRectToLocalLogical(0, QRect(0, 0, 3840, 2160)),
             QRect(0, 0, 2560, 1440));
    // A sub-rect at 1.5x: capture (150,300 600x900) -> local (100,200 400x600).
    QCOMPARE(lay.captureRectToLocalLogical(0, QRect(150, 300, 600, 900)),
             QRect(100, 200, 400, 600));
}

void TstScreenLayout::monitorLeftOfPrimary()
{
    // Primary at origin; a second monitor sits to its left (negative x).
    const ScreenLayout lay({
        entry(QRect(0, 0, 1000, 1000),    QRect(0, 0, 1000, 1000)),
        entry(QRect(-800, 0, 800, 600),   QRect(-800, 0, 800, 600)),
    });
    QCOMPARE(lay.captureBounds(), QRect(-800, 0, 1800, 1000));
    QCOMPARE(lay.indexForCapture(QPoint(-400, 300)), 1);
    QCOMPARE(lay.indexForCapture(QPoint(500, 500)), 0);
}

void TstScreenLayout::monitorAbovePrimary()
{
    const ScreenLayout lay({
        entry(QRect(0, 0, 1000, 1000),   QRect(0, 0, 1000, 1000)),
        entry(QRect(0, -600, 1200, 600), QRect(0, -600, 1200, 600)),
    });
    QCOMPARE(lay.captureBounds(), QRect(0, -600, 1200, 1600));
    QCOMPARE(lay.indexForCapture(QPoint(500, -300)), 1);
}

void TstScreenLayout::coveredRegionExcludesOffsetGap()
{
    // Two screens with a vertical offset leave true no-screen corners inside the
    // bounding box: the covered region must exclude them.
    const ScreenLayout lay({
        entry(QRect(0, 0, 1000, 1000),    QRect(0, 0, 1000, 1000)),
        entry(QRect(0, 0, 800, 600),      QRect(1000, 400, 800, 600)),
    });
    const QRegion covered = lay.coveredCaptureRegion();
    QVERIFY(covered.contains(QPoint(500, 500)));     // on screen A
    QVERIFY(covered.contains(QPoint(1400, 700)));    // on screen B
    QVERIFY(!covered.contains(QPoint(1400, 100)));   // top-right corner: no screen
    // The covered region is strictly smaller than its bounding rectangle.
    QVERIFY(covered.boundingRect() == QRect(0, 0, 1800, 1000));
    QVERIFY(covered != QRegion(covered.boundingRect()));
}

void TstScreenLayout::fractionalScaleRectMapping()
{
    // 125% scale: logical 1536x864 shown from a 1920x1080 physical grab.
    const ScreenLayout lay({
        entry(QRect(0, 0, 1536, 864), QRect(0, 0, 1920, 1080)),
    });
    QCOMPARE(lay.screens().at(0).scaleX, qreal(1920) / 1536);
    // Full-screen capture rect still maps back to the exact local logical size (no
    // off-by-one from the fractional scale).
    QCOMPARE(lay.captureRectToLocalLogical(0, QRect(0, 0, 1920, 1080)),
             QRect(0, 0, 1536, 864));
}

void TstScreenLayout::indexForCaptureSeam()
{
    const ScreenLayout lay({
        entry(QRect(0, 0, 2560, 1440),      QRect(0, 0, 3840, 2160)),
        entry(QRect(3840, 353, 2560, 1440), QRect(3840, 353, 2560, 1440)),
    });
    QCOMPARE(lay.indexForCapture(QPoint(3839, 500)), 0);   // last A column
    QCOMPARE(lay.indexForCapture(QPoint(3840, 500)), 1);   // first B column
}

QTEST_GUILESS_MAIN(TstScreenLayout)
#include "tst_screen_layout.moc"
