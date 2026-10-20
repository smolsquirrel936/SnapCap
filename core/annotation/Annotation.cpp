#include "core/annotation/Annotation.h"
#include "core/render/Raster.h"

#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QFont>
#include <QFontMetrics>
#include <QStringList>
#include <QTransform>
#include <QJsonArray>
#include <QJsonObject>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace annotation {

QString toolIdToString(ToolId t)
{
    switch (t) {
        case ToolId::Rect:      return QStringLiteral("rect");
        case ToolId::Ellipse:   return QStringLiteral("ellipse");
        case ToolId::Line:      return QStringLiteral("line");
        case ToolId::Arrow:     return QStringLiteral("arrow");
        case ToolId::LineStrip: return QStringLiteral("linestrip");
        case ToolId::Pencil:    return QStringLiteral("pencil");
        case ToolId::Marker:    return QStringLiteral("marker");
        case ToolId::MarkerRect: return QStringLiteral("marker_rect");
        case ToolId::Mosaic:    return QStringLiteral("mosaic");
        case ToolId::Blur:      return QStringLiteral("blur");
        case ToolId::MosaicBrush: return QStringLiteral("mosaic_brush");
        case ToolId::BlurBrush:   return QStringLiteral("blur_brush");
        case ToolId::Text:      return QStringLiteral("text");
        case ToolId::Eraser:    return QStringLiteral("eraser");
        case ToolId::EraserRect: return QStringLiteral("eraser_rect");
        case ToolId::NumberLabel: return QStringLiteral("number_label");
        case ToolId::None:      break;
    }
    return QStringLiteral("none");
}

QString capStyleToString(CapStyle c)
{
    switch (c) {
        case CapStyle::ArrowSharp:     return QStringLiteral("arrow_sharp");
        case CapStyle::ArrowRound:     return QStringLiteral("arrow_round");
        case CapStyle::ArrowChevron:   return QStringLiteral("arrow_chevron");
        case CapStyle::ArrowHollow:    return QStringLiteral("arrow_hollow");
        case CapStyle::Dot:            return QStringLiteral("dot");
        case CapStyle::HollowDot:      return QStringLiteral("hollow_dot");
        case CapStyle::Diamond:        return QStringLiteral("diamond");
        case CapStyle::HollowDiamond:  return QStringLiteral("hollow_diamond");
        case CapStyle::Bar:            return QStringLiteral("bar");
        case CapStyle::None:           break;
    }
    return QStringLiteral("none");
}

CapStyle capStyleFromString(const QString& s)
{
    if (s == QLatin1String("arrow_sharp"))      return CapStyle::ArrowSharp;
    if (s == QLatin1String("arrow_round"))      return CapStyle::ArrowRound;
    if (s == QLatin1String("arrow_chevron"))    return CapStyle::ArrowChevron;
    // The chevron used to be a flat-backed filled head; keep reading the old name so a
    // saved session (or the remembered edit/line_*_cap setting) doesn't reset to None.
    if (s == QLatin1String("arrow_round_flat")) return CapStyle::ArrowChevron;
    if (s == QLatin1String("arrow_hollow"))     return CapStyle::ArrowHollow;
    if (s == QLatin1String("dot"))              return CapStyle::Dot;
    if (s == QLatin1String("hollow_dot"))       return CapStyle::HollowDot;
    if (s == QLatin1String("diamond"))          return CapStyle::Diamond;
    if (s == QLatin1String("hollow_diamond"))   return CapStyle::HollowDiamond;
    if (s == QLatin1String("bar"))              return CapStyle::Bar;
    return CapStyle::None;
}

QString lineStyleToString(LineStyle l)
{
    switch (l) {
        case LineStyle::Dash:          return QStringLiteral("dash");
        case LineStyle::Dot:           return QStringLiteral("dot");
        case LineStyle::DashDot:       return QStringLiteral("dash_dot");
        case LineStyle::DashDotDot:    return QStringLiteral("dash_dot_dot");
        case LineStyle::Tapered:       return QStringLiteral("tapered");
        case LineStyle::TaperedHollow: return QStringLiteral("tapered_hollow");
        case LineStyle::Solid:         break;
    }
    return QStringLiteral("solid");
}

LineStyle lineStyleFromString(const QString& s)
{
    if (s == QLatin1String("dash"))           return LineStyle::Dash;
    if (s == QLatin1String("dot"))            return LineStyle::Dot;
    if (s == QLatin1String("dash_dot"))       return LineStyle::DashDot;
    if (s == QLatin1String("dash_dot_dot"))   return LineStyle::DashDotDot;
    if (s == QLatin1String("tapered"))        return LineStyle::Tapered;
    if (s == QLatin1String("tapered_hollow")) return LineStyle::TaperedHollow;
    return LineStyle::Solid;
}

QString labelStyleToString(LabelStyle l)
{
    switch (l) {
        case LabelStyle::RingedFilled: return QStringLiteral("ringed_filled");
        case LabelStyle::Hollow:       return QStringLiteral("hollow");
        case LabelStyle::Filled:       break;
    }
    return QStringLiteral("filled");
}

LabelStyle labelStyleFromString(const QString& s)
{
    if (s == QLatin1String("ringed_filled")) return LabelStyle::RingedFilled;
    if (s == QLatin1String("hollow"))        return LabelStyle::Hollow;
    return LabelStyle::Filled;
}

QString numberStyleToString(NumberStyle n)
{
    switch (n) {
        case NumberStyle::UpperAlpha: return QStringLiteral("upper_alpha");
        case NumberStyle::LowerAlpha: return QStringLiteral("lower_alpha");
        case NumberStyle::UpperRoman: return QStringLiteral("upper_roman");
        case NumberStyle::LowerRoman: return QStringLiteral("lower_roman");
        case NumberStyle::Decimal:    break;
    }
    return QStringLiteral("decimal");
}

NumberStyle numberStyleFromString(const QString& s)
{
    if (s == QLatin1String("upper_alpha")) return NumberStyle::UpperAlpha;
    if (s == QLatin1String("lower_alpha")) return NumberStyle::LowerAlpha;
    if (s == QLatin1String("upper_roman")) return NumberStyle::UpperRoman;
    if (s == QLatin1String("lower_roman")) return NumberStyle::LowerRoman;
    return NumberStyle::Decimal;
}

namespace {

// Bijective base-26: 1→A, 26→Z, 27→AA, 52→AZ, 53→BA. (Not plain base-26 — there is no
// "zero" letter, so each digit runs 1..26 and the remainder is taken before the divide.)
QString alphaNumeral(int n, char first)
{
    QString out;
    while (n > 0) {
        const int rem = (n - 1) % 26;
        out.prepend(QChar(first + rem));
        n = (n - 1) / 26;
    }
    return out;
}

// Standard subtractive roman numerals, valid 1..3999 (the caller range-checks).
QString romanNumeral(int n)
{
    static const struct { int v; const char* s; } kVals[] = {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"},
        {50, "L"}, {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}
    };
    QString out;
    for (const auto& e : kVals)
        while (n >= e.v) { out += QLatin1String(e.s); n -= e.v; }
    return out;
}

} // namespace

QString formatNumber(int ordinal, NumberStyle style)
{
    if (ordinal >= 1) {
        switch (style) {
            case NumberStyle::UpperAlpha: return alphaNumeral(ordinal, 'A');
            case NumberStyle::LowerAlpha: return alphaNumeral(ordinal, 'a');
            case NumberStyle::UpperRoman:
                if (ordinal <= 3999) return romanNumeral(ordinal);
                break;
            case NumberStyle::LowerRoman:
                if (ordinal <= 3999) return romanNumeral(ordinal).toLower();
                break;
            case NumberStyle::Decimal:
                break;
        }
    }
    // Decimal, ordinal < 1 (no roman/alpha zero), or a roman numeral past 3999.
    return QString::number(ordinal);
}

ToolId toolIdFromString(const QString& s)
{
    if (s == QLatin1String("rect"))      return ToolId::Rect;
    if (s == QLatin1String("ellipse"))   return ToolId::Ellipse;
    if (s == QLatin1String("line"))      return ToolId::Line;
    if (s == QLatin1String("arrow"))     return ToolId::Arrow;
    if (s == QLatin1String("linestrip")) return ToolId::LineStrip;
    if (s == QLatin1String("pencil"))    return ToolId::Pencil;
    if (s == QLatin1String("marker"))    return ToolId::Marker;
    if (s == QLatin1String("marker_rect")) return ToolId::MarkerRect;
    if (s == QLatin1String("mosaic"))    return ToolId::Mosaic;
    if (s == QLatin1String("blur"))      return ToolId::Blur;
    if (s == QLatin1String("mosaic_brush")) return ToolId::MosaicBrush;
    if (s == QLatin1String("blur_brush"))   return ToolId::BlurBrush;
    if (s == QLatin1String("text"))      return ToolId::Text;
    if (s == QLatin1String("eraser"))    return ToolId::Eraser;
    if (s == QLatin1String("eraser_rect")) return ToolId::EraserRect;
    if (s == QLatin1String("number_label")) return ToolId::NumberLabel;
    return ToolId::None;
}

namespace {

Qt::PenStyle penStyleFor(LineStyle l)
{
    switch (l) {
        case LineStyle::Dash:       return Qt::DashLine;
        case LineStyle::Dot:        return Qt::DotLine;
        case LineStyle::DashDot:    return Qt::DashDotLine;
        case LineStyle::DashDotDot: return Qt::DashDotDotLine;
        // Not dash patterns: the tapered shaft is a filled silhouette, not a stroke.
        // Any caller that reaches them with a pen degrades to a plain solid stroke.
        case LineStyle::Tapered:
        case LineStyle::TaperedHollow:
        case LineStyle::Solid:      break;
    }
    return Qt::SolidLine;
}

// `dash` is opt-in per call site, NOT read from the style unconditionally: this pen is
// shared by tools whose toolbar has no line-style dropdown (Arrow/LineStrip/Marker/…),
// and the hosts carry ONE Style across tool switches — so a dash picked for a rectangle
// would otherwise leak onto the next line. Only Rect/Ellipse/Pencil opt in.
QPen strokePen(const Style& s, Qt::PenCapStyle cap = Qt::RoundCap, bool dash = false)
{
    const Qt::PenStyle ps = dash ? penStyleFor(s.lineStyle) : Qt::SolidLine;
    // A dashed stroke uses flat caps so the pattern renders at its nominal proportions:
    // square/round caps extend every dash by half the pen width at each end, which at
    // Dot's 1-on-2-off spacing swallows most of the gaps.
    if (ps != Qt::SolidLine)
        cap = Qt::FlatCap;
    QPen pen(s.color, qMax(1, s.penWidth), ps, cap, Qt::RoundJoin);
    return pen;
}

// Map a global-logical rect to the corresponding (inclusive) pixel rect in the
// PaintCtx base image, rounding outward so no edge is clipped.
QRect baseRect(const QRect& logical, const PaintCtx& ctx)
{
    const QRectF n = QRectF(logical.normalized());
    const QPointF tl = (n.topLeft()     - ctx.originLogical) * ctx.scale;
    const QPointF br = (n.bottomRight()  - ctx.originLogical) * ctx.scale;
    return QRect(QPoint(qFloor(tl.x()), qFloor(tl.y())),
                 QPoint(qCeil(br.x()),  qCeil(br.y())));
}

// A shape whose footprint hangs off the base image samples a *clipped* source rect, so
// the sampled image is smaller than the footprint it is drawn into. Painting it over the
// whole footprint stretches it along whichever axis got clipped — and not only into the
// off-image part: the stretch drags wrong pixels across the visible part too. This walks
// each clipped edge back to where the base image actually ends, inverting the basePx =
// (logical - origin) * scale mapping. Edges that were NOT clipped keep their exact
// original coordinate, so the (usual) fully-inside case is bit-identical to before —
// which matters because baseRect() rounds outward, making `srcWant` up to a pixel larger
// than `dstLogical` maps to, so rescaling proportionally would shift every edge slightly.
QRectF clippedDest(const QRect& dstLogical, const QRect& srcWant, const QRect& srcClipped,
                   const PaintCtx& ctx)
{
    QRectF d(dstLogical);
    if (srcClipped == srcWant || ctx.scale <= 0.0)
        return d;
    // The sampled image covers base pixels [left, right+1) x [top, bottom+1).
    if (srcClipped.left() != srcWant.left())
        d.setLeft(srcClipped.left() / ctx.scale + ctx.originLogical.x());
    if (srcClipped.top() != srcWant.top())
        d.setTop(srcClipped.top() / ctx.scale + ctx.originLogical.y());
    if (srcClipped.right() != srcWant.right())
        d.setRight((srcClipped.right() + 1) / ctx.scale + ctx.originLogical.x());
    if (srcClipped.bottom() != srcWant.bottom())
        d.setBottom((srcClipped.bottom() + 1) / ctx.scale + ctx.originLogical.y());
    return d;
}

double distToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= 0.0)
        return std::hypot(p.x() - a.x(), p.y() - a.y());
    double t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2;
    t = qBound(0.0, t, 1.0);
    const QPointF proj(a.x() + t * ab.x(), a.y() + t * ab.y());
    return std::hypot(p.x() - proj.x(), p.y() - proj.y());
}

// ---- two-point shapes -----------------------------------------------------
class Base2P : public Annotation {
public:
    ToolId tool() const override { return m_tool; }
    void setGeometry(const QVector<QPoint>& pts) override
    {
        if (pts.isEmpty()) return;
        m_p1 = pts.first();
        m_p2 = pts.last();
    }
    QVector<QPoint> geometry() const override { return {m_p1, m_p2}; }
    bool isEmpty() const override { return m_p1 == m_p2; }
    void setRotation(qreal deg) override { m_rot = deg; }
    qreal rotation() const override { return m_rot; }
    QRect bounds() const override
    {
        const QRect r = contentRect();
        if (qFuzzyIsNull(m_rot)) return r;
        return rotationTransform(QRectF(r)).mapRect(QRectF(r)).toAlignedRect();
    }
    void translate(const QPoint& d) override { m_p1 += d; m_p2 += d; }
    bool hitTest(const QPoint& g) const override
    {
        QPointF local(g);
        if (!qFuzzyIsNull(m_rot))
            local = rotationTransform(QRectF(contentRect())).inverted().map(QPointF(g));
        return hitTestLocal(local);
    }
    // The (rotated) content-rect corners, so a selection highlight follows the box.
    QVector<QPoint> outline() const override
    {
        const QRectF r(contentRect());
        const QPointF c[4] = { r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft() };
        const QTransform t = qFuzzyIsNull(m_rot) ? QTransform() : rotationTransform(r);
        QVector<QPoint> out;
        out.reserve(4);
        for (const QPointF& pt : c) out << t.map(pt).toPoint();
        return out;
    }

protected:
    explicit Base2P(ToolId t) : m_tool(t) {}

    // The un-rotated content rect (the two-point box); rotation pivots on its centre.
    QRect contentRect() const { return QRect(m_p1, m_p2).normalized(); }

    // Rotation about the content-rect centre, in global-logical space.
    QTransform rotationTransform(const QRectF& r) const
    {
        const QPointF c = r.center();
        QTransform t;
        t.translate(c.x(), c.y());
        t.rotate(m_rot);
        t.translate(-c.x(), -c.y());
        return t;
    }

    // Hit-test in the shape's un-rotated local frame (hitTest() removes the rotation
    // first). Default: the inflated content rect; Ellipse/Arrow refine it.
    virtual bool hitTestLocal(const QPointF& local) const
    {
        return QRectF(contentRect()).adjusted(-6, -6, 6, 6).contains(local);
    }

    void writeJson(QJsonObject& o) const override
    {
        o[QStringLiteral("x1")] = m_p1.x(); o[QStringLiteral("y1")] = m_p1.y();
        o[QStringLiteral("x2")] = m_p2.x(); o[QStringLiteral("y2")] = m_p2.y();
        if (!qFuzzyIsNull(m_rot))
            o[QStringLiteral("rot")] = m_rot;
    }
    void readJson(const QJsonObject& o) override
    {
        m_p1 = QPoint(o.value(QStringLiteral("x1")).toInt(), o.value(QStringLiteral("y1")).toInt());
        m_p2 = QPoint(o.value(QStringLiteral("x2")).toInt(), o.value(QStringLiteral("y2")).toInt());
        m_rot = o.value(QStringLiteral("rot")).toDouble();
    }

    ToolId m_tool;
    QPoint m_p1, m_p2;
    qreal m_rot = 0.0;   // degrees clockwise about the content-rect centre
};

class RectShape : public Base2P {
public:
    RectShape() : Base2P(ToolId::Rect) {}
    void paint(QPainter& p, const PaintCtx&) const override
    {
        if (style.filled) {
            p.setPen(Qt::NoPen);
            p.setBrush(style.color);
        } else {
            p.setPen(strokePen(style, Qt::SquareCap, /*dash*/ true));
            p.setBrush(Qt::NoBrush);
        }
        p.save();
        if (!qFuzzyIsNull(rotation())) {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setTransform(rotationTransform(QRectF(contentRect())), true);
        }
        p.drawRect(contentRect());
        p.restore();
    }
};

class EllipseShape : public Base2P {
public:
    EllipseShape() : Base2P(ToolId::Ellipse) {}
    void paint(QPainter& p, const PaintCtx&) const override
    {
        if (style.filled) {
            p.setPen(Qt::NoPen);
            p.setBrush(style.color);
        } else {
            p.setPen(strokePen(style, Qt::RoundCap, /*dash*/ true));
            p.setBrush(Qt::NoBrush);
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        p.save();
        if (!qFuzzyIsNull(rotation()))
            p.setTransform(rotationTransform(QRectF(contentRect())), true);
        p.drawEllipse(contentRect());
        p.restore();
    }
    QPainterPath outlinePath() const override
    {
        const QRectF r(contentRect());
        QPainterPath path;
        path.addEllipse(r);
        return qFuzzyIsNull(rotation()) ? path : rotationTransform(r).map(path);
    }

protected:
    bool hitTestLocal(const QPointF& local) const override
    {
        const QRectF b(contentRect());
        if (b.width() <= 0 || b.height() <= 0) return false;
        const QPointF c = b.center();
        const double nx = (local.x() - c.x()) / (b.width() / 2.0);
        const double ny = (local.y() - c.y()) / (b.height() / 2.0);
        return nx * nx + ny * ny <= 1.2;   // a little slack
    }
};

class ArrowShape : public Base2P {
public:
    ArrowShape() : Base2P(ToolId::Arrow) {}
    void paint(QPainter& p, const PaintCtx&) const override
    {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(strokePen(style));
        p.drawLine(m_p1, m_p2);
        // Arrowhead at p2, sized from the pen width.
        const double ang = std::atan2(m_p2.y() - m_p1.y(), m_p2.x() - m_p1.x());
        const double len = qMax(12.0, style.penWidth * 4.0);
        const double spread = M_PI / 7.0;
        const QPointF tip(m_p2);
        const QPointF a(tip.x() - len * std::cos(ang - spread),
                        tip.y() - len * std::sin(ang - spread));
        const QPointF b(tip.x() - len * std::cos(ang + spread),
                        tip.y() - len * std::sin(ang + spread));
        QPainterPath head;
        head.moveTo(tip); head.lineTo(a); head.lineTo(b); head.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(style.color);
        p.drawPath(head);
    }
    bool isEmpty() const override { return m_p1 == m_p2; }
    bool hitTest(const QPoint& g) const override
    {
        return distToSegment(g, m_p1, m_p2) <= qMax(6, style.penWidth);
    }
};

class MosaicShape : public Base2P {
public:
    MosaicShape() : Base2P(ToolId::Mosaic) {}
    void paint(QPainter& p, const PaintCtx& ctx) const override
    {
        const QImage* srcImg = ctx.redactSource ? ctx.redactSource : ctx.base;
        if (contentRect().isEmpty() || !srcImg) return;
        const bool rotated = !qFuzzyIsNull(rotation());
        // Sample over the (rotated) footprint's axis-aligned bounding box, then clip
        // the pixelated result to the oriented shape so only the true covered pixels show.
        const QRect aabb = rotated ? bounds() : contentRect();
        const QRect want = baseRect(aabb, ctx);
        const QRect src = want.intersected(srcImg->rect());
        if (src.isEmpty()) return;
        const int block = qMax(6, int(std::lround(qMax(2, style.redactStrength * 3) * ctx.scale)));
        const QImage out = render::mosaic(srcImg->copy(src), block);
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        if (rotated) p.setClipPath(outlinePath());
        p.drawImage(clippedDest(aabb, want, src, ctx), out);
        p.restore();
    }
};

class BlurShape : public Base2P {
public:
    BlurShape() : Base2P(ToolId::Blur) {}
    void paint(QPainter& p, const PaintCtx& ctx) const override
    {
        const QImage* srcImg = ctx.redactSource ? ctx.redactSource : ctx.base;
        if (contentRect().isEmpty() || !srcImg) return;
        const bool rotated = !qFuzzyIsNull(rotation());
        const QRect aabb = rotated ? bounds() : contentRect();
        const QRect want = baseRect(aabb, ctx);
        const QRect src = want.intersected(srcImg->rect());
        if (src.isEmpty()) return;
        const int radius = qMax(2, int(std::lround(qMax(2, style.redactStrength * 2) * ctx.scale)));
        const QImage out = render::blur(srcImg->copy(src), radius);
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (rotated) p.setClipPath(outlinePath());
        p.drawImage(clippedDest(aabb, want, src, ctx), out);
        p.restore();
    }
};

// ---- poly shapes ----------------------------------------------------------
class BasePoly : public Annotation {
public:
    ToolId tool() const override { return m_tool; }
    void setGeometry(const QVector<QPoint>& pts) override { m_pts = pts; }
    QVector<QPoint> geometry() const override { return m_pts; }
    bool isEmpty() const override { return m_pts.size() < 2; }
    QRect bounds() const override
    {
        if (m_pts.isEmpty()) return {};
        QRect r(m_pts.first(), QSize(0, 0));
        for (const QPoint& pt : m_pts) r = r.united(QRect(pt, QSize(1, 1)));
        return r;
    }
    void translate(const QPoint& d) override
    {
        for (QPoint& pt : m_pts) pt += d;
    }
    bool hitTest(const QPoint& g) const override
    {
        for (int i = 1; i < m_pts.size(); ++i)
            if (distToSegment(g, m_pts[i - 1], m_pts[i]) <= qMax(4, style.penWidth))
                return true;
        return false;
    }

protected:
    explicit BasePoly(ToolId t) : m_tool(t) {}
    void drawPolyline(QPainter& p) const
    {
        if (m_pts.size() < 2) return;
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawPolyline(m_pts.constData(), m_pts.size());
    }
    void writeJson(QJsonObject& o) const override
    {
        QJsonArray arr;
        for (const QPoint& pt : m_pts) { arr.append(pt.x()); arr.append(pt.y()); }
        o[QStringLiteral("pts")] = arr;
    }
    void readJson(const QJsonObject& o) override
    {
        m_pts.clear();
        const QJsonArray arr = o.value(QStringLiteral("pts")).toArray();
        for (int i = 0; i + 1 < arr.size(); i += 2)
            m_pts.append(QPoint(arr[i].toInt(), arr[i + 1].toInt()));
    }

    ToolId m_tool;
    QVector<QPoint> m_pts;
};

// The Line tool's vertices as the polyline everything downstream works on. When
// `curved`, the vertices are densified into a Catmull-Rom spline that passes through
// every one of them; the flattening is fine enough that stroking, the tapered outline
// and hit-testing all read as a smooth curve without any of them knowing about curves.
QVector<QPointF> linePath(const QVector<QPoint>& ipts, bool curved)
{
    QVector<QPointF> pts;
    pts.reserve(ipts.size());
    for (const QPoint& q : ipts) pts.append(QPointF(q));
    const int n = pts.size();
    if (!curved || n < 3)
        return pts;                          // two points already ARE the whole curve

    QVector<QPointF> out;
    out.append(pts.first());
    for (int i = 0; i + 1 < n; ++i) {
        // Catmull-Rom → cubic Bezier: the tangent at a vertex is 1/6 of the chord
        // between its neighbours. The two end vertices stand in as their own neighbour,
        // which is what makes the curve start/stop cleanly instead of flying off.
        const QPointF p0 = pts[qMax(0, i - 1)];
        const QPointF p1 = pts[i];
        const QPointF p2 = pts[i + 1];
        const QPointF p3 = pts[qMin(n - 1, i + 2)];
        const QPointF c1 = p1 + (p2 - p0) / 6.0;
        const QPointF c2 = p2 - (p3 - p1) / 6.0;
        // ~1 sample per 3 px of chord: past the point where the flattening shows, but
        // bounded so a screen-wide segment doesn't pile up thousands of points.
        const double chord = std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
        const int steps = qBound(8, int(chord / 3.0), 64);
        for (int s = 1; s <= steps; ++s) {
            const double t = double(s) / steps, u = 1.0 - t;
            out.append(u * u * u * p1 + 3 * u * u * t * c1 + 3 * u * t * t * c2 + t * t * t * p2);
        }
    }
    return out;
}

// Build a filled outline around a polyline whose half-width grows linearly from
// `wTail` (first vertex) to `wHead` (last vertex) — the tapered arrow shaft. Each
// vertex is offset along the averaged normal of its adjacent segments.
QPainterPath taperedOutline(const QVector<QPointF>& pts, double wTail, double wHead)
{
    const int n = pts.size();
    if (n < 2) return {};
    QVector<double> cum(n, 0.0);
    for (int i = 1; i < n; ++i)
        cum[i] = cum[i - 1] + std::hypot(pts[i].x() - pts[i - 1].x(), pts[i].y() - pts[i - 1].y());
    const double total = cum[n - 1];
    if (total <= 0.0) return {};

    auto segNormal = [&](int i, int j) -> QPointF {
        const QPointF d = pts[j] - pts[i];
        const double L = std::hypot(d.x(), d.y());
        return L <= 0.0 ? QPointF(0, 0) : QPointF(-d.y() / L, d.x() / L);
    };
    QVector<QPointF> nrm(n);
    for (int i = 0; i < n; ++i) {
        const QPointF na = i > 0     ? segNormal(i - 1, i) : QPointF();
        const QPointF nb = i < n - 1 ? segNormal(i, i + 1) : QPointF();
        QPointF s = na + nb;
        double L = std::hypot(s.x(), s.y());
        if (L <= 0.0) { s = (i > 0 ? na : nb); L = 1.0; }
        nrm[i] = s / L;
    }
    auto half = [&](int i) { return (wTail + (wHead - wTail) * (cum[i] / total)) * 0.5; };

    QPainterPath path;
    path.moveTo(pts[0] + nrm[0] * half(0));
    for (int i = 1; i < n; ++i) path.lineTo(pts[i] + nrm[i] * half(i));
    for (int i = n - 1; i >= 0; --i) path.lineTo(pts[i] - nrm[i] * half(i));
    path.closeSubpath();
    // The shaft is ONE closed loop, so wherever the line crosses itself the loop crosses
    // too. Under Qt's default odd-even rule those overlaps count as outside and get
    // punched out as holes; winding keeps them filled.
    path.setFillRule(Qt::WindingFill);
    return path;
}

// A concave (swept-back-barb) arrowhead at `tip`, oriented along `dir` (unit) with
// perpendicular `nrm`. The back edge notches *forward* toward the tip, so the base is
// concave rather than a flat triangle.
QPainterPath concaveArrowHead(const QPointF& tip, const QPointF& dir, const QPointF& nrm,
                              double headLen, double headHalf, double notchDepth)
{
    const QPointF neck = tip - dir * headLen;            // back of the head (barb line)
    QPainterPath head;
    head.moveTo(tip);
    head.lineTo(neck + nrm * headHalf);                  // top barb
    head.lineTo(tip - dir * (headLen - notchDepth));     // back-notch, pulled toward tip
    head.lineTo(neck - nrm * headHalf);                  // bottom barb
    head.closeSubpath();
    return head;
}

// One end decoration, resolved to something drawable.
struct CapGeom {
    QPainterPath path;      // already positioned + oriented in scene coords
    double inset = 0.0;     // how far short of the end point the shaft must stop
    // The trim to use when this cap is MERGED into the shaft's silhouette (see merges()).
    // A merge needs a real OVERLAP, not a shared edge: two paths that merely touch keep
    // that edge in the union, and stroking it then draws a line straight across the
    // shaft. Negative means "same as inset".
    double mergeInset = -1.0;
    bool fill  = true;      // fill it, or stroke its outline (the hollow variants)
    // This cap has a flat back edge running ACROSS the shaft (only the hollow arrow
    // does; a dot or diamond meets the shaft at a point). Stroked as-is it reads as a bar
    // through the junction, so such a cap is drawn *under* the shaft, which then covers
    // that edge — see the draw order in paintLineArrow.
    bool coverBack = false;
    bool round = false;     // soften the corners (the "round" arrows)
    // Eligible to merge into a tapered silhouette so the two share one outline. Whether
    // it ACTUALLY merges also depends on the shaft: see `merges()` in paintLineArrow —
    // a cap only merges into a shaft drawn the same way it is.
    bool unite = false;
};

// Build the cap for one end. `end` is the endpoint, `dir` the OUTWARD unit direction
// there (pointing away from the line), so start and end caps use the same code with
// opposite `dir`. Every size rides on the pen width `w`, which SnipOverlay::captureStyle
// has already scaled into capture px.
//
// `scale` enlarges the SHAPE — never the outline weight, which always follows the pen
// width so a hollow cap matches the line you drew. It exists for one case: the
// tapered-hollow shaft is a far wider ribbon than the pen, and a pen-sized head next to
// it is swallowed, leaving a long wedge with a nub on the end. Caps there are therefore
// *deliberately* bigger than on every other line style — chosen by eye, not derived.
// Locked in by tst_annotation_document::capSizeMatchesExceptTaperedHollow.
//
// ===> tune the end-cap shapes here <===
CapGeom capGeometry(CapStyle c, const QPointF& end, const QPointF& dir, double penW,
                    double scale)
{
    CapGeom g;
    const QPointF nrm(-dir.y(), dir.x());
    const double w = penW * scale;      // drives every SHAPE dimension below
    switch (c) {
        case CapStyle::None:
            break;
        case CapStyle::ArrowSharp: {                 // slender, sharp barbs, notched back
            const double len = qMax(16.0, w * 4.8), half = qMax(7.0, w * 2.3);
            const double notch = len * 0.35;
            g.path  = concaveArrowHead(end, dir, nrm, len, half, notch);
            g.unite = true;
            // Stop the shaft just *forward* of the notch apex so it fills the notch.
            g.inset = (len - notch) - qMax(2.0, w * 0.6);
            break;
        }
        case CapStyle::ArrowRound: {                 // deeper fork, rounded barbs
            const double len = qMax(14.0, w * 4.2), half = qMax(7.0, w * 2.2);
            const double notch = len * 0.5;
            g.path  = concaveArrowHead(end, dir, nrm, len, half, notch);
            g.unite = true;
            g.round = true;
            g.inset = (len - notch) - qMax(2.0, w * 0.6);
            break;
        }
        case CapStyle::ArrowChevron: {               // an open ">" of two strokes
            // Not a closed shape at all: two arms meeting at the tip, stroked at the pen
            // width so the head reads as the same line continuing.
            // ===> tune the chevron angle here <===
            const double wingsDeg = 80.0;            // angle BETWEEN the two arms
            const double half = qMax(7.0, w * 2.2);
            const double len  = half / std::tan(qDegreesToRadians(wingsDeg * 0.5));
            g.path.moveTo(end - dir * len + nrm * half);
            g.path.lineTo(end);
            g.path.lineTo(end - dir * len - nrm * half);
            g.fill  = false;                         // stroked, never filled
            // The shaft runs almost to the tip: it is what fills the middle of the "V",
            // exactly as in the reference, and its round cap lands on the point.
            g.inset = w * 0.5;
            break;
        }
        case CapStyle::ArrowHollow: {                // outline triangle
            const double len = qMax(16.0, w * 4.4), half = qMax(7.0, w * 2.2);
            g.path = concaveArrowHead(end, dir, nrm, len, half, 0.0);
            g.fill  = false;
            g.unite = true;      // ...into a TaperedHollow shaft: both are outlines
            g.coverBack = true;  // ...otherwise the shaft hides its back edge
            // Unmerged, the shaft is drawn ON TOP and must reach past the far face of the
            // back edge's stroke (which straddles it by half the pen width) to cover it.
            g.inset = len - penW * 0.5;
            g.mergeInset = len * 0.55;               // merged: run well inside it
            break;
        }
        case CapStyle::Dot:
        case CapStyle::HollowDot: {
            const double r = qMax(4.0, w * 1.7);
            g.path.addEllipse(end, r, r);            // centred ON the endpoint
            g.fill  = (c == CapStyle::Dot);
            g.inset = r;
            break;
        }
        case CapStyle::Diamond:
        case CapStyle::HollowDiamond: {              // far tip sits on the endpoint
            const double len = qMax(12.0, w * 4.0), half = qMax(5.0, w * 1.7);
            g.path.moveTo(end);
            g.path.lineTo(end - dir * (len * 0.5) + nrm * half);
            g.path.lineTo(end - dir * len);
            g.path.lineTo(end - dir * (len * 0.5) - nrm * half);
            g.path.closeSubpath();
            g.fill  = (c == CapStyle::Diamond);
            g.inset = len;
            break;
        }
        case CapStyle::Bar: {                        // perpendicular tee
            const double half = qMax(5.0, w * 2.0);
            g.path.moveTo(end + nrm * half);
            g.path.lineTo(end - nrm * half);
            g.fill  = false;
            break;                                   // inset 0: the shaft meets its centre
        }
    }
    if (g.mergeInset < 0.0)
        g.mergeInset = g.inset;
    return g;
}

void drawCap(QPainter& p, const CapGeom& g, const QColor& color, double w)
{
    if (g.path.isEmpty())
        return;
    if (g.fill) {
        // Rounding is done by ALSO stroking the path with a round-join pen — cheaper and
        // more robust than rebuilding the outline with arcs, and it grows the head by
        // only half the pen width.
        p.setPen(g.round ? QPen(color, qMax(1.5, w * 0.55), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
                         : QPen(Qt::NoPen));
        p.setBrush(color);
        p.drawPath(g.path);
        p.setBrush(Qt::NoBrush);
    } else {
        // Hollow caps (and the bar) are outlined at the tool's OWN pen width, so their
        // weight always matches the line the user drew. A fixed fraction of it looked
        // thin next to a plain shaft and badly mismatched next to a tapered one, which
        // reaches 1.3x by its end.
        p.strokePath(g.path, QPen(color, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
}

// The outward unit direction at one end of the polyline.
QPointF outwardDir(const QVector<QPointF>& pts, bool atStart)
{
    const QPointF a = atStart ? pts[1] : pts[pts.size() - 2];
    const QPointF b = atStart ? pts.first() : pts.back();
    const QPointF d = b - a;
    const double L = std::hypot(d.x(), d.y());
    return L > 0.0 ? d / L : QPointF(1, 0);
}

// Pull the LAST point back along the polyline's own arc length by `cut`, dropping any
// vertex that falls inside. On a straight line with one long final segment this is just
// `tip - dir * cut`; the walk matters for curves, whose flattened points are only ~3 px
// apart, and without it the trimmed segment would double back and knot the outline.
void trimTail(QVector<QPointF>& pts, double cut)
{
    if (cut <= 0.0 || pts.size() < 2)
        return;
    const QPointF tip = pts.back();
    auto distToTip = [&](const QPointF& q) { return std::hypot(q.x() - tip.x(), q.y() - tip.y()); };
    while (pts.size() > 2 && distToTip(pts[pts.size() - 2]) < cut)
        pts.removeLast();
    const QPointF back = pts[pts.size() - 2] - tip;
    const double L = std::hypot(back.x(), back.y());
    pts.back() = L > 0.0 ? tip + back / L * qMin(cut, L) : tip;
}

// (paintLineArrow itself is declared in the header and so must live OUTSIDE this
// anonymous namespace — it is defined further down, beside paintNumberBadge.)

// The Line tool: a multi-segment polyline (or, under style.curved, a smooth curve
// through the same vertices) that honours style.lineStyle for the shaft and
// style.startCap / style.endCap for its two ends. Built vertex-by-vertex by the
// overlay's line state machine, then adjustable via control points before it is
// finalized.
class LineArrowShape : public BasePoly {
public:
    LineArrowShape() : BasePoly(ToolId::Line) {}
    void paint(QPainter& p, const PaintCtx&) const override
    {
        p.setRenderHint(QPainter::Antialiasing, true);
        paintLineArrow(p, m_pts, style);
    }
    bool hitTest(const QPoint& g) const override
    {
        // Test what is actually drawn, so a curve is grabbable where it bulges away
        // from the straight chord between its vertices.
        const QVector<QPointF> pts = linePath(m_pts, style.curved);
        for (int i = 1; i < pts.size(); ++i)
            if (distToSegment(g, pts[i - 1], pts[i]) <= qMax(6, style.penWidth))
                return true;
        return false;
    }
};

class LineStripShape : public BasePoly {
public:
    LineStripShape() : BasePoly(ToolId::LineStrip) {}
    void paint(QPainter& p, const PaintCtx&) const override
    {
        p.setPen(strokePen(style, Qt::SquareCap));
        drawPolyline(p);
    }
};

class PencilShape : public BasePoly {
public:
    PencilShape() : BasePoly(ToolId::Pencil) {}
    void paint(QPainter& p, const PaintCtx&) const override
    {
        p.setPen(strokePen(style, Qt::RoundCap, /*dash*/ true));
        drawPolyline(p);
    }
};

// The highlighter look, shared by the freehand and rectangle marker variants so the
// two can't drift. Force translucency on an opaque colour, and paint with a Multiply
// blend so the stroke *darkens toward* the content beneath it (yellow over black text
// stays black) the way a real highlighter does, rather than merely tinting it. The
// caller is responsible for save()/restore() around the returned composition change.
QColor markerColor(const QColor& c)
{
    QColor m = c;
    if (m.alpha() == 255) m.setAlpha(110);              // highlighter translucency
    return m;
}

// Drop hand jitter from a freehand stroke: keep a point only when it is at least
// `minDist` from the last kept one (the first and last points are always kept). A
// wobble smaller than the stroke's radius doesn't change the filled area, but it DOES
// create sharp hairpins that a thick pen renders as artifacts at the join (round discs
// with RoundJoin, flat zigzag steps with BevelJoin) — typically a cluster of these at
// the start/end where the hand presses down and lifts. Pruning them yields clean ends.
QVector<QPoint> simplifyStroke(const QVector<QPoint>& pts, double minDist)
{
    if (pts.size() < 3 || minDist <= 0.0)
        return pts;
    QVector<QPoint> out;
    out.reserve(pts.size());
    out.append(pts.first());
    const double min2 = minDist * minDist;
    for (int i = 1; i < pts.size() - 1; ++i) {
        const QPointF d = pts[i] - out.last();
        if (d.x() * d.x() + d.y() * d.y() >= min2)
            out.append(pts[i]);
    }
    out.append(pts.last());
    return out;
}

class MarkerShape : public BasePoly {
public:
    MarkerShape() : BasePoly(ToolId::Marker) {}
    void paint(QPainter& p, const PaintCtx&) const override
    {
        if (m_pts.size() < 2) return;
        const int w = qMax(6, style.penWidth * 4);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
        // Flat chisel ends + BevelJoin. Prune sub-radius jitter first so neither the
        // join nor the cap has a hairpin to render as a disc or zigzag at the ends.
        QPen pen(markerColor(style.color), w, Qt::SolidLine, Qt::FlatCap, Qt::BevelJoin);
        p.setPen(pen);
        const QVector<QPoint> pts = simplifyStroke(m_pts, w * 0.5);
        p.drawPolyline(pts.constData(), pts.size());
        p.restore();
    }
};

// Rectangle highlighter: a translucent solid-filled rectangle (the rect-drag
// counterpart of the freehand MarkerShape). Shares markerColor + the Multiply blend.
class MarkerRectShape : public Base2P {
public:
    MarkerRectShape() : Base2P(ToolId::MarkerRect) {}
    void paint(QPainter& p, const PaintCtx&) const override
    {
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(markerColor(style.color));
        if (!qFuzzyIsNull(rotation()))
            p.setTransform(rotationTransform(QRectF(contentRect())), true);
        p.drawRect(contentRect());
        p.restore();
    }
};

class EraserShape : public BasePoly {
public:
    EraserShape() : BasePoly(ToolId::Eraser) {}
    void paint(QPainter& p, const PaintCtx& ctx) const override
    {
        if (m_pts.size() < 2 || !ctx.base) return;
        QPainterPath path(m_pts.first());
        for (int i = 1; i < m_pts.size(); ++i) path.lineTo(m_pts[i]);
        QPainterPathStroker stroker;
        stroker.setWidth(qMax(4, style.penWidth));       // penWidth = brush diameter
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        const QPainterPath stroked = stroker.createStroke(path);
        const QRect b = stroked.boundingRect().toRect();
        const QRect want = baseRect(b, ctx);
        const QRect src = want.intersected(ctx.base->rect());
        if (src.isEmpty()) return;
        p.save();
        p.setClipPath(stroked);                          // restore original pixels
        p.drawImage(clippedDest(b, want, src, ctx), *ctx.base, QRectF(src));
        p.restore();
    }
};

// Rectangle eraser: restore the original base pixels over a dragged rectangle
// region (the rect-region counterpart of EraserShape's freehand circle brush).
class EraserRectShape : public Base2P {
public:
    EraserRectShape() : Base2P(ToolId::EraserRect) {}
    void paint(QPainter& p, const PaintCtx& ctx) const override
    {
        const QRect b = QRect(m_p1, m_p2).normalized();
        if (b.isEmpty() || !ctx.base) return;
        const QRect want = baseRect(b, ctx);
        const QRect src = want.intersected(ctx.base->rect());
        if (src.isEmpty()) return;
        p.drawImage(clippedDest(b, want, src, ctx), *ctx.base, QRectF(src));   // restore original pixels
    }
};

// Brush redaction: pixelate/blur the base only along a stroked freehand path.
// Shares the EraserShape clip-to-stroke technique; the strength comes from
// style.redactStrength and the brush diameter from style.penWidth.
static void paintRedactStroke(QPainter& p, const PaintCtx& ctx, const QVector<QPoint>& pts,
                              const Style& style, bool blur)
{
    const QImage* srcImg = ctx.redactSource ? ctx.redactSource : ctx.base;
    if (pts.size() < 2 || !srcImg) return;
    QPainterPath path(pts.first());
    for (int i = 1; i < pts.size(); ++i) path.lineTo(pts[i]);
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(2, style.penWidth));
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    const QPainterPath stroked = stroker.createStroke(path);
    const QRect b = stroked.boundingRect().toRect();
    const QRect want = baseRect(b, ctx);
    const QRect src = want.intersected(srcImg->rect());
    if (src.isEmpty()) return;
    QImage out;
    if (blur) {
        const int radius = qMax(2, int(std::lround(qMax(2, style.redactStrength * 2) * ctx.scale)));
        out = render::blur(srcImg->copy(src), radius);
    } else {
        const int block = qMax(6, int(std::lround(qMax(2, style.redactStrength * 3) * ctx.scale)));
        out = render::mosaic(srcImg->copy(src), block);
    }
    p.save();
    p.setClipPath(stroked);
    p.setRenderHint(QPainter::SmoothPixmapTransform, blur);
    p.drawImage(clippedDest(b, want, src, ctx), out);
    p.restore();
}

class MosaicBrushShape : public BasePoly {
public:
    MosaicBrushShape() : BasePoly(ToolId::MosaicBrush) {}
    void paint(QPainter& p, const PaintCtx& ctx) const override
    { paintRedactStroke(p, ctx, m_pts, style, /*blur*/ false); }
};

class BlurBrushShape : public BasePoly {
public:
    BlurBrushShape() : BasePoly(ToolId::BlurBrush) {}
    void paint(QPainter& p, const PaintCtx& ctx) const override
    { paintRedactStroke(p, ctx, m_pts, style, /*blur*/ true); }
};

// ---- text -----------------------------------------------------------------
// Build the QFont a Text annotation renders with (family + style + rounded pixel
// size). Shared by the bounds/hit-test/paint math so they always agree.
static QFont textFont(const Style& s)
{
    QFont f(s.fontFamily);
    f.setPixelSize(qMax(6, int(qRound(s.fontPx))));
    f.setBold(s.bold);
    f.setItalic(s.italic);
    return f;
}

// Word-wrap `text` to `width` logical px (0 = no wrap), honoring explicit newlines.
// Accumulates char-by-char and rewinds to the last space for latin word breaks;
// scripts without spaces (CJK) fall through to character wrapping. Used by both the
// filled and hollow (outline) render paths so they lay out identically.
static QStringList wrapLines(const QString& text, const QFontMetrics& fm, int width)
{
    QStringList out;
    const QStringList paras = text.split(QLatin1Char('\n'));
    for (const QString& para : paras) {
        if (width <= 0) { out << para; continue; }
        QString line;
        for (const QChar ch : para) {
            if (!line.isEmpty() && fm.horizontalAdvance(line + ch) > width) {
                const int sp = line.lastIndexOf(QLatin1Char(' '));
                if (sp > 0) { out << line.left(sp); line = line.mid(sp + 1); }
                else        { out << line;          line.clear(); }
            }
            line += ch;
        }
        out << line;
    }
    return out;
}

class TextShape : public Annotation {
public:
    ToolId tool() const override { return ToolId::Text; }
    void setGeometry(const QVector<QPoint>& pts) override
    {
        if (!pts.isEmpty()) m_pos = pts.first();
    }
    QVector<QPoint> geometry() const override { return {m_pos}; }
    bool isEmpty() const override { return m_text.isEmpty(); }
    void setText(const QString& t) override { m_text = t; }
    QString text() const override { return m_text; }
    void setBox(const QSize& b) override { m_box = b; }
    QSize box() const override { return m_box; }
    void setRotation(qreal deg) override { m_rot = deg; }
    qreal rotation() const override { return m_rot; }
    void setMirrored(bool m) override { m_mirrored = m; }
    bool mirrored() const override { return m_mirrored; }

    // The un-rotated content rectangle (top-left anchor + wrap box). Falls back to
    // the text's own bounding size when no explicit box has been set.
    QRect contentRect() const
    {
        if (m_box.width() > 0 && m_box.height() > 0)
            return QRect(m_pos, m_box);
        const QFont f = textFont(style);
        const QFontMetrics fm(f);
        const QStringList lines = wrapLines(m_text, fm, 0);
        int w = 0;
        for (const QString& l : lines) w = qMax(w, fm.horizontalAdvance(l));
        const int h = qMax(1, lines.size()) * fm.lineSpacing();
        return QRect(m_pos, QSize(qMax(w, fm.averageCharWidth()), qMax(h, fm.height())));
    }

    QRect bounds() const override
    {
        const QRectF r(contentRect());
        if (qFuzzyIsNull(m_rot)) return r.toAlignedRect();
        QTransform t = rotationTransform(r);
        return t.mapRect(r).toAlignedRect();
    }
    void translate(const QPoint& d) override { m_pos += d; }
    bool hitTest(const QPoint& g) const override
    {
        const QRectF r(contentRect());
        QPointF local(g);
        if (!qFuzzyIsNull(m_rot))
            local = rotationTransform(r).inverted().map(QPointF(g));
        return r.adjusted(-4, -4, 4, 4).contains(local);
    }
    // The rotated content-rect corners (for the hover/selection outline). Inflated a
    // few px off the glyphs so the box reads clearly around the text, not on top of it.
    QVector<QPoint> outline() const override
    {
        const QRectF r = QRectF(contentRect()).adjusted(-4, -4, 4, 4);
        QPointF c[4] = { r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft() };
        QVector<QPoint> out;
        out.reserve(4);
        const QTransform t = qFuzzyIsNull(m_rot) ? QTransform() : rotationTransform(r);
        for (const QPointF& pt : c)
            out << t.map(pt).toPoint();
        return out;
    }
    void paint(QPainter& p, const PaintCtx&) const override
    {
        if (m_text.isEmpty()) return;
        const QFont f = textFont(style);
        const QFontMetrics fm(f);
        const QRect cr = contentRect();
        const QStringList lines = wrapLines(m_text, fm, cr.width());

        p.save();
        if (!qFuzzyIsNull(m_rot) || m_mirrored)
            p.setTransform(orientTransform(QRectF(cr)), true);
        p.setRenderHint(QPainter::Antialiasing, true);

        qreal y = cr.top() + fm.ascent();
        if (style.hollowText) {
            // White-surrounded text: a white halo stroked around the glyphs, then the
            // coloured glyphs filled on top (the fill covers the inner half of the
            // stroke, leaving a clean white border).
            QPainterPath path;
            for (const QString& l : lines) {
                path.addText(qreal(cr.left()), y, f, l);
                y += fm.lineSpacing();
            }
            p.setPen(QPen(Qt::white, qMax(2.0, style.fontPx / 8.0),
                          Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            p.setPen(Qt::NoPen);
            p.setBrush(style.color);
            p.drawPath(path);
        } else {
            p.setFont(f);
            p.setPen(style.color);
            for (const QString& l : lines) {
                p.drawText(QPointF(cr.left(), y), l);
                y += fm.lineSpacing();
            }
        }
        p.restore();
    }

protected:
    void writeJson(QJsonObject& o) const override
    {
        o[QStringLiteral("x")] = m_pos.x();
        o[QStringLiteral("y")] = m_pos.y();
        o[QStringLiteral("boxW")] = m_box.width();
        o[QStringLiteral("boxH")] = m_box.height();
        o[QStringLiteral("rot")] = m_rot;
        if (m_mirrored)                            // rare (pin-only) → omit the common case
            o[QStringLiteral("mirrored")] = true;
        o[QStringLiteral("text")] = m_text;
    }
    void readJson(const QJsonObject& o) override
    {
        m_pos = QPoint(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt());
        m_box = QSize(o.value(QStringLiteral("boxW")).toInt(), o.value(QStringLiteral("boxH")).toInt());
        m_rot = o.value(QStringLiteral("rot")).toDouble();
        m_mirrored = o.value(QStringLiteral("mirrored")).toBool(false);
        m_text = o.value(QStringLiteral("text")).toString();
    }

private:
    // Rotation about the content box's center, in global-logical space.
    QTransform rotationTransform(const QRectF& r) const
    {
        const QPointF c = r.center();
        QTransform t;
        t.translate(c.x(), c.y());
        t.rotate(m_rot);
        t.translate(-c.x(), -c.y());
        return t;
    }

    // Rotation plus (for a pin-mirrored text) a horizontal flip, both about the box
    // centre — the mirror applied first, so the glyphs mirror and the box then rotates.
    // Only paint() needs this: mirroring a box about its own centre maps it onto itself,
    // so bounds()/hitTest()/outline() are unaffected and keep using rotationTransform().
    QTransform orientTransform(const QRectF& r) const
    {
        if (!m_mirrored)
            return rotationTransform(r);
        const QPointF c = r.center();
        QTransform t;
        t.translate(c.x(), c.y());
        t.rotate(m_rot);
        t.scale(-1, 1);
        t.translate(-c.x(), -c.y());
        return t;
    }

    QPoint m_pos;
    QSize m_box;        // wrap box (logical px); empty = auto-size to text
    qreal m_rot = 0.0;  // degrees clockwise about the box center
    bool m_mirrored = false;   // pre-mirrored to survive a pin's flip (see setMirrored)
    QString m_text;
};

// ---- number label ---------------------------------------------------------
// Ring stroke width for the ringed/hollow looks — proportional so it reads the same
// at every badge size.
qreal labelRing(qreal d) { return qMax(1.5, d / 12.0); }

// The digit font: style.fontPx, shrunk just enough that `number` fits inside the
// fixed-diameter disc. Per spec the badge never grows for 2-3 digit numbers — the
// digits shrink instead, so a row of labels keeps a uniform footprint.
// The glyphs a badge is DRAWN with. Badges store a decimal ordinal; style.numberStyle
// picks how it reads. Anything that isn't an integer — notably the empty string while a
// badge is being renumbered — passes through untouched.
QString badgeGlyphs(const Style& s, const QString& ordinal)
{
    bool ok = false;
    const int n = ordinal.toInt(&ok);
    return ok ? formatNumber(n, s.numberStyle) : ordinal;
}

QFont labelFont(const Style& s, const QString& number, qreal d)
{
    QFont f = textFont(s);
    const qreal maxW = d * 0.72;
    const qreal w = QFontMetricsF(f).horizontalAdvance(number);
    if (w > maxW && w > 0.0)
        f.setPixelSize(qMax(6, int(std::floor(f.pixelSize() * maxW / w))));
    return f;
}

class NumberLabelShape : public Annotation {
public:
    ToolId tool() const override { return ToolId::NumberLabel; }
    // The stamped point is the badge's CENTRE (the cursor preview centres it too, so
    // what you see under the cursor is exactly what the click commits).
    void setGeometry(const QVector<QPoint>& pts) override
    {
        if (!pts.isEmpty()) m_pos = pts.first();
    }
    QVector<QPoint> geometry() const override { return {m_pos}; }
    bool isEmpty() const override { return m_num.isEmpty(); }
    // The number rides on the generic text plumbing — no extra virtuals needed.
    void setText(const QString& t) override { m_num = t; }
    QString text() const override { return m_num; }

    QRect bounds() const override
    {
        const qreal d = numberBadgeDiameter(style);
        const qreal r = d / 2.0 + labelRing(d);
        return QRectF(m_pos.x() - r, m_pos.y() - r, 2 * r, 2 * r).toAlignedRect();
    }
    void translate(const QPoint& d) override { m_pos += d; }
    bool hitTest(const QPoint& g) const override
    {
        const qreal r = numberBadgeDiameter(style) / 2.0;
        return std::hypot(qreal(g.x() - m_pos.x()), qreal(g.y() - m_pos.y())) <= r + 3.0;
    }
    void paint(QPainter& p, const PaintCtx&) const override
    {
        paintNumberBadge(p, style, QPointF(m_pos), m_num);
    }

protected:
    void writeJson(QJsonObject& o) const override
    {
        o[QStringLiteral("x")] = m_pos.x();
        o[QStringLiteral("y")] = m_pos.y();
        o[QStringLiteral("number")] = m_num;
    }
    void readJson(const QJsonObject& o) override
    {
        m_pos = QPoint(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt());
        m_num = o.value(QStringLiteral("number")).toString();
    }

private:
    QPoint m_pos;      // badge centre
    QString m_num;
};

std::unique_ptr<Annotation> makeBare(ToolId t)
{
    switch (t) {
        case ToolId::Rect:      return std::make_unique<RectShape>();
        case ToolId::Ellipse:   return std::make_unique<EllipseShape>();
        case ToolId::Line:      return std::make_unique<LineArrowShape>();
        case ToolId::Arrow:     return std::make_unique<ArrowShape>();
        case ToolId::LineStrip: return std::make_unique<LineStripShape>();
        case ToolId::Pencil:    return std::make_unique<PencilShape>();
        case ToolId::Marker:    return std::make_unique<MarkerShape>();
        case ToolId::MarkerRect: return std::make_unique<MarkerRectShape>();
        case ToolId::Mosaic:    return std::make_unique<MosaicShape>();
        case ToolId::Blur:      return std::make_unique<BlurShape>();
        case ToolId::MosaicBrush: return std::make_unique<MosaicBrushShape>();
        case ToolId::BlurBrush:   return std::make_unique<BlurBrushShape>();
        case ToolId::Text:      return std::make_unique<TextShape>();
        case ToolId::Eraser:    return std::make_unique<EraserShape>();
        case ToolId::EraserRect: return std::make_unique<EraserRectShape>();
        case ToolId::NumberLabel: return std::make_unique<NumberLabelShape>();
        case ToolId::None:      break;
    }
    return nullptr;
}

} // namespace

// Default outline path: the polygon from outline(). Base2P/Ellipse override to
// return a (rotated) rect / ellipse so the hover highlight matches the shape.
QPainterPath Annotation::outlinePath() const
{
    QPainterPath path;
    const QVector<QPoint> poly = outline();
    if (poly.isEmpty())
        return path;
    path.moveTo(poly.first());
    for (int i = 1; i < poly.size(); ++i)
        path.lineTo(poly[i]);
    path.closeSubpath();
    return path;
}

qreal numberBadgeDiameter(const Style& style)
{
    return qMax(8.0, qreal(style.fontPx) * 2.0);
}

// Render a (multi-segment) line: a shaft carrying an independent decoration at each end.
// The shaft is either a pen stroke (honouring the dash patterns) or — under the Tapered
// line styles — a filled/stroked silhouette that widens from start to end. Exported (see
// the header) so the toolbar's dropdowns can draw their items with the real renderer.
void paintLineArrow(QPainter& p, const QVector<QPoint>& ipts, const Style& s)
{
    if (ipts.size() < 2) return;
    QVector<QPointF> pts = linePath(ipts, s.curved);
    const double w = qMax(1, s.penWidth);

    // Caps are pen-sized on every style but tapered-hollow, whose ribbon is wide enough
    // to swallow them (see capGeometry). ===> tune the cap scale here <===
    const double capScale = s.lineStyle == LineStyle::TaperedHollow ? 1.8 : 1.0;
    const CapGeom capA = capGeometry(s.startCap, pts.first(), outwardDir(pts, true),  w, capScale);
    const CapGeom capB = capGeometry(s.endCap,   pts.back(),  outwardDir(pts, false), w, capScale);

    // A cap merges into the tapered silhouette only when the shaft is drawn the SAME WAY
    // it is: a filled cap into a filled (Tapered) shaft, an outline cap into a stroked
    // (TaperedHollow) one. Merging across the two — which is what the first cut did —
    // makes a solid arrowhead come out hollow on a TaperedHollow line, contradicting the
    // cap dropdown's own icon. Everything else is simply drawn on top: same-colour fills
    // overlap invisibly, and the trim already stops the shaft inside the cap.
    //
    // Decided BEFORE the trim, because a merged cap wants a different one (mergeInset).
    const bool tapered = isTapered(s.lineStyle);
    const bool hollowShaft = s.lineStyle == LineStyle::TaperedHollow;
    auto merges = [&](const CapGeom& g) {
        return tapered && g.unite && !g.path.isEmpty() && g.fill != hollowShaft;
    };
    const bool mergeA = merges(capA), mergeB = merges(capB);

    // Trim so the shaft cannot poke out through a hollow cap. The start is trimmed by
    // reversing — one tail-trimmer, used twice.
    trimTail(pts, mergeB ? capB.mergeInset : capB.inset);
    std::reverse(pts.begin(), pts.end());
    trimTail(pts, mergeA ? capA.mergeInset : capA.inset);
    std::reverse(pts.begin(), pts.end());

    // Caps whose back edge crosses the shaft go UNDER it, so the shaft covers that edge
    // and the junction reads open instead of barred. Everything else goes on top.
    const bool underA = !mergeA && capA.coverBack;
    const bool underB = !mergeB && capB.coverBack;
    if (underA) drawCap(p, capA, s.color, w);
    if (underB) drawCap(p, capB, s.color, w);

    if (tapered) {
        // Thin at the start, thick at the end, so it still reads as flowing toward the end.
        // ⚠️ The hollow variant needs a wider ribbon than the filled one: it is drawn as an
        // outline at the full pen width (so its weight matches the pencil and every hollow
        // cap), and a w-wide stroke centred on a ribbon only 1.3w across would close the
        // interior completely. But it must NOT get much wider than this — the caps are
        // sized off the pen width alone, so an over-wide ribbon swallows the arrowhead and
        // the line reads as a wedge with a nub. 1.8w leaves 0.8w of visible interior at the
        // head while still clearing the head comfortably.
        // ===> tune the taper here <===
        const double wTail = hollowShaft ? w * 0.8 : w * 0.35;
        const double wHead = hollowShaft ? w * 2.8 : w * 1.3;
        QPainterPath silhouette = taperedOutline(pts, qMax(1.0, wTail), wHead);
        bool rounded = false;
        if (mergeA) { silhouette = silhouette.united(capA.path); rounded |= capA.round; }
        if (mergeB) { silhouette = silhouette.united(capB.path); rounded |= capB.round; }
        silhouette.setFillRule(Qt::WindingFill);     // united() resets it to odd-even
        if (hollowShaft) {
            p.strokePath(silhouette, QPen(s.color, w,
                                          Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        } else {
            p.fillPath(silhouette, s.color);
            if (rounded)                             // round the merged head's corners
                p.strokePath(silhouette, QPen(s.color, qMax(1.5, w * 0.55),
                                              Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
    } else {
        p.setPen(strokePen(s, Qt::RoundCap, /*dash*/ true));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(pts.constData(), pts.size());
    }

    // Caps already merged into the silhouette (or drawn underneath) must not be drawn
    // twice — a rounded one would be stroked a second time and grow.
    if (!mergeA && !underA) drawCap(p, capA, s.color, w);
    if (!mergeB && !underB) drawCap(p, capB, s.color, w);
}

QColor numberBadgeInk(const Style& s)
{
    // The filled looks carry white digits; the hollow one draws them in the style colour.
    return s.label == LabelStyle::Hollow ? s.color : QColor(Qt::white);
}

void paintNumberBadge(QPainter& p, const Style& s, const QPointF& c, const QString& number)
{
    const QString glyphs = badgeGlyphs(s, number);
    const qreal d = numberBadgeDiameter(s);
    const qreal ring = labelRing(d);
    const QRectF full(c.x() - d / 2.0, c.y() - d / 2.0, d, d);
    // Inset by half the ring so a stroked badge stays inside the same footprint.
    const QRectF disc = full.adjusted(ring / 2.0, ring / 2.0, -ring / 2.0, -ring / 2.0);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    switch (s.label) {
        case LabelStyle::Filled:
            p.setPen(Qt::NoPen);
            p.setBrush(s.color);
            p.drawEllipse(full);
            break;
        case LabelStyle::RingedFilled:
            p.setPen(QPen(Qt::white, ring));
            p.setBrush(s.color);
            p.drawEllipse(disc);
            break;
        case LabelStyle::Hollow:
            // Interior left transparent — the capture shows through.
            p.setPen(QPen(s.color, ring));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(disc);
            break;
    }
    if (!glyphs.isEmpty()) {
        p.setFont(labelFont(s, glyphs, d));
        p.setPen(numberBadgeInk(s));
        p.setBrush(Qt::NoBrush);
        p.drawText(full, Qt::AlignCenter, glyphs);
    }
    p.restore();
}

QRectF numberBadgeCaretRect(const Style& s, const QPointF& c, const QString& number)
{
    // The digits are centred in the disc, so the caret sits half their advance to the
    // right of centre. Uses the same (possibly shrunk) font the badge renders with.
    const QString glyphs = badgeGlyphs(s, number);
    const qreal d = numberBadgeDiameter(s);
    const QFontMetricsF fm(labelFont(s, glyphs, d));
    const qreal w = fm.horizontalAdvance(glyphs);
    const qreal h = fm.ascent() + fm.descent();
    return QRectF(c.x() + w / 2.0 + qMax(1.0, d * 0.04),
                  c.y() - h / 2.0,
                  qMax(1.0, d / 20.0), h);
}

std::unique_ptr<Annotation> create(ToolId t, const Style& style)
{
    std::unique_ptr<Annotation> a = makeBare(t);
    if (a) a->style = style;
    return a;
}

QJsonObject Annotation::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("tool")] = toolIdToString(tool());
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("color")] = style.color.name(QColor::HexArgb);
    o[QStringLiteral("penWidth")] = style.penWidth;
    o[QStringLiteral("fontPx")] = style.fontPx;
    o[QStringLiteral("fontFamily")] = style.fontFamily;
    o[QStringLiteral("bold")] = style.bold;
    o[QStringLiteral("italic")] = style.italic;
    o[QStringLiteral("hollowText")] = style.hollowText;
    o[QStringLiteral("redactStrength")] = style.redactStrength;
    o[QStringLiteral("filled")] = style.filled;
    o[QStringLiteral("startCap")] = capStyleToString(style.startCap);
    o[QStringLiteral("endCap")] = capStyleToString(style.endCap);
    o[QStringLiteral("curved")] = style.curved;
    o[QStringLiteral("lineStyle")] = lineStyleToString(style.lineStyle);
    o[QStringLiteral("label")] = labelStyleToString(style.label);
    o[QStringLiteral("numberStyle")] = numberStyleToString(style.numberStyle);
    writeJson(o);
    return o;
}

std::unique_ptr<Annotation> Annotation::fromJson(const QJsonObject& o)
{
    const ToolId t = toolIdFromString(o.value(QStringLiteral("tool")).toString());
    std::unique_ptr<Annotation> a = makeBare(t);
    if (!a) return nullptr;
    a->id = o.value(QStringLiteral("id")).toInt();
    a->style.color = QColor(o.value(QStringLiteral("color")).toString());
    if (!a->style.color.isValid()) a->style.color = Style{}.color;
    a->style.penWidth = o.value(QStringLiteral("penWidth")).toInt(Style{}.penWidth);
    a->style.fontPx = o.value(QStringLiteral("fontPx")).toDouble(Style{}.fontPx);
    a->style.fontFamily = o.value(QStringLiteral("fontFamily")).toString(Style{}.fontFamily);
    a->style.bold = o.value(QStringLiteral("bold")).toBool(Style{}.bold);
    a->style.italic = o.value(QStringLiteral("italic")).toBool(Style{}.italic);
    a->style.hollowText = o.value(QStringLiteral("hollowText")).toBool(Style{}.hollowText);
    a->style.redactStrength = o.value(QStringLiteral("redactStrength")).toInt(Style{}.redactStrength);
    a->style.filled = o.value(QStringLiteral("filled")).toBool(Style{}.filled);
    a->style.curved = o.value(QStringLiteral("curved")).toBool(Style{}.curved);
    a->style.lineStyle = lineStyleFromString(o.value(QStringLiteral("lineStyle")).toString());
    if (o.contains(QStringLiteral("startCap")) || o.contains(QStringLiteral("endCap"))) {
        a->style.startCap = capStyleFromString(o.value(QStringLiteral("startCap")).toString());
        a->style.endCap = capStyleFromString(o.value(QStringLiteral("endCap")).toString());
    } else {
        // Sessions/history written before the line ends became independent stored one
        // `arrow` field that conflated the head with the shaft taper. Map it forward so
        // an existing pin keeps the arrow it was saved with.
        const QString legacy = o.value(QStringLiteral("arrow")).toString();
        a->style.startCap = CapStyle::None;
        a->style.endCap = legacy.isEmpty() || legacy == QLatin1String("none")
                              ? CapStyle::None : CapStyle::ArrowSharp;
        if (legacy == QLatin1String("tapered_solid"))
            a->style.lineStyle = LineStyle::Tapered;
        else if (legacy == QLatin1String("tapered_hollow"))
            a->style.lineStyle = LineStyle::TaperedHollow;
    }
    a->style.label = labelStyleFromString(o.value(QStringLiteral("label")).toString());
    a->style.numberStyle = numberStyleFromString(o.value(QStringLiteral("numberStyle")).toString());
    a->readJson(o);
    return a;
}

} // namespace annotation
