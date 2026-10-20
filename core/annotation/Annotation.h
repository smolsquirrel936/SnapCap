#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <memory>

class QPainter;
class QPainterPath;
class QJsonObject;

// The annotation model — geometry, not pixels. Each annotation stores logical
// coordinates (global virtual-desktop logical, the same space as SelectionModel
// and the snip overlay) plus a Style, and knows how to paint itself and hit-test
// a point. Storing geometry (not a rasterized layer) lets annotations re-render
// at any scale, serialize compactly (JSON), and survive a flatten at the
// composited image's device resolution. Lives in core/ (no widgets/Win32) so the
// document logic is headlessly unit-testable; QPainter/QImage from Qt Gui are a
// deliberate cross-platform exception (like core/ImageExport). Stage 4.
namespace annotation {

// Every drawing tool. Mirrors Features.md §2 (free tier). Eraser paints the
// original base pixels back over earlier annotations beneath its stroke (a
// freehand circle brush); EraserRect does the same over a dragged rectangle.
enum class ToolId {
    None,
    Rect, Ellipse, Line, Arrow, LineStrip, Pencil, Marker, MarkerRect, Mosaic, Blur, Text, Eraser, EraserRect,
    // Brush (freehand-stroke) variants of the redaction tools; Mosaic/Blur stay the
    // rectangle-drag variants. The PaintBar redact sub-row picks type × application mode.
    MosaicBrush, BlurBrush,
    // Auto-numbering badge (click to stamp 1, 2, 3, …). Its three looks are a Style
    // field (LabelStyle), not separate tools — same pattern as the line tool's end caps
    // (Style::startCap / endCap).
    NumberLabel
};

QString toolIdToString(ToolId t);
ToolId toolIdFromString(const QString& s);

// What decorates ONE end of a Line. The Line tool's sub-bar carries two of these
// dropdowns — one per end — so the two ends are independent (`Style::startCap` /
// `Style::endCap`). Order mirrors the dropdown.
//
// The four arrows are four *shapes*, not one shape at four sizes:
//   ArrowSharp   — slender, sharp barbs, back edge notched forward (the classic swept head)
//   ArrowRound   — the same notched head with rounded barbs/corners
//   ArrowChevron — an open ">" of two strokes meeting at the tip, drawn at the pen width
//                  (no fill and no back edge at all)
//   ArrowHollow  — a stroked outline triangle (the counterpart of HollowDot/HollowDiamond)
// Every cap's size rides on `penWidth`, so it inherits the logical→capture px scaling
// SnipOverlay::captureStyle() already applies.
enum class CapStyle {
    None,
    ArrowSharp, ArrowRound, ArrowChevron, ArrowHollow,
    Dot, HollowDot, Diamond, HollowDiamond, Bar
};
QString capStyleToString(CapStyle c);
CapStyle capStyleFromString(const QString& s);

// Stroke style for the tools whose 2nd-level toolbar offers the line-style dropdown:
// Rect and Ellipse (hollow only — a filled shape has no stroke), Pencil, and the Line
// tool. The first five are dash patterns mapping 1:1 onto Qt::PenStyle, whose dash
// lengths are expressed in units of pen width — so a dashed stroke inherits the
// logical→capture px scaling SnipOverlay::captureStyle() already applies to penWidth,
// with no extra DPI work.
//
// Tapered/TaperedHollow are **Line-tool only** (PaintBar filters the dropdown by group):
// they are not dash patterns at all but a shaft that widens from start to end, drawn as
// a filled (or stroked) silhouette rather than a pen stroke — which is also why they
// cannot be dashed. `penStyleFor` maps them to SolidLine so any code that does reach
// them with a pen degrades to a plain stroke instead of drawing nonsense.
enum class LineStyle { Solid, Dash, Dot, DashDot, DashDotDot, Tapered, TaperedHollow };
QString lineStyleToString(LineStyle l);
LineStyle lineStyleFromString(const QString& s);
inline bool isTapered(LineStyle l)
{
    return l == LineStyle::Tapered || l == LineStyle::TaperedHollow;
}

// The three looks of a NumberLabel badge (its 2nd-level toolbar's first three
// buttons, in order):
//   Filled       — disc filled with `color`, white digit.
//   RingedFilled — the same disc, plus a white ring around its outside.
//   Hollow       — ring and digit in `color`, interior transparent (the capture
//                  shows through).
enum class LabelStyle { Filled, RingedFilled, Hollow };
QString labelStyleToString(LabelStyle l);
LabelStyle labelStyleFromString(const QString& s);

// How a NumberLabel badge RENDERS its ordinal (its 2nd-level toolbar's numbering
// dropdown, in order): 1.2.3 · A.B.C · a.b.c · I.II.III · i.ii.iii. Badges always
// *store* a plain decimal ordinal — this only picks the glyphs it is drawn with, so
// the counter, the +/− reorder swap and digit typing are all style-independent.
enum class NumberStyle { Decimal, UpperAlpha, LowerAlpha, UpperRoman, LowerRoman };
QString numberStyleToString(NumberStyle n);
NumberStyle numberStyleFromString(const QString& s);

// Render `ordinal` in the given style: 1→A/a/I/i, 26→Z, 27→AA, 4→IV. Out of range
// (ordinal < 1, or a roman numeral past 3999) falls back to the decimal digits, so a
// badge can never come out blank.
QString formatNumber(int ordinal, NumberStyle style);

// Shared per-annotation style. `color` carries its own alpha (Marker lowers it
// for the highlighter look). penWidth/fontPx are in logical pixels so they scale
// with the painter's world transform at flatten time.
struct Style {
    QColor color = QColor(0xff, 0x3b, 0x30);   // red default
    int penWidth = 3;
    // Text tool. fontPx is a qreal so a corner-drag can scale it continuously (the
    // size box shows e.g. 16.13); the renderer rounds it for QFont::setPixelSize.
    qreal fontPx = 18;
    QString fontFamily = QStringLiteral("Consolas");
    bool bold = false;
    bool italic = false;
    bool hollowText = false;                   // coloured glyphs inside a white outline
    // Rect/Ellipse fill mode: false = hollow outline (penWidth stroke), true = solid
    // fill with `color` and no border. Ignored by every other tool.
    bool filled = false;
    // Line tool end decorations (see CapStyle) — independent per end. Ignored by every
    // other tool. `endCap` defaults to a head so a freshly drawn line looks like an arrow.
    CapStyle startCap = CapStyle::None;
    CapStyle endCap = CapStyle::ArrowSharp;
    // Line tool: draw the vertices as a smooth curve instead of straight segments. The
    // curve passes THROUGH every vertex (Catmull-Rom), so the clicks mean the same thing
    // either way. Combines with the caps — each rides its end's tangent.
    // Ignored by every other tool.
    bool curved = false;
    // Stroke dash pattern (see LineStyle). Honoured by hollow Rect/Ellipse and Pencil
    // only; every other tool strokes solid regardless.
    LineStyle lineStyle = LineStyle::Solid;
    // NumberLabel badge look (see LabelStyle). Ignored by every other tool. The badge's
    // SIZE rides on fontPx — it is the digit's font size, so the label needs no extra
    // field and inherits the mixed-DPI pre-scaling SnipOverlay::captureStyle() already
    // applies to fontPx.
    LabelStyle label = LabelStyle::Filled;
    // NumberLabel numbering glyphs (see NumberStyle). Ignored by every other tool. It is a
    // DISPLAY property: the badge stores its ordinal as decimal digits either way.
    NumberStyle numberStyle = NumberStyle::Decimal;
    // Redaction degree (mosaic block size / blur radius), in logical px before the
    // ctx.scale. Independent of penWidth, which for redaction-brush strokes is the
    // brush diameter. Used by Mosaic/Blur/MosaicBrush/BlurBrush only.
    int redactStrength = 6;
};

inline bool operator==(const Style& a, const Style& b)
{
    return a.color == b.color && a.penWidth == b.penWidth
        && qFuzzyCompare(a.fontPx + 1.0, b.fontPx + 1.0) && a.fontFamily == b.fontFamily
        && a.bold == b.bold && a.italic == b.italic && a.hollowText == b.hollowText
        && a.filled == b.filled && a.startCap == b.startCap && a.endCap == b.endCap
        && a.curved == b.curved && a.lineStyle == b.lineStyle
        && a.label == b.label && a.numberStyle == b.numberStyle
        && a.redactStrength == b.redactStrength;
}
inline bool operator!=(const Style& a, const Style& b) { return !(a == b); }

// How to map a global-logical point to a pixel in `base` (the image redaction
// tools sample from). The painter handed to paint() is pre-transformed so that
// drawing in global-logical coordinates lands correctly; redaction tools also
// need to *read* original pixels, which this describes:
//   basePx = (logical - originLogical) * scale
struct PaintCtx {
    const QImage* base = nullptr;   // device-pixel source (frozen screen / composite)
    QPointF originLogical;          // global-logical coord of base's pixel (0,0)
    qreal scale = 1.0;              // base pixels per logical unit
    // What the REDACTION tools (Mosaic/Blur + their brushes) sample. When null they
    // fall back to `base` (the clean capture). AnnotationDocument::paint points this
    // at an evolving composite (base + every annotation painted *beneath* the
    // redaction) so a mosaic/blur covers the annotations under it, not just the raw
    // pixels. Same pixel coordinates as `base`. Eraser tools deliberately ignore this
    // and always restore the clean `base`.
    const QImage* redactSource = nullptr;
};

// The redaction tools — the ones whose paint() *reads back* pixels to pixelate/blur
// (so they must sample the composite-below, not the clean base). Eraser is NOT one of
// these: it restores the clean original on purpose.
inline bool isRedactTool(ToolId t)
{
    return t == ToolId::Mosaic || t == ToolId::Blur
        || t == ToolId::MosaicBrush || t == ToolId::BlurBrush;
}

// The NumberLabel badge's outer diameter for a given style. Derived from
// style.fontPx (the digit's size) so the badge scales as one unit; multi-digit
// numbers keep this footprint and shrink their digits to fit instead.
qreal numberBadgeDiameter(const Style& style);

// Paint a NumberLabel badge centred at `center`, in the painter's current coordinate
// space. Exposed so the snip/pin cursor preview and the toolbar's variant icons render
// the exact same badge as the committed annotation (WYSIWYG). `number` is the badge's
// decimal ORDINAL — style.numberStyle picks the glyphs it is drawn with (formatNumber);
// anything that isn't an integer (e.g. the empty string mid-edit) is drawn verbatim.
void paintNumberBadge(QPainter& p, const Style& style, const QPointF& center,
                      const QString& number);

// Paint a Line's shaft + both end caps through `ipts`, in the painter's current
// coordinate space. Exposed for the same reason as paintNumberBadge: the toolbar's
// end-cap and line-style dropdown icons render themselves with the real renderer, so an
// item can never drift from what the tool actually draws (WYSIWYG, one paint path).
void paintLineArrow(QPainter& p, const QVector<QPoint>& ipts, const Style& s);

// The ink a badge's digits are drawn in (white on the filled looks, the style colour on
// the hollow one). Exposed so an editing caret can match its digits.
QColor numberBadgeInk(const Style& style);

// The text-caret rect for a badge being renumbered — just right of its digits, in the
// same coordinate space as paintNumberBadge. Exposed so the editing UI can blink a caret
// without duplicating the badge's font math (which shrinks digits to fit).
QRectF numberBadgeCaretRect(const Style& style, const QPointF& center,
                            const QString& number);

class Annotation {
public:
    virtual ~Annotation() = default;

    int id = 0;        // document-assigned, stable for the annotation's lifetime
    Style style;

    virtual ToolId tool() const = 0;

    // Paint in global-logical coordinates (the painter is pre-transformed).
    virtual void paint(QPainter& p, const PaintCtx& ctx) const = 0;

    // Hit-test a global-logical point (for selection / quick-mark / eraser-by-pick).
    virtual bool hitTest(const QPoint& globalLogical) const = 0;

    virtual QRect bounds() const = 0;          // global-logical bounding rect

    // Corner polygon of the annotation's oriented box, in global-logical coords —
    // used to draw a selection/hover outline. Default: the four axis-aligned bounds
    // corners; rotatable shapes (Text) override with their rotated box corners.
    virtual QVector<QPoint> outline() const
    {
        const QRect b = bounds();
        return { b.topLeft(), b.topRight(), b.bottomRight(), b.bottomLeft() };
    }

    // Shape-matched outline path in global-logical coords, for the hover/selection
    // highlight. Default: the polygon from outline(); Base2P returns its (rotated)
    // rect and Ellipse a (rotated) ellipse, so an ellipse gets an elliptical outline.
    virtual QPainterPath outlinePath() const;

    virtual void translate(const QPoint& d) = 0;

    // Generic geometry plumbing so the overlay's drawing state machine can build
    // any tool without knowing the concrete type. Interpretation is tool-specific:
    // two-point shapes use the first & last point; poly tools use the whole list;
    // Text uses the first point as its top-left anchor.
    virtual void setGeometry(const QVector<QPoint>& pts) = 0;
    virtual QVector<QPoint> geometry() const = 0;
    virtual bool isEmpty() const = 0;          // not yet a drawable shape

    virtual void setText(const QString&) {}    // only Text uses these
    virtual QString text() const { return {}; }

    // Text box geometry: the wrap box (logical px) the text is laid out + scaled in,
    // and a rotation (degrees, clockwise, about the box center). No-ops elsewhere.
    virtual void setBox(const QSize&) {}
    virtual QSize box() const { return {}; }
    virtual void setRotation(qreal) {}
    virtual qreal rotation() const { return 0.0; }

    // Horizontal mirror about the box centre, applied before the rotation. Only Text
    // uses it, and only the PIN editor sets it: annotations bake into a pin's source
    // pixels *before* its flips, so text on a singly-flipped pin would come out
    // mirrored — storing it pre-mirrored makes the pin's flip cancel back to readable.
    // (The snip overlay never sets this.) Mirroring a box about its own centre is
    // bounds-preserving, so this affects paint() only — not bounds/hitTest/outline.
    virtual void setMirrored(bool) {}
    virtual bool mirrored() const { return false; }

    // Serialization. Subclasses extend writeJson/readJson with their geometry.
    QJsonObject toJson() const;
    static std::unique_ptr<Annotation> fromJson(const QJsonObject& o);

protected:
    virtual void writeJson(QJsonObject& o) const = 0;
    virtual void readJson(const QJsonObject& o) = 0;
};

// Factory: a fresh, empty annotation of the given tool, pre-styled.
std::unique_ptr<Annotation> create(ToolId t, const Style& style);

} // namespace annotation
