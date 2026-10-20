#include <QtTest>
#include <QImage>
#include <QPainter>

#include "core/PinModel.h"
#include "core/annotation/Annotation.h"
#include "core/annotation/AnnotationDocument.h"

// Stage 5: pin transform state + deterministic compose() order & clamping/reset.
class TstPinModel : public QObject
{
    Q_OBJECT
private:
    static QImage sample(int w, int h)
    {
        QImage img(w, h, QImage::Format_ARGB32);
        img.fill(Qt::white);
        return img;
    }

private slots:
    void composeDefaultUnchanged();
    void rotateSwapsDimensions();
    void rotateCCWWraps();
    void flipMirrorsPixels();
    void grayscaleDesaturatesAndKeepsAlpha();
    void invertFlipsRgbKeepsAlpha();
    void colorOpsSpareAnnotations();
    void colorOpsRoundTrip();
    void scaleResizes();
    void scaleClamped();
    void maxLongSideClampsScale();
    void opacityClamped();
    void resetRestoresAll();
    void rotateThenScaleOrder();
    void replaceResetsTransforms();
    void smoothScalingBlendsPixels();
    void smoothOverrideRoundTrips();
    void sourcePathRoundTrips();
    void replaceClearsSourcePath();
    void sourceToComposedCoversComposedBounds();
    void sourceToComposedMapsKnownCorners();
    void sourceToComposedInvertsForInput();
    void sourceToComposedAgreesWithComposedPixels();
    void draftTransformMatchesBakedResult();
    void annotationsBakeIntoCompose();
    void annotationsIgnoreSourceDpr();
    void annotationsRotateWithTheImage();
    void replaceResetsAnnotations();
    void animationSwapsSourceOnly();
    void animationWrapsAndCarriesDelays();
    void singleFrameIsNotAnAnimation();
    void replaceClearsAnimation();
};

void TstPinModel::composeDefaultUnchanged()
{
    PinModel m(sample(4, 2));
    QCOMPARE(m.compose().size(), QSize(4, 2));
    QCOMPARE(m.composedSize(), QSize(4, 2));
}

void TstPinModel::rotateSwapsDimensions()
{
    PinModel m(sample(4, 2));
    m.rotateCW();
    QCOMPARE(m.rotation(), 90);
    QCOMPARE(m.composedSize(), QSize(2, 4));
    QCOMPARE(m.compose().size(), QSize(2, 4));
}

void TstPinModel::rotateCCWWraps()
{
    PinModel m(sample(4, 2));
    m.rotateCCW();
    QCOMPARE(m.rotation(), 270);
    m.rotateCCW();
    QCOMPARE(m.rotation(), 180);
}

void TstPinModel::flipMirrorsPixels()
{
    QImage img(2, 1, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, Qt::red);
    img.setPixelColor(1, 0, Qt::blue);
    PinModel m(img);
    m.flipHorizontal();
    const QImage out = m.compose();
    QCOMPARE(out.pixelColor(0, 0), QColor(Qt::blue));
    QCOMPARE(out.pixelColor(1, 0), QColor(Qt::red));
}

void TstPinModel::grayscaleDesaturatesAndKeepsAlpha()
{
    QImage img(2, 1, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(255, 0, 0, 128));    // semi-transparent red
    img.setPixelColor(1, 0, QColor(0, 0, 255));
    PinModel m(img);
    m.toggleGrayscale();
    QVERIFY(m.grayscale());

    const QColor red = m.compose().pixelColor(0, 0);
    QCOMPARE(red, QColor(87, 87, 87, 128));             // qGray(255,0,0) = 87
    // The alpha is the point: converting via Format_Grayscale8 would drop it and turn a
    // transparent pin opaque.
    QCOMPARE(red.alpha(), 128);
    const QColor blue = m.compose().pixelColor(1, 0);
    QCOMPARE(blue.red(), blue.blue());

    m.toggleGrayscale();                                // toggles back off, non-destructive
    QVERIFY(!m.grayscale());
    QCOMPARE(m.compose().pixelColor(0, 0), QColor(255, 0, 0, 128));
}

void TstPinModel::invertFlipsRgbKeepsAlpha()
{
    QImage img(1, 1, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(10, 20, 30, 200));
    PinModel m(img);
    m.toggleInvert();
    QVERIFY(m.inverted());
    QCOMPARE(m.compose().pixelColor(0, 0), QColor(245, 235, 225, 200));   // alpha untouched
}

void TstPinModel::colorOpsRoundTrip()
{
    PinModel m(sample(4, 4));
    m.toggleGrayscale();
    m.toggleInvert();

    PinModel restored(sample(4, 4));
    restored.fromJson(m.toJson());
    QVERIFY(restored.grayscale());       // survives a session restore
    QVERIFY(restored.inverted());
}

void TstPinModel::scaleResizes()
{
    PinModel m(sample(10, 6));
    m.setScale(2.0);
    QCOMPARE(m.composedSize(), QSize(20, 12));
    QCOMPARE(m.compose().size(), QSize(20, 12));
}

void TstPinModel::scaleClamped()
{
    PinModel m(sample(10, 10));
    m.setScale(1000.0);
    QVERIFY(m.scale() <= 20.0);
    m.setScale(0.0);
    QVERIFY(m.scale() >= 0.05);
}

void TstPinModel::maxLongSideClampsScale()
{
    PinModel m(sample(100, 50));      // longest side = 100 px
    m.setMaxLongSide(300);            // cap the composed longest side at 300 px
    m.setScale(10.0);                 // would be 1000 px → clamped to 3.0 (300/100)
    QCOMPARE(m.composedSize(), QSize(300, 150));
    m.scaleBy(100.0);                 // still capped
    QCOMPARE(m.composedSize().width(), 300);
    m.setMaxLongSide(0);              // 0 = unlimited → cap lifts
    m.setScale(10.0);
    QCOMPARE(m.composedSize(), QSize(1000, 500));
}

void TstPinModel::opacityClamped()
{
    PinModel m(sample(4, 4));
    m.setOpacity(5.0);
    QCOMPARE(m.opacity(), 1.0);
    m.setOpacity(0.0);
    QVERIFY(m.opacity() >= 0.1);
}

void TstPinModel::resetRestoresAll()
{
    PinModel m(sample(4, 2));
    m.rotateCW();
    m.flipVertical();
    m.toggleGrayscale();
    m.toggleInvert();
    m.setScale(3.0);
    m.setOpacity(0.5);
    m.reset();
    QCOMPARE(m.rotation(), 0);
    QVERIFY(!m.flippedH() && !m.flippedV());
    QVERIFY(!m.grayscale() && !m.inverted());
    QCOMPARE(m.scale(), 1.0);
    QCOMPARE(m.opacity(), 1.0);
}

void TstPinModel::rotateThenScaleOrder()
{
    PinModel m(sample(4, 2));
    m.rotateCW();             // 4x2 → 2x4
    m.setScale(2.0);          // → 4x8
    QCOMPARE(m.compose().size(), QSize(4, 8));
}

void TstPinModel::replaceResetsTransforms()
{
    PinModel m(sample(4, 2), QStringLiteral("orig"));
    m.rotateCW();
    m.setScale(2.0);
    m.setSource(sample(8, 8), QStringLiteral("new"));
    QCOMPARE(m.rotation(), 0);
    QCOMPARE(m.scale(), 1.0);
    QCOMPARE(m.sourceText(), QStringLiteral("new"));
    QCOMPARE(m.compose().size(), QSize(8, 8));
}

void TstPinModel::smoothScalingBlendsPixels()
{
    // A 2×2 checkerboard upscaled: nearest-neighbour keeps only the two source colours,
    // bilinear introduces blended (intermediate-grey) pixels along the edges. This is the
    // pixel-level difference the "Smooth scaling" setting controls.
    QImage img(2, 2, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, Qt::black); img.setPixelColor(1, 0, Qt::white);
    img.setPixelColor(0, 1, Qt::white); img.setPixelColor(1, 1, Qt::black);

    auto hasBlend = [](const QImage& out) {
        for (int y = 0; y < out.height(); ++y)
            for (int x = 0; x < out.width(); ++x) {
                const int v = qGray(out.pixel(x, y));
                if (v > 20 && v < 235)          // neither near-black nor near-white
                    return true;
            }
        return false;
    };

    PinModel fast(img);
    fast.setSmoothScaling(false);
    fast.setScale(8.0);                          // 2×2 → 16×16
    QVERIFY(!hasBlend(fast.compose()));          // nearest → only black/white blocks

    PinModel smooth(img);
    smooth.setSmoothScaling(true);
    smooth.setScale(8.0);
    QVERIFY(hasBlend(smooth.compose()));         // bilinear → blended edges
}

void TstPinModel::smoothOverrideRoundTrips()
{
    // Only a per-pin override (the pin's own Zoom > Smoothing item) is serialized: a pin
    // that never touched it must stay free to follow the global default at restore.
    PinModel plain(sample(4, 4));
    plain.setSmoothScaling(false);                    // the global default seeding a pin
    QVERIFY(!plain.smoothScalingOverridden());
    QVERIFY(!plain.toJson().contains(QStringLiteral("smooth")));

    PinModel m(sample(4, 4));
    m.overrideSmoothScaling(false);
    QVERIFY(m.smoothScalingOverridden());
    QVERIFY(!m.smoothScaling());

    PinModel restored(sample(4, 4));
    restored.fromJson(m.toJson());
    QVERIFY(restored.smoothScalingOverridden());      // survives a session restore
    QVERIFY(!restored.smoothScaling());
}

void TstPinModel::sourcePathRoundTrips()
{
    PinModel m(sample(4, 4));
    QVERIFY(m.sourcePath().isEmpty());                // a fresh pin has no file behind it
    QVERIFY(!m.toJson().contains(QStringLiteral("sourcePath")));

    m.setSourcePath(QStringLiteral("C:/pics/shot.png"));
    PinModel restored(sample(4, 4));
    restored.fromJson(m.toJson());
    QCOMPARE(restored.sourcePath(), QStringLiteral("C:/pics/shot.png"));
}

void TstPinModel::replaceClearsSourcePath()
{
    // After a clipboard replace the pixels no longer match the file they came from, so
    // "View in folder" must not keep pointing at it.
    PinModel m(sample(4, 4));
    m.setSourcePath(QStringLiteral("C:/pics/shot.png"));
    m.setSource(sample(8, 8));
    QVERIFY(m.sourcePath().isEmpty());
}

// A solid blue rect annotation covering [0,0]-(cover x cover) of the source.
static std::unique_ptr<annotation::Annotation> blueRect(int cover)
{
    annotation::Style st;
    st.color = Qt::blue;
    st.filled = true;                       // solid fill, no border → predictable pixels
    auto a = annotation::create(annotation::ToolId::Rect, st);
    a->setGeometry({QPoint(0, 0), QPoint(cover, cover)});
    return a;
}

void TstPinModel::sourceToComposedCoversComposedBounds()
{
    // sourceToComposed() must be the exact twin of compose(): mapping the source rect
    // through it has to land on the composed image's bounds — at the origin, and with
    // the composed size — for every rotation x flip x scale. This is the transform the
    // pin editor paints and hit-tests through, so a mismatch here misplaces everything.
    for (int rot = 0; rot < 4; ++rot) {
        for (int flip = 0; flip < 4; ++flip) {
            for (qreal s : {0.5, 1.0, 2.5}) {
                PinModel m(sample(10, 6));
                for (int i = 0; i < rot; ++i) m.rotateCW();
                if (flip & 1) m.flipHorizontal();
                if (flip & 2) m.flipVertical();
                m.setScale(s);

                const QRectF mapped = m.sourceToComposed().mapRect(QRectF(0, 0, 10, 6));
                const QSize composed = m.composedSize();
                const QString at = QStringLiteral("rot=%1 flipH=%2 flipV=%3 scale=%4")
                                       .arg(rot * 90).arg(flip & 1).arg((flip & 2) >> 1).arg(s);

                // Composed images always start at (0,0) — QImage::transformed() shifts
                // the rotated result back to the origin, and the transform must too.
                QVERIFY2(qAbs(mapped.left()) < 0.001 && qAbs(mapped.top()) < 0.001,
                         qPrintable(at + QStringLiteral(" origin=(%1,%2)")
                                             .arg(mapped.left()).arg(mapped.top())));
                // Within a pixel: the transform uses the unrounded scale, compose()
                // rounds to whole pixels.
                QVERIFY2(qAbs(mapped.width() - composed.width()) <= 1.0
                             && qAbs(mapped.height() - composed.height()) <= 1.0,
                         qPrintable(at + QStringLiteral(" mapped=%1x%2 composed=%3x%4")
                                             .arg(mapped.width()).arg(mapped.height())
                                             .arg(composed.width()).arg(composed.height())));
            }
        }
    }
}

void TstPinModel::sourceToComposedMapsKnownCorners()
{
    // Pin down the orientation, not just the bounds: a rotation that mapped the corners
    // to the wrong places would still cover the right box.
    {   // Rotate CW: the source's top-left lands top-RIGHT of the composed image.
        PinModel m(sample(4, 2));
        m.rotateCW();                                  // 4x2 → 2x4
        QCOMPARE(m.sourceToComposed().map(QPointF(0, 0)), QPointF(2, 0));
        QCOMPARE(m.sourceToComposed().map(QPointF(4, 0)), QPointF(2, 4));
    }
    {   // Mirror horizontally: top-left → top-right, same box.
        PinModel m(sample(4, 2));
        m.flipHorizontal();
        QCOMPARE(m.sourceToComposed().map(QPointF(0, 0)), QPointF(4, 0));
        QCOMPARE(m.sourceToComposed().map(QPointF(4, 2)), QPointF(0, 2));
    }
    {   // Scale multiplies on top of the orientation.
        PinModel m(sample(4, 2));
        m.setScale(3.0);
        QCOMPARE(m.sourceToComposed().map(QPointF(4, 2)), QPointF(12, 6));
    }
}

void TstPinModel::sourceToComposedInvertsForInput()
{
    // The editor turns mouse position into source px with the inverse — it must round-trip.
    PinModel m(sample(10, 6));
    m.rotateCW();
    m.flipVertical();
    m.setScale(1.75);

    const QTransform t = m.sourceToComposed();
    QVERIFY(t.isInvertible());
    const QPointF src(3, 4);
    const QPointF back = t.inverted().map(t.map(src));
    QVERIFY(qAbs(back.x() - src.x()) < 0.001);
    QVERIFY(qAbs(back.y() - src.y()) < 0.001);
}

void TstPinModel::sourceToComposedAgreesWithComposedPixels()
{
    // The bounds check above can't tell 90° from 270° (both give the same box), nor an
    // H flip from a V one on a square. So track a single marked pixel through the REAL
    // compose() output: wherever the transform says a source pixel went, that is where
    // compose() must actually have put it. This is the check that pins down orientation.
    for (int rot = 0; rot < 4; ++rot) {
        for (int flip = 0; flip < 4; ++flip) {
            QImage src = sample(8, 4);
            src.setPixelColor(1, 0, Qt::red);       // asymmetric in BOTH axes

            PinModel m(src);
            for (int i = 0; i < rot; ++i) m.rotateCW();
            if (flip & 1) m.flipHorizontal();
            if (flip & 2) m.flipVertical();

            // Map the marked pixel's CENTRE — a corner would sit ambiguously on an edge.
            const QPointF at = m.sourceToComposed().map(QPointF(1.5, 0.5));
            const QImage out = m.compose();
            const QPoint px(int(at.x()), int(at.y()));
            const QString where = QStringLiteral("rot=%1 flipH=%2 flipV=%3 → (%4,%5)")
                                      .arg(rot * 90).arg(flip & 1).arg((flip & 2) >> 1)
                                      .arg(px.x()).arg(px.y());

            QVERIFY2(out.rect().contains(px), qPrintable(where + QStringLiteral(" is outside")));
            QVERIFY2(out.pixelColor(px) == QColor(Qt::red),
                     qPrintable(where + QStringLiteral(" is %1, expected red")
                                            .arg(out.pixelColor(px).name())));
        }
    }
}

void TstPinModel::draftTransformMatchesBakedResult()
{
    // The pin editor paints the LIVE draft through sourceToComposed(), but compose()
    // BAKES the committed annotation onto the source and transforms the pixels. Those are
    // two different rasterisation routes to the same picture, and the user only ever sees
    // the seam between them: draw (draft) → release (baked). If they disagree, the shape
    // appears to jump on commit. Nothing above tests that they agree.
    for (int rot = 0; rot < 4; ++rot) {
        for (qreal s : {1.0, 2.0}) {
            const QString at = QStringLiteral("rot=%1 scale=%2").arg(rot * 90).arg(s);

            // Route A — commit: the annotation is baked, then the image is transformed.
            PinModel baked(sample(20, 12));
            for (int i = 0; i < rot; ++i) baked.rotateCW();
            baked.setScale(s);
            baked.annotations()->add(blueRect(6));
            const QImage a = baked.compose();

            // Route B — draft: the clean pin is composed, then the same annotation is
            // painted over it through the transform (what PinEditController::paintDraft
            // does, minus the widget's dpr/margin).
            PinModel clean(sample(20, 12));
            for (int i = 0; i < rot; ++i) clean.rotateCW();
            clean.setScale(s);
            QImage b = clean.compose();
            {
                const QImage base = clean.source();
                annotation::PaintCtx ctx;
                ctx.base = &base;
                ctx.originLogical = QPointF(0, 0);
                ctx.scale = 1.0;
                QPainter p(&b);
                p.setTransform(clean.sourceToComposed());
                blueRect(6)->paint(p, ctx);
            }

            QCOMPARE(a.size(), b.size());
            // Compare the blue mark's bounding box on each route: an offset between them
            // is exactly the "jumps on commit" symptom.
            auto blueBounds = [](const QImage& img) {
                QRect r;
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x)
                        if (img.pixelColor(x, y).blue() > 200 && img.pixelColor(x, y).red() < 100)
                            r = r.isNull() ? QRect(x, y, 1, 1) : r.united(QRect(x, y, 1, 1));
                return r;
            };
            const QRect ra = blueBounds(a), rb = blueBounds(b);
            QVERIFY2(!ra.isNull() && !rb.isNull(), qPrintable(at + QStringLiteral(" no mark")));
            QVERIFY2(ra == rb, qPrintable(at + QStringLiteral(" baked=%1,%2 %3x%4 vs draft=%5,%6 %7x%8")
                                                  .arg(ra.x()).arg(ra.y()).arg(ra.width()).arg(ra.height())
                                                  .arg(rb.x()).arg(rb.y()).arg(rb.width()).arg(rb.height())));
        }
    }
}

void TstPinModel::annotationsBakeIntoCompose()
{
    PinModel m(sample(8, 8));                 // all white
    QVERIFY(m.annotations() != nullptr);
    QVERIFY(m.annotations()->isEmpty());
    QCOMPARE(m.compose().pixelColor(1, 1), QColor(Qt::white));

    m.annotations()->add(blueRect(4));
    QCOMPARE(m.compose().pixelColor(1, 1), QColor(Qt::blue));   // baked in
    QCOMPARE(m.compose().pixelColor(6, 6), QColor(Qt::white));  // outside the rect
    QCOMPARE(m.composedSize(), QSize(8, 8));                    // flatten preserves size

    m.annotations()->undo();                                     // undo reaches the pixels
    QCOMPARE(m.compose().pixelColor(1, 1), QColor(Qt::white));
}

void TstPinModel::annotationsIgnoreSourceDpr()
{
    // A snip pin's source carries the capture dpr (Application hands img.devicePixelRatio()
    // to addImage), and on a scaled display that used to make the BAKE — a QPainter on a
    // dpr>1 image, working in logical px — scale every committed item away from the origin.
    // The live draft paints through sourceToWidget() and was unaffected, so the shape
    // jumped bottom-right the instant it was committed. Dpr must not move a single pixel.
    QImage base(8, 8, QImage::Format_ARGB32);
    base.fill(Qt::white);
    base.setDevicePixelRatio(2.0);

    PinModel m(base);
    m.annotations()->add(blueRect(4));       // source px (0,0)-(4,4)
    const QImage out = m.compose();

    QCOMPARE(out.size(), QSize(8, 8));
    QCOMPARE(out.pixelColor(1, 1), QColor(Qt::blue));    // inside the rect
    QCOMPARE(out.pixelColor(6, 6), QColor(Qt::white));   // outside it — was blue at 2x
}

void TstPinModel::colorOpsSpareAnnotations()
{
    // Unlike the geometry transforms, the color ops run on the BASE pixels only — they are
    // applied before the annotations bake, so a blue callout stays blue over a greyed
    // photo instead of dissolving into it.
    QImage base(8, 8, QImage::Format_ARGB32);
    base.fill(Qt::red);
    PinModel m(base);
    m.annotations()->add(blueRect(4));
    m.toggleGrayscale();

    const QImage out = m.compose();
    QCOMPARE(out.pixelColor(6, 6), QColor(87, 87, 87));      // base desaturated
    QCOMPARE(out.pixelColor(1, 1), QColor(Qt::blue));        // annotation keeps its colour

    // The eraser restores the base as DISPLAYED (PinEditController's m_draftClean), so the
    // processed source must be grey too — restoring source() would smear red back in.
    QCOMPARE(m.processedSource().pixelColor(1, 1), QColor(87, 87, 87));
}

void TstPinModel::annotationsRotateWithTheImage()
{
    // Annotations belong to the image: they bake onto the source BEFORE the transform
    // stack, so rotating the pin carries them along. The blue corner starts top-left and
    // must ride round to top-right under a clockwise rotation.
    PinModel m(sample(8, 8));
    m.annotations()->add(blueRect(4));
    QCOMPARE(m.compose().pixelColor(1, 1), QColor(Qt::blue));

    m.rotateCW();
    QCOMPARE(m.compose().pixelColor(6, 1), QColor(Qt::blue));    // top-right now
    QCOMPARE(m.compose().pixelColor(1, 1), QColor(Qt::white));
}

void TstPinModel::replaceResetsAnnotations()
{
    // New pixels → the old geometry means nothing on them. The undo stack has to go too,
    // or Ctrl+Z would resurrect annotations onto an unrelated image.
    PinModel m(sample(8, 8));
    m.annotations()->add(blueRect(4));
    QVERIFY(!m.annotations()->isEmpty());

    m.setSource(sample(8, 8));
    QVERIFY(m.annotations()->isEmpty());
    QVERIFY(!m.annotations()->canUndo());
    QCOMPARE(m.compose().pixelColor(1, 1), QColor(Qt::white));
}

// --- animation (GIF playback) ------------------------------------------------
// Two 4x4 frames, distinguishable by their fill colour.
static QVector<QImage> twoFrames()
{
    QImage a(4, 4, QImage::Format_ARGB32); a.fill(Qt::red);
    QImage b(4, 4, QImage::Format_ARGB32); b.fill(Qt::green);
    return {a, b};
}

void TstPinModel::animationSwapsSourceOnly()
{
    // The whole point of advanceFrame(): every frame goes through the same transform stack
    // and annotations, so those must survive a frame swap (unlike setSource()).
    PinModel m(sample(4, 4));
    m.setAnimation(twoFrames(), {40, 60});
    m.setSourcePath(QStringLiteral("C:/tmp/a.gif"));
    m.annotations()->add(blueRect(2));
    m.rotateCW();
    m.setScale(2.0);

    QVERIFY(m.isAnimated());
    QCOMPARE(m.frameCount(), 2);
    QCOMPARE(m.source().pixelColor(0, 0), QColor(Qt::red));

    m.advanceFrame();
    QCOMPARE(m.source().pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(m.rotation(), 90);
    QCOMPARE(m.scale(), 2.0);
    QCOMPARE(m.sourcePath(), QStringLiteral("C:/tmp/a.gif"));
    QVERIFY(!m.annotations()->isEmpty());
    QCOMPARE(m.compose().size(), QSize(8, 8));     // rotate+scale still applied to frame 1
}

void TstPinModel::animationWrapsAndCarriesDelays()
{
    PinModel m(sample(4, 4));
    m.setAnimation(twoFrames(), {40, 60});
    QCOMPARE(m.currentFrame(), 0);
    QCOMPARE(m.currentDelayMs(), 40);

    m.advanceFrame();
    QCOMPARE(m.currentFrame(), 1);
    QCOMPARE(m.currentDelayMs(), 60);

    m.advanceFrame();                              // loops forever — no "finished" state
    QCOMPARE(m.currentFrame(), 0);
    QCOMPARE(m.source().pixelColor(0, 0), QColor(Qt::red));
}

void TstPinModel::singleFrameIsNotAnAnimation()
{
    PinModel m(sample(4, 4));
    m.setAnimation({twoFrames().first()}, {40});   // one frame is a still, not a loop
    QVERIFY(!m.isAnimated());
    QCOMPARE(m.currentDelayMs(), 0);
    QCOMPARE(m.source().pixelColor(0, 0), QColor(Qt::white));   // source left alone
    m.advanceFrame();                                           // no-op, must not crash
    QCOMPARE(m.source().pixelColor(0, 0), QColor(Qt::white));
}

void TstPinModel::replaceClearsAnimation()
{
    PinModel m(sample(4, 4));
    m.setAnimation(twoFrames(), {40, 60});
    m.setSource(sample(4, 4));                     // new pixels → the old frames are stale
    QVERIFY(!m.isAnimated());
    QCOMPARE(m.frameCount(), 0);
    QCOMPARE(m.source().pixelColor(0, 0), QColor(Qt::white));
}

QTEST_GUILESS_MAIN(TstPinModel)
#include "tst_pin_model.moc"
