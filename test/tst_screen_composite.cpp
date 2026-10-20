#include <QtTest>

#include "core/ScreenComposite.h"

// Multi-monitor stitch in capture (physical-pixel) space (headless).
class TstScreenComposite : public QObject
{
    Q_OBJECT
private slots:
    void straddlesSeam();
    void selectionInsideOneScreen();
    void mixedDpiAdjacencyNoGap();
    void offsetGapStaysTransparent();
    void selectionOffAllScreens();

private:
    // A screen whose frozen pixels encode their own GLOBAL capture coordinate
    // (low byte of x in red, low byte of y in green, `seed` in blue). Compositing
    // is then verifiable pixel-exact: out(i,j) must equal the pattern for the global
    // point (selCapture.topLeft + (i,j)) on whichever screen covers it.
    static ScreenShot makeScreen(const QRect& captureGeo, int seed)
    {
        ScreenShot s;
        s.captureGeo = captureGeo;
        s.geo = captureGeo;                 // logical unused by the compositor
        s.shot = QImage(captureGeo.size(), QImage::Format_ARGB32);
        for (int y = 0; y < captureGeo.height(); ++y)
            for (int x = 0; x < captureGeo.width(); ++x) {
                const int gx = captureGeo.x() + x, gy = captureGeo.y() + y;
                s.shot.setPixel(x, y, qRgb(gx & 0xff, gy & 0xff, seed));
            }
        return s;
    }
    static QRgb pat(const QPoint& global, int seed)
    {
        return qRgb(global.x() & 0xff, global.y() & 0xff, seed);
    }
};

void TstScreenComposite::straddlesSeam()
{
    const QList<ScreenShot> screens = {
        makeScreen(QRect(0, 0, 100, 100), 11),
        makeScreen(QRect(100, 0, 100, 100), 22),
    };
    const QRect sel(40, 10, 100, 50);          // 60px on left + 40px on right
    const QImage out = ScreenComposite::compositeSelection(screens, sel);
    QCOMPARE(out.size(), QSize(100, 50));
    QCOMPARE(out.pixelColor(0, 0),  QColor(pat(QPoint(40, 10), 11)));
    QCOMPARE(out.pixelColor(59, 0), QColor(pat(QPoint(99, 10), 11)));   // last left column
    QCOMPARE(out.pixelColor(60, 0), QColor(pat(QPoint(100, 10), 22)));  // first right column
    QCOMPARE(out.pixelColor(99, 49), QColor(pat(QPoint(139, 59), 22)));
}

void TstScreenComposite::selectionInsideOneScreen()
{
    const QList<ScreenShot> screens = {
        makeScreen(QRect(0, 0, 100, 100), 11),
        makeScreen(QRect(100, 0, 100, 100), 22),
    };
    const QRect sel(120, 20, 30, 30);
    const QImage out = ScreenComposite::compositeSelection(screens, sel);
    QCOMPARE(out.size(), QSize(30, 30));
    QCOMPARE(out.pixelColor(0, 0),   QColor(pat(QPoint(120, 20), 22)));
    QCOMPARE(out.pixelColor(29, 29), QColor(pat(QPoint(149, 49), 22)));
}

void TstScreenComposite::mixedDpiAdjacencyNoGap()
{
    // Left monitor 150% (150px physical wide), right 100% — edge-adjacent at x=150 in
    // capture space (the case that produced the 1280px white band in logical space).
    const QList<ScreenShot> screens = {
        makeScreen(QRect(0, 0, 150, 100), 11),
        makeScreen(QRect(150, 0, 100, 100), 22),
    };
    const QRect sel(100, 0, 120, 80);          // straddles the seam at x=150
    const QImage out = ScreenComposite::compositeSelection(screens, sel);
    QCOMPARE(out.size(), QSize(120, 80));
    // Every column is covered — no transparent (white-band) gap at the seam.
    for (int i = 0; i < out.width(); ++i)
        QCOMPARE(out.pixelColor(i, 0).alpha(), 255);
    QCOMPARE(out.pixelColor(49, 0), QColor(pat(QPoint(149, 0), 11)));   // last left column
    QCOMPARE(out.pixelColor(50, 0), QColor(pat(QPoint(150, 0), 22)));   // first right column
}

void TstScreenComposite::offsetGapStaysTransparent()
{
    // A genuine vertical-offset gap (right monitor lower): the uncovered corner is left
    // transparent rather than filled — the deliberate behaviour of the fix.
    const QList<ScreenShot> screens = {
        makeScreen(QRect(0, 0, 100, 100), 11),
        makeScreen(QRect(100, 50, 100, 100), 22),
    };
    const QRect sel(50, 0, 120, 120);
    const QImage out = ScreenComposite::compositeSelection(screens, sel);
    QCOMPARE(out.size(), QSize(120, 120));
    QCOMPARE(out.pixelColor(0, 0).alpha(), 255);      // on screen A
    QCOMPARE(out.pixelColor(0, 0), QColor(pat(QPoint(50, 0), 11)));
    QCOMPARE(out.pixelColor(119, 0).alpha(), 0);      // top-right: below A, above B → gap
    QCOMPARE(out.pixelColor(119, 119), QColor(pat(QPoint(169, 119), 22)));   // on screen B
}

void TstScreenComposite::selectionOffAllScreens()
{
    const QList<ScreenShot> screens = { makeScreen(QRect(0, 0, 100, 100), 11) };
    const QImage out = ScreenComposite::compositeSelection(screens, QRect(500, 500, 20, 20));
    QVERIFY(out.isNull());
}

QTEST_GUILESS_MAIN(TstScreenComposite)
#include "tst_screen_composite.moc"
