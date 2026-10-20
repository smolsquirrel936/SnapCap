#include <QtTest>
#include <QImage>
#include <QJsonObject>
#include <QPainterPath>

#include "core/annotation/Annotation.h"
#include "core/annotation/AnnotationDocument.h"

using namespace annotation;

// Stage 4: the annotation document — add/remove/z-order, undo/redo invariants
// (incl. clear-all), hit-testing, flatten, and JSON round-trip. Pure logic
// (QImage/QPainter only) so it runs headlessly. Text rendering (QFontMetrics)
// needs a GUI app and is verified manually; here Text is exercised via JSON only.
class TstAnnotationDocument : public QObject
{
    Q_OBJECT
private:
    static std::unique_ptr<Annotation> rect(QPoint a, QPoint b, QColor c = Qt::red)
    {
        Style s; s.color = c; s.penWidth = 4;
        auto r = create(ToolId::Rect, s);
        r->setGeometry({a, b});
        return r;
    }

private slots:
    void addAssignsIdAndCounts();
    void degenerateNotAdded();
    void undoRedo();
    void clearAllUndo();
    void removeUndo();
    void moveUndo();
    void hitTestReturnsTopmost();
    void rotatedHitTestAndBounds();
    void outlinePathMatchesShape();
    void rotatedShapeJsonRoundTrip();
    void reshapeAppliedIsOneUndoStep();
    void restyleIsOneUndoStep();
    void restyleAppliedIsOneUndoStep();
    void jsonRoundTrip();
    void flattenNoOp();
    void flattenDrawsShape();
    void flattenIgnoresBaseDpr();
    void redactBrushJsonRoundTrip();
    void redactBrushFlatten();
    void redactRectOffCanvasDoesNotStretch();
    void resetDropsItemsAndUndo();
    void textMirroredJsonRoundTrip();
    void flattenAntialiases();
    void numberLabelCentredBoundsAndHitTest();
    void numberLabelJsonRoundTrip();
    void numberStyleFormatting();
    void setTextsIsOneUndoStep();
    void draggedMoveIsOneUndoStep();
    void wheelResizeIsOneUndoStep();
    void deleteWithRenumberIsOneUndoStep();
    void curvedLineFollowsVertices();
    void selfCrossingArrowHasNoHoles();
    void dashedShaftHasGaps();
    void capSizeMatchesExceptTaperedHollow();
    void lineCapsJsonRoundTrip();
    void legacyArrowFieldMigrates();
};

void TstAnnotationDocument::addAssignsIdAndCounts()
{
    AnnotationDocument doc;
    const int id = doc.add(rect({0, 0}, {20, 20}));
    QVERIFY(id > 0);
    QCOMPARE(doc.count(), 1);
}

void TstAnnotationDocument::degenerateNotAdded()
{
    AnnotationDocument doc;
    QCOMPARE(doc.add(rect({5, 5}, {5, 5})), 0);   // zero-size → not recorded
    QCOMPARE(doc.count(), 0);
    QVERIFY(doc.add(nullptr) == 0);
}

void TstAnnotationDocument::undoRedo()
{
    AnnotationDocument doc;
    doc.add(rect({0, 0}, {10, 10}));
    doc.add(rect({5, 5}, {20, 20}));
    QCOMPARE(doc.count(), 2);
    QVERIFY(doc.canUndo());
    doc.undo();
    QCOMPARE(doc.count(), 1);
    doc.undo();
    QCOMPARE(doc.count(), 0);
    QVERIFY(!doc.canUndo());
    doc.redo();
    doc.redo();
    QCOMPARE(doc.count(), 2);
}

void TstAnnotationDocument::clearAllUndo()
{
    AnnotationDocument doc;
    doc.add(rect({0, 0}, {10, 10}));
    doc.add(rect({5, 5}, {20, 20}));
    doc.clearAll();
    QCOMPARE(doc.count(), 0);
    doc.undo();
    QCOMPARE(doc.count(), 2);   // clear-all is a single, fully reversible step
}

void TstAnnotationDocument::removeUndo()
{
    AnnotationDocument doc;
    const int id = doc.add(rect({0, 0}, {10, 10}));
    doc.add(rect({30, 30}, {40, 40}));
    doc.remove(id);
    QCOMPARE(doc.count(), 1);
    doc.undo();
    QCOMPARE(doc.count(), 2);
}

void TstAnnotationDocument::moveUndo()
{
    AnnotationDocument doc;
    const int id = doc.add(rect({0, 0}, {10, 10}));
    const QRect before = doc.items().front()->bounds();
    doc.move(id, QPoint(15, 7));
    QCOMPARE(doc.items().front()->bounds(), before.translated(15, 7));
    doc.undo();
    QCOMPARE(doc.items().front()->bounds(), before);
}

void TstAnnotationDocument::hitTestReturnsTopmost()
{
    AnnotationDocument doc;
    const int bottom = doc.add(rect({0, 0}, {50, 50}));
    const int top = doc.add(rect({0, 0}, {50, 50}));
    Q_UNUSED(bottom);
    Annotation* hit = doc.hitTest(QPoint(25, 25));
    QVERIFY(hit != nullptr);
    QCOMPARE(hit->id, top);                          // last added wins
    QVERIFY(doc.hitTest(QPoint(500, 500)) == nullptr);
}

void TstAnnotationDocument::rotatedHitTestAndBounds()
{
    // A non-text Base2P shape can be rotated; hitTest & bounds must respect the angle.
    Style s; s.penWidth = 3;
    auto a = create(ToolId::Rect, s);
    a->setGeometry({QPoint(0, 0), QPoint(40, 10)});   // a wide, short bar
    QVERIFY(!a->hitTest(QPoint(20, 20)));             // below the bar → miss
    QVERIFY(a->hitTest(QPoint(40, 5)));               // right edge → hit

    a->setRotation(90.0);                             // stand it upright
    QVERIFY(a->hitTest(QPoint(20, 20)));             // now inside the rotated footprint
    QVERIFY(!a->hitTest(QPoint(40, 5)));             // now outside it
    const QRect b = a->bounds();
    QVERIFY(b.height() > b.width());                 // a wide bar rotated 90° is now tall
}

void TstAnnotationDocument::outlinePathMatchesShape()
{
    // The hover highlight follows the shape: a rect gets a rectangular outline, an
    // ellipse an elliptical one — same bounding box, different fill region.
    auto rc = create(ToolId::Rect, Style{});
    rc->setGeometry({QPoint(0, 0), QPoint(100, 100)});
    auto el = create(ToolId::Ellipse, Style{});
    el->setGeometry({QPoint(0, 0), QPoint(100, 100)});

    QCOMPARE(el->outlinePath().boundingRect(), rc->outlinePath().boundingRect());
    QVERIFY(rc->outlinePath().contains(QPointF(5, 5)));    // corner is inside the rect
    QVERIFY(!el->outlinePath().contains(QPointF(5, 5)));   // ...but outside the ellipse
    QVERIFY(el->outlinePath().contains(QPointF(50, 50)));  // centre is inside both
}

void TstAnnotationDocument::rotatedShapeJsonRoundTrip()
{
    Style s; s.color = QColor(9, 8, 7); s.penWidth = 6;
    auto a = create(ToolId::Ellipse, s);
    a->setGeometry({QPoint(2, 3), QPoint(50, 40)});
    a->setRotation(37.5);

    auto back = Annotation::fromJson(a->toJson());
    QVERIFY(back != nullptr);
    QCOMPARE(back->tool(), ToolId::Ellipse);
    QCOMPARE(back->geometry(), (QVector<QPoint>{{2, 3}, {50, 40}}));
    QCOMPARE(back->rotation(), 37.5);

    // A shape left un-rotated omits "rot" but still reads back as 0.
    auto plain = create(ToolId::Rect, s);
    plain->setGeometry({QPoint(0, 0), QPoint(10, 10)});
    QVERIFY(!plain->toJson().contains(QStringLiteral("rot")));
    QCOMPARE(Annotation::fromJson(plain->toJson())->rotation(), 0.0);
}

void TstAnnotationDocument::reshapeAppliedIsOneUndoStep()
{
    // A resize/rotate drag reshapes the annotation live per mouse-move; reshapeApplied
    // records the whole gesture (geometry + rotation) as a single undo step.
    AnnotationDocument doc;
    const int id = doc.add(rect({0, 0}, {20, 20}));
    Annotation* a = doc.items().front().get();
    const QVector<QPoint> beforeGeom = a->geometry();
    const qreal beforeRot = a->rotation();            // 0

    a->setGeometry({QPoint(0, 0), QPoint(60, 40)});   // live resize...
    a->setRotation(30.0);                             // ...and rotate
    doc.reshapeApplied(id, beforeGeom, beforeRot);
    QCOMPARE(a->geometry(), (QVector<QPoint>{{0, 0}, {60, 40}}));
    QCOMPARE(a->rotation(), 30.0);

    doc.undo();                                       // ONE undo reverts both
    QCOMPARE(doc.items().front()->geometry(), beforeGeom);
    QCOMPARE(doc.items().front()->rotation(), 0.0);
    doc.redo();
    QCOMPARE(doc.items().front()->rotation(), 30.0);

    // A reshape that nets no change must not push anything.
    Annotation* b = doc.items().front().get();
    doc.reshapeApplied(id, b->geometry(), b->rotation());
    doc.undo();
    QCOMPARE(doc.items().front()->rotation(), 0.0);   // undid the real reshape, not a no-op
}

void TstAnnotationDocument::restyleIsOneUndoStep()
{
    AnnotationDocument doc;
    const int id = doc.add(rect({0, 0}, {20, 20}, Qt::red));   // penWidth 4
    Style after = doc.items().front()->style;
    after.color = Qt::blue;
    after.penWidth = 9;
    doc.restyle(id, after);
    QCOMPARE(doc.items().front()->style.color, QColor(Qt::blue));
    QCOMPARE(doc.items().front()->style.penWidth, 9);

    doc.undo();
    QCOMPARE(doc.items().front()->style.color, QColor(Qt::red));
    QCOMPARE(doc.items().front()->style.penWidth, 4);
    doc.redo();
    QCOMPARE(doc.items().front()->style.color, QColor(Qt::blue));
}

void TstAnnotationDocument::restyleAppliedIsOneUndoStep()
{
    // A wheel width burst mutates style.penWidth live per notch; restyleApplied banks the
    // whole burst as one undo step (like resizeApplied for the label font size).
    AnnotationDocument doc;
    const int id = doc.add(rect({0, 0}, {20, 20}));    // penWidth 4
    Annotation* a = doc.items().front().get();
    const Style before = a->style;

    for (int i = 0; i < 6; ++i) a->style.penWidth += 1;   // 6 live notches → 10
    QCOMPARE(a->style.penWidth, 10);
    doc.restyleApplied(id, before);
    QCOMPARE(a->style.penWidth, 10);                   // net width unchanged by the bookkeeping

    doc.undo();                                        // ONE undo restores the start width
    QCOMPARE(doc.items().front()->style.penWidth, 4);
    doc.undo();                                        // ...and the next undo is the add
    QCOMPARE(doc.count(), 0);
    doc.redo();
    doc.redo();
    QCOMPARE(doc.items().front()->style.penWidth, 10);

    // A burst that nets no change pushes nothing.
    Annotation* b = doc.items().front().get();
    doc.restyleApplied(id, b->style);
    doc.undo();
    QCOMPARE(doc.items().front()->style.penWidth, 4);  // undid the real restyle, not a no-op
}

void TstAnnotationDocument::jsonRoundTrip()
{
    AnnotationDocument doc;
    doc.add(rect({1, 2}, {30, 40}, QColor(10, 20, 30)));
    {
        Style s; s.color = Qt::blue; s.penWidth = 5;
        auto line = create(ToolId::Line, s);
        line->setGeometry({QPoint(3, 4), QPoint(60, 70)});
        doc.add(std::move(line));
    }
    {
        Style s; s.fontPx = 24;
        auto txt = create(ToolId::Text, s);
        txt->setGeometry({QPoint(8, 9)});
        txt->setText(QStringLiteral("hello"));
        doc.add(std::move(txt));
    }

    const QJsonObject json = doc.toJson();
    AnnotationDocument loaded;
    loaded.fromJson(json);

    QCOMPARE(loaded.count(), doc.count());
    const auto& a = loaded.items();
    QCOMPARE(a[0]->tool(), ToolId::Rect);
    QCOMPARE(a[0]->style.color, QColor(10, 20, 30));
    QCOMPARE(a[0]->geometry(), (QVector<QPoint>{{1, 2}, {30, 40}}));
    QCOMPARE(a[1]->tool(), ToolId::Line);
    QCOMPARE(a[1]->style.penWidth, 5);
    QCOMPARE(a[2]->tool(), ToolId::Text);
    QCOMPARE(a[2]->text(), QStringLiteral("hello"));
    QCOMPARE(a[2]->style.fontPx, 24);
}

void TstAnnotationDocument::flattenNoOp()
{
    QImage base(40, 40, QImage::Format_ARGB32);
    base.fill(Qt::white);
    AnnotationDocument doc;                          // empty → flatten returns base
    const QImage out = doc.flatten(base, QRect(0, 0, 40, 40));
    QCOMPARE(out.size(), base.size());
    QCOMPARE(out.convertToFormat(QImage::Format_ARGB32),
             base.convertToFormat(QImage::Format_ARGB32));
}

void TstAnnotationDocument::flattenDrawsShape()
{
    QImage base(100, 100, QImage::Format_ARGB32);
    base.fill(Qt::white);
    AnnotationDocument doc;
    doc.add(rect({10, 10}, {90, 90}, Qt::red));     // red border, penWidth 4
    const QImage out = doc.flatten(base, QRect(0, 0, 100, 100));
    QCOMPARE(out.size(), base.size());
    // A pixel on the top border should now be strongly red (rect has no AA).
    const QColor c = out.pixelColor(50, 10);
    QVERIFY2(c.red() > 200 && c.green() < 90 && c.blue() < 90, qPrintable(c.name()));
    // A pixel far outside the border stays white.
    QCOMPARE(out.pixelColor(50, 50), QColor(Qt::white));
}

void TstAnnotationDocument::flattenIgnoresBaseDpr()
{
    // flatten() maps `sel` onto out's DEVICE pixels, so a base carrying a devicePixelRatio
    // must not shift anything: QPainter would otherwise work in logical px and scale every
    // item away from the origin (a pin's source keeps its capture dpr, which made committed
    // annotations jump bottom-right of the draft). Same base, same geometry, dpr 1 vs 2 →
    // identical pixels.
    QImage base(100, 100, QImage::Format_ARGB32);
    base.fill(Qt::white);
    QImage scaled = base;
    scaled.setDevicePixelRatio(2.0);

    AnnotationDocument doc;
    doc.add(rect({10, 10}, {90, 90}, Qt::red));
    const QImage plain = doc.flatten(base, QRect(0, 0, 100, 100));
    QImage out = doc.flatten(scaled, QRect(0, 0, 100, 100));

    QCOMPARE(out.devicePixelRatio(), 2.0);          // the caller's dpr is handed back
    out.setDevicePixelRatio(1.0);                   // ...but it moved no pixels
    QCOMPARE(out, plain);
}

void TstAnnotationDocument::redactBrushJsonRoundTrip()
{
    AnnotationDocument doc;
    Style s; s.penWidth = 22; s.redactStrength = 11;
    auto a = create(ToolId::MosaicBrush, s);          // brush variant + decoupled strength
    a->setGeometry({QPoint(5, 5), QPoint(50, 50)});
    doc.add(std::move(a));

    AnnotationDocument loaded;
    loaded.fromJson(doc.toJson());
    QCOMPARE(loaded.count(), 1);
    const auto& it = loaded.items().front();
    QCOMPARE(it->tool(), ToolId::MosaicBrush);
    QCOMPARE(it->style.redactStrength, 11);           // survives serialization
    QCOMPARE(it->style.penWidth, 22);
}

void TstAnnotationDocument::redactBrushFlatten()
{
    QImage base(100, 100, QImage::Format_ARGB32);     // fine vertical stripes
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x)
            base.setPixelColor(x, y, ((x / 4) % 2) ? QColor(Qt::white) : QColor(Qt::black));

    AnnotationDocument doc;
    Style s; s.penWidth = 20; s.redactStrength = 8;
    auto a = create(ToolId::MosaicBrush, s);
    a->setGeometry({QPoint(20, 50), QPoint(80, 50)}); // horizontal stroke across the middle
    doc.add(std::move(a));

    const QImage out = doc.flatten(base, QRect(0, 0, 100, 100));
    // Under the stroke: the stripes are averaged into a mid-gray, not pure b/w.
    const QColor c = out.pixelColor(50, 50);
    QVERIFY2(c.red() > 40 && c.red() < 215, qPrintable(c.name()));
    // Far from the stroke: pixels are untouched.
    QCOMPARE(out.pixelColor(50, 5), base.pixelColor(50, 5));
}

// A redaction rect dragged off the edge of the base samples a source rect that gets
// clipped to the image, so the sampled image is smaller than the rect it is painted
// into. Painting it over the whole rect stretched the content along the clipped axis
// -- and not just off-canvas: the stretch dragged wrong pixels across the *visible*
// part too. The overhanging rect must therefore render exactly like the same rect
// trimmed to the canvas, on both axes.
void TstAnnotationDocument::redactRectOffCanvasDoesNotStretch()
{
    QImage base(100, 100, QImage::Format_ARGB32);     // fine vertical stripes
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x)
            base.setPixelColor(x, y, ((x / 4) % 2) ? QColor(Qt::white) : QColor(Qt::black));

    const QRect sel(0, 0, 100, 100);
    auto flat = [&](ToolId t, QPoint a, QPoint b) {
        AnnotationDocument doc;
        Style s; s.redactStrength = 8;
        auto sh = create(t, s);
        sh->setGeometry({a, b});
        doc.add(std::move(sh));
        return doc.flatten(base, sel);
    };

    for (ToolId t : {ToolId::Mosaic, ToolId::Blur}) {
        // Hanging off the left edge vs. starting at it.
        QCOMPARE(flat(t, {-60, 20}, {60, 80}), flat(t, {0, 20}, {60, 80}));
        // Hanging off the top edge vs. starting at it.
        QCOMPARE(flat(t, {20, -40}, {80, 60}), flat(t, {20, 0}, {80, 60}));
    }
}

void TstAnnotationDocument::flattenAntialiases()
{
    // Every on-screen preview of an annotation is painted antialiased (the snip views and
    // the pin's paintEvent both set the hint), so flatten() must match — otherwise the
    // committed/saved result is rasterised aliased and its edges land up to a pixel off
    // the preview, biased toward the top-left. On a pin that seam is directly visible:
    // draw a shape (antialiased draft) and it shifts the moment you release.
    QImage base(40, 40, QImage::Format_ARGB32);
    base.fill(Qt::white);

    AnnotationDocument doc;
    Style s; s.color = Qt::black; s.penWidth = 3;
    auto line = create(ToolId::Arrow, s);           // a diagonal: maximally AA-sensitive
    line->setGeometry({QPoint(6, 5), QPoint(34, 33)});
    doc.add(std::move(line));

    const QImage out = doc.flatten(base, QRect(0, 0, 40, 40));

    // Antialiasing leaves partially-covered edge pixels; a purely aliased rasteriser
    // leaves only hard black or hard white.
    bool blended = false;
    for (int y = 0; y < out.height() && !blended; ++y)
        for (int x = 0; x < out.width() && !blended; ++x) {
            const int g = qGray(out.pixel(x, y));
            if (g > 40 && g < 215)
                blended = true;
        }
    QVERIFY2(blended, "flatten() rasterised aliased — its edges will not line up with the "
                      "antialiased on-screen preview");
}

void TstAnnotationDocument::resetDropsItemsAndUndo()
{
    // reset() is the "the subject these referred to is gone" path (PinModel::setSource
    // replacing the pixels). Unlike clearAll() it must NOT be undoable — otherwise a
    // Ctrl+Z would bring the annotations back onto an unrelated image.
    AnnotationDocument doc;
    doc.add(rect({0, 0}, {20, 20}));
    doc.add(rect({5, 5}, {25, 25}));
    QCOMPARE(doc.count(), 2);
    QVERIFY(doc.canUndo());

    doc.reset();
    QCOMPARE(doc.count(), 0);
    QVERIFY(!doc.canUndo());
    QVERIFY(!doc.canRedo());

    doc.undo();                       // no-op: the history is gone, not rewound
    QCOMPARE(doc.count(), 0);
}

void TstAnnotationDocument::textMirroredJsonRoundTrip()
{
    // The pin editor pre-mirrors text so a singly-flipped pin's flip cancels back to
    // readable; that flag has to survive a session restore.
    Style s;
    auto t = create(ToolId::Text, s);
    t->setText(QStringLiteral("hi"));
    t->setRotation(30.0);
    QVERIFY(!t->mirrored());                        // default: the snip case

    QJsonObject plain = t->toJson();
    QVERIFY(!plain.contains(QStringLiteral("mirrored")));   // omitted when false

    t->setMirrored(true);
    auto back = Annotation::fromJson(t->toJson());
    QVERIFY(back != nullptr);
    QVERIFY(back->mirrored());
    QCOMPARE(back->rotation(), 30.0);
    QCOMPARE(back->text(), QStringLiteral("hi"));
}

void TstAnnotationDocument::numberLabelCentredBoundsAndHitTest()
{
    // The stamped point is the badge CENTRE (not a top-left anchor) — the cursor
    // preview centres it the same way, so the click lands where it previewed.
    Style s;
    s.fontPx = 20;                                   // → 40px diameter
    auto a = create(ToolId::NumberLabel, s);
    a->setText(QStringLiteral("3"));
    a->setGeometry({QPoint(100, 100)});

    QCOMPARE(numberBadgeDiameter(s), 40.0);
    // Symmetric about the stamped point (QRect::center() rounds down on an even-sized
    // rect, so compare the margins instead).
    const QRect b = a->bounds();
    QVERIFY(b.width() >= 40);
    QVERIFY(qAbs((100 - b.left()) - (b.right() - 100)) <= 1);
    QVERIFY(qAbs((100 - b.top()) - (b.bottom() - 100)) <= 1);

    QVERIFY(a->hitTest(QPoint(100, 100)));           // centre
    QVERIFY(a->hitTest(QPoint(118, 100)));           // inside the disc
    QVERIFY(!a->hitTest(QPoint(130, 100)));          // outside it
    QVERIFY(!a->hitTest(QPoint(118, 118)));          // in the bounding box, off the disc

    // Multi-digit numbers keep the same footprint (the digits shrink instead).
    auto wide = create(ToolId::NumberLabel, s);
    wide->setText(QStringLiteral("128"));
    wide->setGeometry({QPoint(100, 100)});
    QCOMPARE(wide->bounds(), a->bounds());

    // An unnumbered badge is degenerate and must not enter the document.
    AnnotationDocument doc;
    auto blank = create(ToolId::NumberLabel, s);
    blank->setGeometry({QPoint(10, 10)});
    QCOMPARE(doc.add(std::move(blank)), 0);
}

void TstAnnotationDocument::numberLabelJsonRoundTrip()
{
    Style s;
    s.color = QColor(0x10, 0x20, 0x30);
    s.fontPx = 14;
    s.label = LabelStyle::Hollow;
    auto a = create(ToolId::NumberLabel, s);
    a->setText(QStringLiteral("7"));
    a->setGeometry({QPoint(42, 84)});

    auto back = Annotation::fromJson(a->toJson());
    QVERIFY(back != nullptr);
    QCOMPARE(back->tool(), ToolId::NumberLabel);
    QCOMPARE(back->text(), QStringLiteral("7"));
    QCOMPARE(back->geometry().first(), QPoint(42, 84));
    QCOMPARE(back->style.fontPx, 14.0);
    QVERIFY(back->style.label == LabelStyle::Hollow);

    // The numbering style travels with the badge; JSON written before it existed loads
    // as plain decimal.
    s.numberStyle = NumberStyle::LowerRoman;
    auto roman = create(ToolId::NumberLabel, s);
    roman->setText(QStringLiteral("4"));
    roman->setGeometry({QPoint(1, 2)});
    QJsonObject o = roman->toJson();
    QVERIFY(Annotation::fromJson(o)->style.numberStyle == NumberStyle::LowerRoman);
    o.remove(QStringLiteral("numberStyle"));
    QVERIFY(Annotation::fromJson(o)->style.numberStyle == NumberStyle::Decimal);
}

void TstAnnotationDocument::numberStyleFormatting()
{
    // Badges always STORE a decimal ordinal; the style only picks the glyphs it reads in.
    QCOMPARE(formatNumber(7, NumberStyle::Decimal), QStringLiteral("7"));

    // Bijective base-26 — there is no "zero" letter, so Z rolls over to AA (not BA).
    QCOMPARE(formatNumber(1, NumberStyle::UpperAlpha), QStringLiteral("A"));
    QCOMPARE(formatNumber(26, NumberStyle::UpperAlpha), QStringLiteral("Z"));
    QCOMPARE(formatNumber(27, NumberStyle::UpperAlpha), QStringLiteral("AA"));
    QCOMPARE(formatNumber(52, NumberStyle::UpperAlpha), QStringLiteral("AZ"));
    QCOMPARE(formatNumber(703, NumberStyle::UpperAlpha), QStringLiteral("AAA"));
    QCOMPARE(formatNumber(27, NumberStyle::LowerAlpha), QStringLiteral("aa"));

    // Subtractive roman numerals.
    QCOMPARE(formatNumber(4, NumberStyle::UpperRoman), QStringLiteral("IV"));
    QCOMPARE(formatNumber(9, NumberStyle::UpperRoman), QStringLiteral("IX"));
    QCOMPARE(formatNumber(40, NumberStyle::UpperRoman), QStringLiteral("XL"));
    QCOMPARE(formatNumber(3999, NumberStyle::UpperRoman), QStringLiteral("MMMCMXCIX"));
    QCOMPARE(formatNumber(14, NumberStyle::LowerRoman), QStringLiteral("xiv"));

    // Out of range falls back to the decimal digits, so a badge is never blank: neither
    // system has a zero, and roman stops at 3999.
    QCOMPARE(formatNumber(0, NumberStyle::UpperAlpha), QStringLiteral("0"));
    QCOMPARE(formatNumber(-2, NumberStyle::LowerRoman), QStringLiteral("-2"));
    QCOMPARE(formatNumber(4000, NumberStyle::UpperRoman), QStringLiteral("4000"));

    // A wide numeral keeps the badge's footprint (the glyphs shrink to fit, as digits do).
    Style s;
    s.fontPx = 20;
    s.numberStyle = NumberStyle::UpperRoman;
    auto wide = create(ToolId::NumberLabel, s);
    wide->setText(QStringLiteral("38"));            // XXXVIII
    wide->setGeometry({QPoint(100, 100)});
    QCOMPARE(numberBadgeDiameter(s), 40.0);
    QVERIFY(wide->hitTest(QPoint(118, 100)));
    QVERIFY(!wide->hitTest(QPoint(130, 100)));
}

void TstAnnotationDocument::setTextsIsOneUndoStep()
{
    // A number-label renumber changes TWO badges (the edited one and whichever held the
    // number it took), and one Ctrl+Z must revert both. Reproduces the reported flow:
    // badges 1,2,3,4 → step the 2nd up → 1,3,2,4 → step it up again → 1,4,2,3.
    AnnotationDocument doc;
    Style s;
    QVector<int> ids;
    for (int n = 1; n <= 4; ++n) {
        auto a = create(ToolId::NumberLabel, s);
        a->setText(QString::number(n));
        a->setGeometry({QPoint(n * 50, 50)});
        ids << doc.add(std::move(a));
    }
    QCOMPARE(doc.count(), 4);

    auto numbers = [&doc, &ids] {
        QStringList out;
        for (int id : ids)
            for (const auto& it : doc.items())
                if (it->id == id) { out << it->text(); break; }
        return out.join(QLatin1Char(','));
    };
    QCOMPARE(numbers(), QStringLiteral("1,2,3,4"));

    // One edit session on badge #2: 2→3 (swapping with #3), then 3→4 (swapping with #4).
    const QHash<int, QString> before{{ids[0], "1"}, {ids[1], "2"}, {ids[2], "3"}, {ids[3], "4"}};
    const QHash<int, QString> after {{ids[0], "1"}, {ids[1], "4"}, {ids[2], "2"}, {ids[3], "3"}};
    doc.setTexts(before, after);
    QCOMPARE(numbers(), QStringLiteral("1,4,2,3"));

    // A SINGLE undo puts every badge back — not just the one that was edited.
    QVERIFY(doc.canUndo());
    doc.undo();
    QCOMPARE(numbers(), QStringLiteral("1,2,3,4"));
    doc.redo();
    QCOMPARE(numbers(), QStringLiteral("1,4,2,3"));

    // A no-op renumber must not push an undo entry (Esc/unchanged commit).
    const int depthBefore = doc.canUndo() ? 1 : 0;
    doc.setTexts(after, after);
    doc.undo();
    QCOMPARE(numbers(), QStringLiteral("1,2,3,4"));   // undid the real edit, not a no-op
    QCOMPARE(depthBefore, 1);
}

void TstAnnotationDocument::draggedMoveIsOneUndoStep()
{
    // Dragging an annotation translates it live on every mouse-move; recording each of
    // those as its own command made one drag take dozens of Ctrl+Z. moveApplied() records
    // the whole gesture as a single step.
    AnnotationDocument doc;
    auto a = create(ToolId::NumberLabel, Style{});
    a->setText(QStringLiteral("1"));
    a->setGeometry({QPoint(100, 100)});
    const int id = doc.add(std::move(a));

    auto centre = [&doc, id]() -> QPoint {
        for (const auto& it : doc.items())
            if (it->id == id) return it->geometry().first();
        return {};
    };
    QCOMPARE(centre(), QPoint(100, 100));

    // Simulate a 30-event drag: applied live, straight on the annotation.
    QPoint total;
    for (int i = 0; i < 30; ++i) {
        const QPoint step(2, 1);
        for (const auto& it : doc.items())
            if (it->id == id) it->translate(step);
        total += step;
    }
    QCOMPARE(centre(), QPoint(160, 130));

    doc.moveApplied(id, total);
    QCOMPARE(centre(), QPoint(160, 130));      // net position unchanged by the bookkeeping

    // ONE undo returns it to the start — not 30.
    doc.undo();
    QCOMPARE(centre(), QPoint(100, 100));
    // ...and the very next undo is the original add, proving the drag left exactly one
    // command behind (before the fix there were 30 move commands stacked here).
    doc.undo();
    QCOMPARE(doc.count(), 0);
    QVERIFY(!doc.canUndo());

    doc.redo();
    doc.redo();
    QCOMPARE(centre(), QPoint(160, 130));
}

void TstAnnotationDocument::wheelResizeIsOneUndoStep()
{
    // A wheel resize is a burst of events with no release; like a drag it is applied live
    // and recorded once (the UI flushes when the wheel goes idle).
    AnnotationDocument doc;
    Style s;
    s.fontPx = 16;
    auto a = create(ToolId::NumberLabel, s);
    a->setText(QStringLiteral("1"));
    a->setGeometry({QPoint(50, 50)});
    const int id = doc.add(std::move(a));

    auto px = [&doc, id]() -> qreal {
        for (const auto& it : doc.items())
            if (it->id == id) return it->style.fontPx;
        return -1;
    };
    QCOMPARE(px(), 16.0);

    // 10 wheel notches, applied live straight on the annotation.
    for (int i = 0; i < 10; ++i)
        for (const auto& it : doc.items())
            if (it->id == id) it->style.fontPx += 1;
    QCOMPARE(px(), 26.0);

    doc.resizeApplied(id, 16.0);
    QCOMPARE(px(), 26.0);                  // net size unchanged by the bookkeeping

    // ONE undo restores the original size, and the next undo is the add — so the whole
    // burst left exactly one command.
    doc.undo();
    QCOMPARE(px(), 16.0);
    doc.undo();
    QCOMPARE(doc.count(), 0);
    doc.redo();
    doc.redo();
    QCOMPARE(px(), 26.0);

    // A burst that nets no change must not push anything.
    doc.resizeApplied(id, 26.0);
    doc.undo();
    QCOMPARE(px(), 16.0);                  // undid the real resize, not a no-op entry
}

void TstAnnotationDocument::deleteWithRenumberIsOneUndoStep()
{
    // Deleting a number label also renumbers the badges above it to close the gap
    // (1,2,3,4 minus the 2 → 1,2,3). That is a remove + a setTexts, and ONE Ctrl+Z must
    // bring back both the badge and the old numbering — hence the macro.
    AnnotationDocument doc;
    QVector<int> ids;
    for (int n = 1; n <= 4; ++n) {
        auto a = create(ToolId::NumberLabel, Style{});
        a->setText(QString::number(n));
        a->setGeometry({QPoint(n * 50, 50)});
        ids << doc.add(std::move(a));
    }
    auto numbers = [&doc, &ids] {
        QStringList out;
        for (int id : ids)
            for (const auto& it : doc.items())
                if (it->id == id) { out << it->text(); break; }
        return out.join(QLatin1Char(','));
    };
    QCOMPARE(numbers(), QStringLiteral("1,2,3,4"));

    // Delete badge #2 (number 2) and close the gap, as the UI does.
    const QHash<int, QString> gapBefore{{ids[2], "3"}, {ids[3], "4"}};
    const QHash<int, QString> gapAfter {{ids[2], "2"}, {ids[3], "3"}};
    doc.beginMacro();
    doc.remove(ids[1]);
    doc.setTexts(gapBefore, gapAfter);
    doc.endMacro();

    QCOMPARE(doc.count(), 3);
    QCOMPARE(numbers(), QStringLiteral("1,2,3"));      // ids[1] gone; the rest closed up

    // ONE undo restores the badge AND the original numbering.
    doc.undo();
    QCOMPARE(doc.count(), 4);
    QCOMPARE(numbers(), QStringLiteral("1,2,3,4"));

    doc.redo();
    QCOMPARE(doc.count(), 3);
    QCOMPARE(numbers(), QStringLiteral("1,2,3"));
}

// The Line tool's curve mode (Style::curved). Three properties, because between them
// they pin down what the toggle promises: the curve visits every clicked vertex (so a
// click means the same thing curved or straight), it genuinely leaves the straight
// chords in between, and the flag survives a save/load.
void TstAnnotationDocument::curvedLineFollowsVertices()
{
    const QVector<QPoint> verts{{0, 0}, {200, 0}, {200, 200}, {0, 200}};
    Style straightStyle; straightStyle.penWidth = 2;
    Style curvedStyle = straightStyle; curvedStyle.curved = true;

    auto curved = create(ToolId::Line, curvedStyle);
    curved->setGeometry(verts);
    auto straight = create(ToolId::Line, straightStyle);
    straight->setGeometry(verts);

    for (const QPoint& v : verts)
        QVERIFY(curved->hitTest(v));

    // Somewhere in the plane the curve covers ground the straight polyline does not —
    // it bows outside the chords at every interior vertex.
    int onlyCurved = 0;
    for (int y = -20; y <= 220; y += 5)
        for (int x = -20; x <= 220; x += 5)
            if (curved->hitTest({x, y}) && !straight->hitTest({x, y}))
                ++onlyCurved;
    QVERIFY(onlyCurved > 0);

    AnnotationDocument doc;
    doc.add(std::move(curved));
    AnnotationDocument loaded;
    loaded.fromJson(doc.toJson());
    QCOMPARE(loaded.count(), 1);
    QVERIFY(loaded.items().front()->style.curved);
}

// A TAPERED line's silhouette is ONE closed outline, so wherever the line crosses itself
// the outline crosses too — and Qt's default odd-even fill rule counts that overlap as
// *outside*, punching a hole clean through the stroke. Not curve-specific (a straight
// bow-tie does it too), but curves made it easy to hit, which is how it surfaced. Only
// the tapered styles build that silhouette; a plain pen stroke has never had the problem.
//
// The geometry is a bow-tie chosen so the crossing is exact: (20,20)→(120,120) is the
// line y = x, (120,20)→(20,120) is y = 140 - x, so they meet at (70,70) — dead centre of
// the overlap, and far from both the tail and the arrowhead.
void TstAnnotationDocument::selfCrossingArrowHasNoHoles()
{
    constexpr int kW = 140, kH = 140;
    QImage base(kW, kH, QImage::Format_ARGB32);
    base.fill(Qt::white);

    Style s;
    s.penWidth = 8;
    s.lineStyle = LineStyle::Tapered;     // the only shaft that is a filled silhouette
    s.endCap = CapStyle::ArrowSharp;
    s.color = Qt::red;
    auto line = create(ToolId::Line, s);
    line->setGeometry({{20, 20}, {120, 120}, {120, 20}, {20, 120}});
    AnnotationDocument doc;
    doc.add(std::move(line));
    const QImage out = doc.flatten(base, QRect(0, 0, kW, kH)).convertToFormat(QImage::Format_ARGB32);

    QCOMPARE(out.pixelColor(70, 70), QColor(Qt::red));
}

// The Line tool only started honouring lineStyle when the ends became independent — the
// old renderer drew the shaft as a filled silhouette whenever there was a head, which no
// pen pattern could reach. Walk the drawn row and count ink→gap transitions.
void TstAnnotationDocument::dashedShaftHasGaps()
{
    constexpr int kW = 200, kH = 40;
    QImage base(kW, kH, QImage::Format_ARGB32);
    base.fill(Qt::white);

    Style s;
    s.penWidth = 5;
    s.color = Qt::red;
    s.startCap = s.endCap = CapStyle::None;   // caps would confuse the run counting
    s.lineStyle = LineStyle::Dash;
    auto line = create(ToolId::Line, s);
    line->setGeometry({{10, kH / 2}, {kW - 10, kH / 2}});
    AnnotationDocument doc;
    doc.add(std::move(line));
    const QImage out = doc.flatten(base, QRect(0, 0, kW, kH)).convertToFormat(QImage::Format_ARGB32);

    int gaps = 0;
    bool wasInk = false;
    for (int x = 0; x < kW; ++x) {
        const bool ink = out.pixelColor(x, kH / 2).red() > 200
                      && out.pixelColor(x, kH / 2).green() < 100;
        if (wasInk && !ink)
            ++gaps;
        wasInk = ink;
    }
    QVERIFY2(gaps >= 3, qPrintable(QStringLiteral("dashed shaft drew %1 gap(s)").arg(gaps)));

    // ...and the same geometry solid must have exactly one run (no gaps before the end).
    Style solid = s;
    solid.lineStyle = LineStyle::Solid;
    auto line2 = create(ToolId::Line, solid);
    line2->setGeometry({{10, kH / 2}, {kW - 10, kH / 2}});
    AnnotationDocument doc2;
    doc2.add(std::move(line2));
    const QImage out2 = doc2.flatten(base, QRect(0, 0, kW, kH)).convertToFormat(QImage::Format_ARGB32);
    int gaps2 = 0;
    wasInk = false;
    for (int x = 0; x < kW; ++x) {
        const bool ink = out2.pixelColor(x, kH / 2).red() > 200
                      && out2.pixelColor(x, kH / 2).green() < 100;
        if (wasInk && !ink)
            ++gaps2;
        wasInk = ink;
    }
    QCOMPARE(gaps2, 1);
}

// The same cap at the same pen width comes out the same size on every line style — with
// ONE deliberate exception. A tapered-hollow shaft is a much wider ribbon than the pen,
// so a pen-sized head there is swallowed by it and the line reads as a wedge with a nub;
// its caps are scaled up on purpose (capScale in paintLineArrow). This pins both halves:
// the four ordinary styles must agree, and the exception must stay an exception rather
// than silently spreading or silently disappearing.
//
// Measured as the drawn height at the head — for a horizontal line the arrowhead is the
// tallest thing in the image, so the tallest ink column is the head's full span.
void TstAnnotationDocument::capSizeMatchesExceptTaperedHollow()
{
    constexpr int kW = 240, kH = 90;
    auto headSpan = [&](LineStyle ls) {
        QImage base(kW, kH, QImage::Format_ARGB32);
        base.fill(Qt::white);
        Style s;
        s.penWidth = 5;
        s.color = Qt::red;
        s.lineStyle = ls;
        s.endCap = CapStyle::ArrowHollow;
        auto a = create(ToolId::Line, s);
        a->setGeometry({{15, kH / 2}, {kW - 25, kH / 2}});
        AnnotationDocument d;
        d.add(std::move(a));
        const QImage out = d.flatten(base, QRect(0, 0, kW, kH)).convertToFormat(QImage::Format_ARGB32);
        int tallest = 0;
        for (int x = 0; x < kW; ++x) {
            int top = -1, bot = -1;
            for (int y = 0; y < kH; ++y) {
                const QColor c = out.pixelColor(x, y);
                if (c.red() > 150 && c.green() < 110) {
                    if (top < 0) top = y;
                    bot = y;
                }
            }
            if (top >= 0) tallest = qMax(tallest, bot - top + 1);
        }
        return tallest;
    };

    const int ref = headSpan(LineStyle::Solid);
    QVERIFY2(ref > 0, "the reference head drew nothing");
    for (LineStyle ls : {LineStyle::Dash, LineStyle::DashDot, LineStyle::Tapered}) {
        const int got = headSpan(ls);
        // 3px of slack absorbs antialiasing and, for the tapered style, the shaft
        // outline meeting the head — but not a head that is a different size.
        QVERIFY2(qAbs(got - ref) <= 3,
                 qPrintable(QStringLiteral("head span %1 for lineStyle %2, but %3 for solid")
                                .arg(got).arg(lineStyleToString(ls)).arg(ref)));
    }

    // The exception. Ranged rather than exact: the head scales but its outline weight
    // does not, so the drawn span grows by less than capScale itself.
    const int hollow = headSpan(LineStyle::TaperedHollow);
    QVERIFY2(hollow > ref * 1.3 && hollow < ref * 2.0,
             qPrintable(QStringLiteral("tapered-hollow head span %1 against %2 for solid — "
                                       "expected clearly larger but not double")
                            .arg(hollow).arg(ref)));
}

void TstAnnotationDocument::lineCapsJsonRoundTrip()
{
    Style s;
    s.startCap = CapStyle::HollowDiamond;
    s.endCap = CapStyle::Bar;
    s.lineStyle = LineStyle::TaperedHollow;
    auto line = create(ToolId::Line, s);
    line->setGeometry({{0, 0}, {50, 50}});
    AnnotationDocument doc;
    doc.add(std::move(line));

    AnnotationDocument loaded;
    loaded.fromJson(doc.toJson());
    QCOMPARE(loaded.count(), 1);
    const Style& got = loaded.items().front()->style;
    QCOMPARE(got.startCap, CapStyle::HollowDiamond);
    QCOMPARE(got.endCap, CapStyle::Bar);
    QCOMPARE(got.lineStyle, LineStyle::TaperedHollow);
}

// Sessions and history written before the ends became independent stored a single
// `arrow` field conflating the head with the shaft taper. Reading one must still produce
// the arrow it was saved with, or every pinned annotation silently loses its head.
void TstAnnotationDocument::legacyArrowFieldMigrates()
{
    auto legacy = [](const QString& arrow) {
        QJsonObject o;
        o[QStringLiteral("tool")] = QStringLiteral("line");
        o[QStringLiteral("pts")] = QJsonArray{0, 0, 50, 50};
        o[QStringLiteral("arrow")] = arrow;
        return Annotation::fromJson(o)->style;
    };

    const Style none = legacy(QStringLiteral("none"));
    QCOMPARE(none.endCap, CapStyle::None);
    QCOMPARE(none.lineStyle, LineStyle::Solid);

    const Style solid = legacy(QStringLiteral("solid"));
    QCOMPARE(solid.endCap, CapStyle::ArrowSharp);
    QCOMPARE(solid.lineStyle, LineStyle::Solid);

    const Style tapered = legacy(QStringLiteral("tapered_solid"));
    QCOMPARE(tapered.endCap, CapStyle::ArrowSharp);
    QCOMPARE(tapered.lineStyle, LineStyle::Tapered);

    const Style hollow = legacy(QStringLiteral("tapered_hollow"));
    QCOMPARE(hollow.endCap, CapStyle::ArrowSharp);
    QCOMPARE(hollow.lineStyle, LineStyle::TaperedHollow);

    // Neither field present at all (a pre-arrow file): no decoration, not the default one.
    QCOMPARE(legacy(QString()).endCap, CapStyle::None);
}

QTEST_GUILESS_MAIN(TstAnnotationDocument)
#include "tst_annotation_document.moc"
