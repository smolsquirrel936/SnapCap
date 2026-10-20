#include <QtTest>

#include "core/RoundedCorners.h"

// Corner-rounding of a finished snip image (headless).
class TstRoundedCorners : public QObject
{
    Q_OBJECT
private slots:
    void clampsToHalfTheShorterSide();
    void zeroRadiusIsIdentity();
    void cornersGoTransparent();
    void centreAndEdgesStayOpaque();
    void nullImageSurvives();

private:
    // A fully opaque red square — every surviving pixel must still be exactly red,
    // which is what proves the mask only touched alpha.
    static QImage solid(int w, int h)
    {
        QImage img(w, h, QImage::Format_ARGB32);
        img.fill(QColor(255, 0, 0));
        return img;
    }
};

void TstRoundedCorners::clampsToHalfTheShorterSide()
{
    QCOMPARE(RoundedCorners::clamp(10, QSize(100, 100)), 10);
    QCOMPARE(RoundedCorners::clamp(90, QSize(100, 60)), 30);    // half of 60
    QCOMPARE(RoundedCorners::clamp(90, QSize(60, 100)), 30);
    QCOMPARE(RoundedCorners::clamp(-5, QSize(100, 100)), 0);
    QCOMPARE(RoundedCorners::clamp(10, QSize()), 0);
}

void TstRoundedCorners::zeroRadiusIsIdentity()
{
    const QImage src = solid(40, 40);
    QCOMPARE(RoundedCorners::apply(src, 0), src);
    QCOMPARE(RoundedCorners::apply(src, -3), src);
}

void TstRoundedCorners::cornersGoTransparent()
{
    // Radius 20 on a 100x100 image: each corner pixel sits well outside its arc.
    const QImage out = RoundedCorners::apply(solid(100, 100), 20);
    QCOMPARE(out.pixelColor(0, 0).alpha(), 0);
    QCOMPARE(out.pixelColor(99, 0).alpha(), 0);
    QCOMPARE(out.pixelColor(0, 99).alpha(), 0);
    QCOMPARE(out.pixelColor(99, 99).alpha(), 0);
    // Just inside the arc's diagonal the pixel must still be there — the cut is a
    // corner, not a chamfer across the whole quadrant.
    QCOMPARE(out.pixelColor(19, 19).alpha(), 255);
}

void TstRoundedCorners::centreAndEdgesStayOpaque()
{
    const QImage out = RoundedCorners::apply(solid(100, 100), 20);
    QCOMPARE(out.pixelColor(50, 50), QColor(255, 0, 0));      // untouched, alpha included
    QCOMPARE(out.pixelColor(50, 0).alpha(), 255);             // top edge midpoint
    QCOMPARE(out.pixelColor(50, 99).alpha(), 255);            // bottom edge midpoint
    QCOMPARE(out.pixelColor(0, 50).alpha(), 255);             // left edge midpoint
    QCOMPARE(out.pixelColor(99, 50).alpha(), 255);            // right edge midpoint
}

void TstRoundedCorners::nullImageSurvives()
{
    QVERIFY(RoundedCorners::apply(QImage(), 20).isNull());
}

QTEST_GUILESS_MAIN(TstRoundedCorners)
#include "tst_rounded_corners.moc"
