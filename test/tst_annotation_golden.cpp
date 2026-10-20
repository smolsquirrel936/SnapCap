#include <QtTest>
#include <QDir>
#include <QImage>
#include <QPainter>

#include "core/annotation/Annotation.h"
#include "core/annotation/AnnotationDocument.h"

using namespace annotation;

// Stage 4: per-shape GOLDEN-IMAGE tests. Each case flattens exactly one shape onto a
// fixed, code-generated base and compares the result to a committed reference PNG in
// test/golden/. This is what catches a rendering regression that the property tests in
// tst_annotation_document (which assert a handful of individual pixels) would sail past.
//
// Deliberately NOT covered: Text and NumberLabel. Their output depends on font
// rasterisation, which differs by machine, installed fonts and Qt build, so a golden for
// them would fail for reasons unrelated to our code. They keep their JSON/geometry
// coverage in tst_annotation_document.
//
// Regenerating: run with SNAPCAP_UPDATE_GOLDEN=1 to (re)write the PNGs instead of
// comparing. A golden records whatever the code currently does — including a bug — so
// look at each image before committing it.
namespace {

constexpr int kW = 160;
constexpr int kH = 100;
// Max per-channel difference tolerated. Non-zero because Qt's antialiasing can land a
// boundary pixel one step either way across platforms/versions; large enough to absorb
// that, small enough that any real change in what is drawn still fails.
constexpr int kEpsilon = 2;

QString goldenDir()
{
    return QStringLiteral(SNAPCAP_SOURCE_DIR "/test/golden");
}

// A deterministic, busy-ish base: a coloured gradient crossed by a checkerboard, so the
// redaction tools (which average real pixels) produce output that actually varies.
QImage makeBase()
{
    QImage img(kW, kH, QImage::Format_ARGB32);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const bool check = ((x / 8) + (y / 8)) % 2 == 0;
            const int r = 40 + (x * 200) / kW;
            const int g = 40 + (y * 200) / kH;
            const int b = check ? 210 : 60;
            img.setPixelColor(x, y, QColor(r, g, b));
        }
    }
    return img;
}

std::unique_ptr<Annotation> make(ToolId t, const Style& s, const QVector<QPoint>& pts)
{
    auto a = create(t, s);
    a->setGeometry(pts);
    return a;
}

// A freehand stroke (pencil / marker / redaction brush / eraser): a coarse arc, kept as
// integer points so the golden is reproducible.
QVector<QPoint> stroke()
{
    QVector<QPoint> pts;
    for (int x = 20; x <= 140; x += 6)
        pts.append(QPoint(x, 60 - (x - 80) * (x - 80) / 90));
    return pts;
}

// Fills `doc` with the single shape (or shape-under-eraser) for a case name.
// AnnotationDocument is a QObject, so it is filled in place rather than returned.
void buildDoc(AnnotationDocument& doc, const QString& name)
{
    Style s;
    s.color = QColor(0xff, 0x3b, 0x30);
    s.penWidth = 4;

    const QVector<QPoint> box{{20, 20}, {140, 80}};

    if (name == QLatin1String("rect_hollow")) {
        doc.add(make(ToolId::Rect, s, box));
    } else if (name == QLatin1String("rect_filled")) {
        s.filled = true;
        s.color = QColor(0x2d, 0x7d, 0xff);
        doc.add(make(ToolId::Rect, s, box));
    } else if (name == QLatin1String("rect_dashed")) {
        s.lineStyle = LineStyle::Dash;
        doc.add(make(ToolId::Rect, s, box));
    } else if (name == QLatin1String("ellipse")) {
        s.color = QColor(0x22, 0xaa, 0x55);
        doc.add(make(ToolId::Ellipse, s, box));
    // The line cases below set caps explicitly in BOTH directions: Style's default endCap
    // is an arrow, so "plain line" has to say so rather than relying on the default.
    } else if (name == QLatin1String("line_plain")) {
        s.penWidth = 5;
        s.endCap = CapStyle::None;
        doc.add(make(ToolId::Line, s, {{20, 80}, {140, 20}}));
    } else if (name == QLatin1String("line_arrow_solid")) {
        s.penWidth = 5;
        s.endCap = CapStyle::ArrowSharp;
        doc.add(make(ToolId::Line, s, {{20, 80}, {140, 20}}));
    } else if (name == QLatin1String("line_arrow_tapered")) {
        s.penWidth = 5;
        s.lineStyle = LineStyle::Tapered;
        s.endCap = CapStyle::ArrowSharp;
        doc.add(make(ToolId::Line, s, {{20, 80}, {140, 20}}));
    } else if (name == QLatin1String("line_arrow_round")) {
        s.penWidth = 5;
        s.endCap = CapStyle::ArrowRound;
        doc.add(make(ToolId::Line, s, {{20, 80}, {140, 20}}));
    } else if (name == QLatin1String("line_arrow_chevron")) {
        s.penWidth = 5;
        s.endCap = CapStyle::ArrowChevron;
        doc.add(make(ToolId::Line, s, {{20, 80}, {140, 20}}));
    } else if (name == QLatin1String("line_caps_dot_hollow_arrow")) {
        // Both ends decorated, and a HOLLOW end: the case that proves the shaft is
        // trimmed at each end rather than poking out through the outline.
        s.penWidth = 5;
        s.startCap = CapStyle::Dot;
        s.endCap = CapStyle::ArrowHollow;
        doc.add(make(ToolId::Line, s, {{20, 80}, {140, 20}}));
    } else if (name == QLatin1String("line_caps_diamond_bar")) {
        s.penWidth = 5;
        s.startCap = CapStyle::HollowDiamond;
        s.endCap = CapStyle::Bar;
        doc.add(make(ToolId::Line, s, {{20, 80}, {140, 20}}));
    } else if (name == QLatin1String("line_dashed_arrow")) {
        // A dash pattern on the shaft with a solid head — the combination the old
        // one-field arrow model could not express at all.
        s.penWidth = 5;
        s.lineStyle = LineStyle::Dash;
        s.startCap = CapStyle::HollowDot;
        s.endCap = CapStyle::ArrowSharp;
        doc.add(make(ToolId::Line, s, {{20, 80}, {140, 20}}));
    } else if (name == QLatin1String("line_tapered_hollow")) {
        // A hollow shaft with a SOLID head: the head must stay filled, matching the cap
        // dropdown's icon, rather than being swept into the stroked outline.
        s.penWidth = 5;
        s.lineStyle = LineStyle::TaperedHollow;
        s.endCap = CapStyle::ArrowSharp;
        doc.add(make(ToolId::Line, s, {{20, 80}, {140, 20}}));
    } else if (name == QLatin1String("line_tapered_hollow_all")) {
        // Hollow shaft + hollow head: the one combination that merges into a single
        // continuous outline with no internal seam.
        s.penWidth = 5;
        s.lineStyle = LineStyle::TaperedHollow;
        s.endCap = CapStyle::ArrowHollow;
        doc.add(make(ToolId::Line, s, {{20, 80}, {140, 20}}));
    } else if (name == QLatin1String("line_curved")) {
        s.penWidth = 5;
        s.curved = true;
        s.endCap = CapStyle::None;
        doc.add(make(ToolId::Line, s, {{20, 80}, {60, 30}, {100, 70}, {140, 25}}));
    } else if (name == QLatin1String("line_curved_arrow")) {
        // Curve + tapered head: the case that exercises the shaft trim, since a
        // flattened curve's last vertices sit inside the arrowhead.
        s.penWidth = 5;
        s.curved = true;
        s.lineStyle = LineStyle::Tapered;
        s.endCap = CapStyle::ArrowSharp;
        doc.add(make(ToolId::Line, s, {{20, 80}, {60, 30}, {100, 70}, {140, 25}}));
    } else if (name == QLatin1String("line_curved_self_cross")) {
        // A curve that crosses ITSELF. The tapered silhouette is one self-intersecting
        // outline, so under the default odd-even fill every crossing punched a hole;
        // this case is what holds the winding fill in place.
        s.penWidth = 5;
        s.curved = true;
        s.lineStyle = LineStyle::Tapered;
        s.endCap = CapStyle::ArrowSharp;
        doc.add(make(ToolId::Line, s, {{20, 70}, {130, 25}, {30, 25}, {140, 70}}));
    } else if (name == QLatin1String("linestrip")) {
        doc.add(make(ToolId::LineStrip, s, {{20, 80}, {60, 30}, {100, 70}, {140, 25}}));
    } else if (name == QLatin1String("pencil")) {
        doc.add(make(ToolId::Pencil, s, stroke()));
    } else if (name == QLatin1String("marker")) {
        s.penWidth = 4;                 // MarkerShape draws at penWidth * 4 (a 16px nib)
        s.color = QColor(0xff, 0xd6, 0x00);
        doc.add(make(ToolId::Marker, s, stroke()));
    } else if (name == QLatin1String("marker_rect")) {
        s.color = QColor(0xff, 0xd6, 0x00);
        doc.add(make(ToolId::MarkerRect, s, {{20, 30}, {140, 70}}));
    } else if (name == QLatin1String("mosaic")) {
        s.redactStrength = 8;
        doc.add(make(ToolId::Mosaic, s, box));
    } else if (name == QLatin1String("blur")) {
        s.redactStrength = 6;
        doc.add(make(ToolId::Blur, s, box));
    } else if (name == QLatin1String("mosaic_brush")) {
        s.penWidth = 20;
        s.redactStrength = 8;
        doc.add(make(ToolId::MosaicBrush, s, stroke()));
    } else if (name == QLatin1String("blur_brush")) {
        s.penWidth = 20;
        s.redactStrength = 6;
        doc.add(make(ToolId::BlurBrush, s, stroke()));
    } else if (name == QLatin1String("eraser")) {
        // The eraser paints the clean base back over earlier annotations, so it only
        // means anything with something underneath it.
        doc.add(make(ToolId::Rect, s, box));
        Style e; e.penWidth = 18;
        doc.add(make(ToolId::Eraser, e, stroke()));
    } else if (name == QLatin1String("eraser_rect")) {
        doc.add(make(ToolId::Rect, s, box));
        Style e;
        doc.add(make(ToolId::EraserRect, e, {{40, 35}, {120, 65}}));
    }
}

} // namespace

class TstAnnotationGolden : public QObject
{
    Q_OBJECT
private slots:
    void shape_data();
    void shape();
};

void TstAnnotationGolden::shape_data()
{
    QTest::addColumn<QString>("name");
    for (const char* n : {"rect_hollow", "rect_filled", "rect_dashed", "ellipse", "line_plain",
                          "line_arrow_solid", "line_arrow_tapered", "line_arrow_round",
                          "line_arrow_chevron", "line_caps_dot_hollow_arrow",
                          "line_caps_diamond_bar", "line_dashed_arrow", "line_tapered_hollow",
                          "line_tapered_hollow_all",
                          "line_curved", "line_curved_arrow", "line_curved_self_cross",
                          "linestrip", "pencil",
                          "marker", "marker_rect", "mosaic", "blur", "mosaic_brush",
                          "blur_brush", "eraser", "eraser_rect"})
        QTest::newRow(n) << QString::fromLatin1(n);
}

void TstAnnotationGolden::shape()
{
    QFETCH(QString, name);

    const QImage base = makeBase();
    AnnotationDocument doc;
    buildDoc(doc, name);
    const QImage out = doc.flatten(base, QRect(0, 0, kW, kH));
    QVERIFY2(!out.isNull(), "flatten returned a null image");

    const QString path = goldenDir() + QLatin1Char('/') + name + QStringLiteral(".png");

    if (!qEnvironmentVariableIsEmpty("SNAPCAP_UPDATE_GOLDEN")) {
        QDir().mkpath(goldenDir());
        QVERIFY2(out.save(path, "PNG"), qPrintable(QStringLiteral("cannot write %1").arg(path)));
        QSKIP("golden regenerated (SNAPCAP_UPDATE_GOLDEN set)");
    }

    QImage want;
    QVERIFY2(want.load(path),
             qPrintable(QStringLiteral("missing golden %1 — regenerate with "
                                       "SNAPCAP_UPDATE_GOLDEN=1").arg(path)));
    want = want.convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(out.size(), want.size());

    int worst = 0, offending = 0;
    QPoint where;
    for (int y = 0; y < out.height(); ++y) {
        for (int x = 0; x < out.width(); ++x) {
            const QColor a = out.pixelColor(x, y);
            const QColor b = want.pixelColor(x, y);
            const int d = std::max({qAbs(a.red()   - b.red()),
                                    qAbs(a.green() - b.green()),
                                    qAbs(a.blue()  - b.blue()),
                                    qAbs(a.alpha() - b.alpha())});
            if (d > kEpsilon) {
                ++offending;
                if (d > worst) { worst = d; where = QPoint(x, y); }
            }
        }
    }

    if (offending > 0) {
        // Leave the produced image beside the golden so the two can be diffed by eye.
        const QString actual = goldenDir() + QLatin1Char('/') + name + QStringLiteral("_actual.png");
        out.save(actual, "PNG");
        QFAIL(qPrintable(QStringLiteral("%1: %2 pixel(s) differ by more than %3 "
                                        "(worst %4 at %5,%6)\n  golden: %7\n  actual: %8")
                             .arg(name).arg(offending).arg(kEpsilon).arg(worst)
                             .arg(where.x()).arg(where.y()).arg(path, actual)));
    }
}

QTEST_MAIN(TstAnnotationGolden)
#include "tst_annotation_golden.moc"
