#include <QtTest>
#include <QImage>

#include "core/render/Raster.h"
#include "core/render/GpuBlur.h"

// Stage 4: deterministic CPU rasterization for the redaction tools. Fixed input
// → fixed output, so these are exact (no display needed).
//
// Stage 7 added the box blur (the shipped CPU path) and the blur dispatch. This
// suite is QTEST_GUILESS_MAIN and that is load-bearing: it proves the dispatch
// defaults to the CPU, because a GL context cannot be created without a
// QGuiApplication. If someone ever flips the default to Auto, these tests are
// what catches it.
class TstRender : public QObject
{
    Q_OBJECT
private slots:
    void mosaicAveragesBlock();
    void mosaicDeterministic();
    void mosaicBlockOneIsNoOp();
    void blurKeepsUniform();
    void blurSmoothsEdge();
    void blurRadiusZeroIsNoOp();
    void boxBlurKeepsUniform();
    void boxBlurRadiusZeroIsNoOp();
    void boxBlurSmoothsEdge();
    void boxBlurDeterministic();
    void boxRadiiAreOddCountedAndPositive();
    void blurDefaultsToCpu();
    void blurAutoFallsBackHeadless();
    void softwareRendererDetection();
};

void TstRender::mosaicAveragesBlock()
{
    QImage img(2, 2, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(0, 0, 0));
    img.setPixelColor(1, 0, QColor(100, 0, 0));
    img.setPixelColor(0, 1, QColor(0, 100, 0));
    img.setPixelColor(1, 1, QColor(0, 0, 100));
    const QImage out = render::mosaic(img, 2);       // one 2x2 cell → its mean
    const QColor avg(25, 25, 25);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            QCOMPARE(out.pixelColor(x, y), avg);
}

void TstRender::mosaicDeterministic()
{
    QImage img(16, 16, QImage::Format_ARGB32);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            img.setPixelColor(x, y, QColor((x * 13) % 256, (y * 7) % 256, (x + y) % 256));
    QCOMPARE(render::mosaic(img, 5), render::mosaic(img, 5));
}

void TstRender::mosaicBlockOneIsNoOp()
{
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::green);
    img.setPixelColor(2, 2, QColor(1, 2, 3));
    QCOMPARE(render::mosaic(img, 1).convertToFormat(QImage::Format_ARGB32),
             img.convertToFormat(QImage::Format_ARGB32));
}

void TstRender::blurKeepsUniform()
{
    QImage img(10, 10, QImage::Format_ARGB32);
    img.fill(QColor(50, 60, 70));
    const QImage out = render::gaussianBlur(img, 3);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            QCOMPARE(out.pixelColor(x, y), QColor(50, 60, 70));
}

void TstRender::blurSmoothsEdge()
{
    QImage img(8, 4, QImage::Format_ARGB32);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x)
            img.setPixelColor(x, y, x < 4 ? QColor(0, 0, 0) : QColor(255, 255, 255));
    const QImage out = render::gaussianBlur(img, 2);
    // Columns straddling the black/white boundary become intermediate grays.
    const int v3 = out.pixelColor(3, 1).red();
    const int v4 = out.pixelColor(4, 1).red();
    QVERIFY(v3 > 0 && v3 < 255);
    QVERIFY(v4 > 0 && v4 < 255);
    QVERIFY(v4 > v3);                                // brighter toward the white side
    QCOMPARE(out, render::gaussianBlur(img, 2));     // deterministic
}

void TstRender::blurRadiusZeroIsNoOp()
{
    QImage img(5, 5, QImage::Format_ARGB32);
    img.fill(Qt::magenta);
    QCOMPARE(render::gaussianBlur(img, 0).convertToFormat(QImage::Format_ARGB32),
             img.convertToFormat(QImage::Format_ARGB32));
}

// --- Stage 7: box blur (the shipped CPU path) -------------------------------

void TstRender::boxBlurKeepsUniform()
{
    QImage img(10, 10, QImage::Format_ARGB32);
    img.fill(QColor(50, 60, 70));
    const QImage out = render::boxBlur(img, 3);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            QCOMPARE(out.pixelColor(x, y), QColor(50, 60, 70));
}

void TstRender::boxBlurRadiusZeroIsNoOp()
{
    QImage img(5, 5, QImage::Format_ARGB32);
    img.fill(Qt::magenta);
    QCOMPARE(render::boxBlur(img, 0).convertToFormat(QImage::Format_ARGB32),
             img.convertToFormat(QImage::Format_ARGB32));
}

void TstRender::boxBlurSmoothsEdge()
{
    QImage img(16, 4, QImage::Format_ARGB32);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 16; ++x)
            img.setPixelColor(x, y, x < 8 ? QColor(0, 0, 0) : QColor(255, 255, 255));
    const QImage out = render::boxBlur(img, 4);
    const int v7 = out.pixelColor(7, 1).red();
    const int v8 = out.pixelColor(8, 1).red();
    QVERIFY(v7 > 0 && v7 < 255);
    QVERIFY(v8 > 0 && v8 < 255);
    QVERIFY(v8 > v7);                                // brighter toward the white side
    // Far from the boundary the original values survive.
    QCOMPARE(out.pixelColor(0, 1).red(), 0);
    QCOMPARE(out.pixelColor(15, 1).red(), 255);
}

void TstRender::boxBlurDeterministic()
{
    QImage img(32, 32, QImage::Format_ARGB32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            img.setPixelColor(x, y, QColor((x * 13) % 256, (y * 7) % 256, (x + y) % 256));
    QCOMPARE(render::boxBlur(img, 9), render::boxBlur(img, 9));
}

void TstRender::boxRadiiAreOddCountedAndPositive()
{
    // Every radius must yield three usable passes; a zero would make a pass a
    // no-op and silently weaken the blur. The GPU path uses these same numbers.
    for (const int r : {1, 2, 6, 12, 40, 80, 200}) {
        const auto radii = render::boxRadii(r);
        QCOMPARE(radii.size(), size_t(3));
        for (const int b : radii)
            QVERIFY2(b >= 1, qPrintable(QStringLiteral("radius %1 gave box %2").arg(r).arg(b)));
    }
    // Wider Gaussian → wider boxes (monotonic in the useful range).
    QVERIFY(render::boxRadii(80)[0] > render::boxRadii(12)[0]);
}

// --- Stage 7: blur dispatch --------------------------------------------------

void TstRender::blurDefaultsToCpu()
{
    QCOMPARE(render::blurMode(), render::BlurMode::Cpu);

    QImage img(24, 24, QImage::Format_ARGB32);
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            img.setPixelColor(x, y, x < 12 ? QColor(10, 20, 30) : QColor(200, 210, 220));
    QCOMPARE(render::blur(img, 6), render::boxBlur(img, 6));
}

void TstRender::blurAutoFallsBackHeadless()
{
    // No QGuiApplication here, so the GPU probe must fail and the dispatch must
    // fall back to the identical CPU result rather than crash or return null.
    render::setBlurMode(render::BlurMode::Auto);
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(QColor(80, 90, 100));
    img.setPixelColor(8, 8, QColor(0, 0, 0));

    const QImage out = render::blur(img, 5);
    QVERIFY(!out.isNull());
    QCOMPARE(out, render::boxBlur(img, 5));

    render::setBlurMode(render::BlurMode::Cpu);      // restore for other cases
}

void TstRender::softwareRendererDetection()
{
    // The half of the capability probe that is testable without hardware -- and
    // the half that matters: windeployqt ships Mesa llvmpipe as Qt's fallback, so
    // context creation SUCCEEDS on a machine with no usable GPU. Missing these
    // strings would make "Auto" slower than "CPU" on exactly those machines.
    QVERIFY(render::isSoftwareRenderer(QStringLiteral("llvmpipe (LLVM 15.0.7, 256 bits)")));
    QVERIFY(render::isSoftwareRenderer(QStringLiteral("Microsoft Basic Render Driver")));
    QVERIFY(render::isSoftwareRenderer(QStringLiteral("GDI Generic")));
    QVERIFY(render::isSoftwareRenderer(QStringLiteral("softpipe")));
    QVERIFY(render::isSoftwareRenderer(QStringLiteral("SWRast")));          // case-insensitive

    QVERIFY(!render::isSoftwareRenderer(QStringLiteral("NVIDIA GeForce RTX 5090/PCIe/SSE2")));
    QVERIFY(!render::isSoftwareRenderer(QStringLiteral("AMD Radeon RX 7900 XTX")));
    QVERIFY(!render::isSoftwareRenderer(QStringLiteral("Intel(R) UHD Graphics 770")));
    QVERIFY(!render::isSoftwareRenderer(QString()));
}

QTEST_GUILESS_MAIN(TstRender)
#include "tst_render.moc"
