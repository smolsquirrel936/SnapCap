#include "ui/PaintBar.h"

#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QFontMetrics>
#include <QLineEdit>
#include <QIntValidator>
#include <QFontDatabase>
#include <QSpinBox>
#include <QLabel>
#include <QFrame>
#include <QIcon>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QSize>
#include <QDir>
#include <QEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QCoreApplication>
#include <QToolTip>
#include <algorithm>
#include <utility>

#include "core/i18n/I18n.h"
#include "core/render/Raster.h"
#include "core/Settings.h"
#include "ui/ColorDialog.h"
#include "ui/PrefsStyle.h"

using annotation::ToolId;

namespace {
// A combo whose collapsed control keeps its fixed (narrow) width, but whose drop-down
// list widens to fit the longest item — so long font names show in full when opened
// even though the toolbar control stays compact. (Independent popup width.)
class WideMenuCombo : public QComboBox {
public:
    explicit WideMenuCombo(QWidget* parent = nullptr) : QComboBox(parent) {}
protected:
    void showPopup() override
    {
        const QFontMetrics fm(view()->font());
        int w = 0;
        for (int i = 0; i < count(); ++i)
            w = qMax(w, fm.horizontalAdvance(itemText(i)));
        // item padding + frame + the popup's scrollbar so nothing is clipped/elided
        w += 28 + style()->pixelMetric(QStyle::PM_ScrollBarExtent);
        view()->setMinimumWidth(qMax(w, width()));
        QComboBox::showPopup();
    }
};

// Draws an icon-only item with the icon CENTRED. Qt's default view delegate hugs the
// decoration to the leading edge (it lays the icon out as if text followed it), which in
// a column of otherwise identical glyphs reads as a misalignment. Rather than fight the
// style's item layout, the icon is stripped from the style option so the base delegate
// paints only the row background/highlight, and it is then drawn into the middle of the
// row here.
//
// ⚠️ The strip MUST happen in initStyleOption, not in paint(): QStyledItemDelegate::paint
// re-runs initStyleOption on its own copy of the option, so an icon cleared in paint() is
// simply restored from the model and drawn anyway — giving two icons per row, one left
// and one centred.
class CenteredIconDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* o, const QModelIndex& idx) const override
    {
        QStyledItemDelegate::initStyleOption(o, idx);
        o->icon = QIcon();
        o->features &= ~QStyleOptionViewItem::HasDecoration;
        o->text.clear();
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        QStyledItemDelegate::paint(p, opt, idx);      // background + selection only
        const QIcon icon = qvariant_cast<QIcon>(idx.data(Qt::DecorationRole));
        if (icon.isNull())
            return;
        const QRect r = QStyle::alignedRect(opt.direction, Qt::AlignCenter,
                                            opt.decorationSize, opt.rect);
        icon.paint(p, r, Qt::AlignCenter,
                   (opt.state & QStyle::State_Selected) ? QIcon::Selected : QIcon::Normal);
    }

    // ...and because the option no longer carries a decoration, the base size hint would
    // collapse to an empty item. Put the icon's width back.
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        s.setWidth(qMax(s.width(), opt.decorationSize.width() + 8));
        return s;
    }
};

// A tool GROUP = one row-1 icon button + the row-2 variants it expands to. The
// `redact` group has no generic variants; it drives the special redact panel.
// `icon` (optional) makes the variant button an icon button instead of a text one.
struct VariantDef { ToolId id; const char* key; const char* icon; };
// `fillable` = the rect/ellipse group (swaps the width spin box for the hollow/solid
// pair, the hollow dot being the scroll-to-resize width control). `arrowy` = the
// lines group, which reuses that width dot and adds the arrow-style dropdown.
// `widthDot` = a freehand-pen group (pencil) that shows the same scroll-to-resize
// width dot as shapes/lines instead of the plain width spin box. `marker` drives the
// special highlighter panel (freehand width dot + rectangle-highlighter toggle + palette).
// `label` drives the special number-label panel (3 badge-look toggles + size box +
// palette).
// `dashable` = the groups whose stroke can carry a dash pattern (shapes + pencil), which
// get the line-style dropdown just left of the colour palette.
struct GroupDef   { const char* icon; const char* tip; const VariantDef* vars; int count;
                    bool redact; bool fillable; bool arrowy; bool eraser; bool widthDot;
                    bool marker; bool text; bool label; bool dashable; };

const VariantDef kShape[]  = {{ToolId::Rect, "edit.tool.rect", "rectangle"},
                              {ToolId::Ellipse, "edit.tool.ellipse", "ellipse"}};
const VariantDef kLine[]   = {{ToolId::Line, "edit.tool.line", nullptr}};
const VariantDef kPencil[] = {{ToolId::Pencil, "edit.tool.pencil", nullptr}};
const VariantDef kMarker[] = {{ToolId::Marker, "edit.tool.marker", nullptr}};
const VariantDef kText[]   = {{ToolId::Text, "edit.tool.text", nullptr}};
const VariantDef kLabel[]  = {{ToolId::NumberLabel, "edit.tool.numberLabel", nullptr}};
const VariantDef kEraser[] = {{ToolId::Eraser, "edit.tool.eraser", nullptr}};

const GroupDef kGroups[] = {
    {"rectangle_ellipse", "edit.group.shapes", kShape,  2, false, true,  false, false, false, false, false, false, true },
    {"arrow",             "edit.group.lines",  kLine,   1, false, false, true , false, false, false, false, false, false},
    {"pencil",            "edit.tool.pencil",  kPencil, 1, false, false, false, false, true , false, false, false, true },
    {"marker_pen",        "edit.tool.marker",  kMarker, 1, false, false, false, false, false, true , false, false, false},
    {"mosaic",            "edit.group.redact", nullptr, 0, true,  false, false, false, false, false, false, false, false},
    {"text",              "edit.tool.text",    kText,   1, false, false, false, false, false, false, true , false, false},
    {"number_label",      "edit.tool.numberLabel", kLabel, 1, false, false, false, false, false, false, false, true , false},
    {"eraser",            "edit.tool.eraser",  kEraser, 1, false, false, false, true , false, false, false, false, false},
};
const int kGroupCount = int(sizeof(kGroups) / sizeof(kGroups[0]));

// The line tool's end decorations, in dropdown order. BOTH cap dropdowns (start and end)
// are built from this one table — the start one just mirrors each icon — so the two ends
// can never offer different sets. Icons are rendered by the real annotation renderer
// (capIcon), not loaded from icons/.
struct CapDef { annotation::CapStyle style; const char* tip; };
const CapDef kCaps[] = {
    {annotation::CapStyle::None,           "edit.cap.none"},
    {annotation::CapStyle::ArrowSharp,     "edit.cap.arrowSharp"},
    {annotation::CapStyle::ArrowRound,     "edit.cap.arrowRound"},
    {annotation::CapStyle::ArrowChevron,   "edit.cap.arrowChevron"},
    {annotation::CapStyle::ArrowHollow,    "edit.cap.arrowHollow"},
    {annotation::CapStyle::Dot,            "edit.cap.dot"},
    {annotation::CapStyle::HollowDot,      "edit.cap.hollowDot"},
    {annotation::CapStyle::Diamond,        "edit.cap.diamond"},
    {annotation::CapStyle::HollowDiamond,  "edit.cap.hollowDiamond"},
    {annotation::CapStyle::Bar,            "edit.cap.bar"},
};
const int kCapCount = int(sizeof(kCaps) / sizeof(kCaps[0]));

int capToIndex(annotation::CapStyle s)
{
    for (int i = 0; i < kCapCount; ++i)
        if (kCaps[i].style == s) return i;
    return 0;   // default: no decoration
}

// The five stroke dash patterns, in dropdown order. Index ↔ LineStyle mapping; the icons
// are drawn in code (dashIcon) rather than loaded from icons/, like the other glyph icons
// here — a horizontal sample of the pattern itself.
struct DashDef { annotation::LineStyle style; const char* tip; };
const DashDef kDashes[] = {
    {annotation::LineStyle::Solid,      "edit.linestyle.solid"},
    {annotation::LineStyle::Dash,       "edit.linestyle.dash"},
    {annotation::LineStyle::Dot,        "edit.linestyle.dot"},
    {annotation::LineStyle::DashDot,    "edit.linestyle.dashDot"},
    {annotation::LineStyle::DashDotDot, "edit.linestyle.dashDotDot"},
    // Line-tool only — a tapered shaft is a filled silhouette, not a dash pattern, and
    // a closed rect/ellipse has no thin end for it to start from. The shapes/pencil
    // dropdown is populated with kDashCount entries, the line one with kLineDashCount.
    {annotation::LineStyle::Tapered,       "edit.linestyle.tapered"},
    {annotation::LineStyle::TaperedHollow, "edit.linestyle.taperedHollow"},
};
const int kLineDashCount = int(sizeof(kDashes) / sizeof(kDashes[0]));
const int kDashCount = kLineDashCount - 2;

// Searches ALL entries, not just the first kDashCount: the shapes/pencil combo can never
// hold a tapered style, so one lookup serves both combos.
int dashToIndex(annotation::LineStyle s)
{
    for (int i = 0; i < kLineDashCount; ++i)
        if (kDashes[i].style == s) return i;
    return 0;   // default: solid
}

// A sample of one dash pattern: a single horizontal stroke across the icon, in the same
// neutral ink as the other code-drawn icons. Qt's dash lengths are in units of pen width,
// so a 2px pen keeps the sample readable at this size.
QIcon dashIcon(annotation::LineStyle s)
{
    const int w = 44, h = 24;
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    // The tapered styles are not pen patterns at all, so they can't be sampled with a
    // pen — they are drawn by the real renderer instead, which is also what keeps the
    // item honest about what the tool will draw.
    if (annotation::isTapered(s)) {
        p.setRenderHint(QPainter::Antialiasing, true);
        annotation::Style ls;
        ls.color = QColor(0x33, 0x33, 0x33);
        // The five dash rows sample themselves with a 2px pen, so a tapered row has to
        // stay near that weight or it dominates the list. 3 is the floor that still
        // leaves a readable gap down the middle of the hollow variant (its outline is
        // penWidth/2, so the gap is penWidth - penWidth/2 at the thick end).
        ls.penWidth = 3;
        ls.lineStyle = s;
        ls.startCap = ls.endCap = annotation::CapStyle::None;
        annotation::paintLineArrow(p, {QPoint(3, h / 2), QPoint(w - 3, h / 2)}, ls);
        p.end();
        return QIcon(pm);
    }
    Qt::PenStyle ps = Qt::SolidLine;
    switch (s) {
        case annotation::LineStyle::Dash:       ps = Qt::DashLine; break;
        case annotation::LineStyle::Dot:        ps = Qt::DotLine; break;
        case annotation::LineStyle::DashDot:    ps = Qt::DashDotLine; break;
        case annotation::LineStyle::DashDotDot: ps = Qt::DashDotDotLine; break;
        case annotation::LineStyle::Tapered:
        case annotation::LineStyle::TaperedHollow:
        case annotation::LineStyle::Solid:      break;
    }
    p.setPen(QPen(QColor(0x33, 0x33, 0x33), 2.0, ps, Qt::FlatCap));
    p.drawLine(3, h / 2, w - 3, h / 2);
    p.end();
    return QIcon(pm);
}

// One end-cap dropdown item: a short line carrying that cap, drawn by the real
// annotation renderer so an item can never drift from what the tool actually draws.
// `atStart` puts the decoration on the left end, for the start-cap dropdown.
//
// kCapIconW/H must match the combo's iconSize exactly, or Qt rescales the pixmap and
// softens every glyph. ⚠️ kCapIconInset has to clear the WIDEST cap that straddles its
// endpoint rather than extending inward: the dot caps are centred ON the end, so an inset
// smaller than their radius (penWidth * 1.7) clips them against the pixmap edge.
const int kCapIconW = 50, kCapIconH = 22, kCapIconInset = 9;

QIcon capIcon(annotation::CapStyle c, bool atStart)
{
    QPixmap pm(kCapIconW, kCapIconH);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    annotation::Style s;
    s.color = QColor(0x33, 0x33, 0x33);
    s.penWidth = 3;
    s.startCap = atStart ? c : annotation::CapStyle::None;
    s.endCap   = atStart ? annotation::CapStyle::None : c;
    const int y = kCapIconH / 2;
    annotation::paintLineArrow(
        p, {QPoint(kCapIconInset, y), QPoint(kCapIconW - kCapIconInset, y)}, s);
    p.end();
    return QIcon(pm);
}

// First-use pen width per tool (before the user has adjusted/saved one). penWidth is
// a base value; Marker/Eraser scale it up at paint time, so their bases stay small.
int defaultWidthFor(ToolId t)
{
    switch (t) {
        case ToolId::Marker: return 4;
        case ToolId::Eraser: return 24;   // circle-eraser diameter (used directly, not scaled)
        default:             return 3;
    }
}

// A dotted (dashed) rectangle outline icon for the rectangle-eraser button.
QPixmap dottedRectPixmap(int box)
{
    QPixmap pm(box, box);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(QColor(0x33, 0x33, 0x33), 1.4, Qt::DotLine, Qt::SquareCap, Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const qreal m = 2.5;
    p.drawRect(QRectF(m, m, box - 2 * m - 1, box - 2 * m - 1));
    p.end();
    return pm;
}

// A dotted-outline, solid-filled HORIZONTAL rectangle (wider than tall) for the
// regional-mosaic button — the solid fill marks "redact a rectangular region", the
// dotted border distinguishes it from the square dotted rect-eraser icon.
QPixmap dottedHRectPixmap(int box)
{
    QPixmap pm(box, box);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    const qreal mx = 2.5;
    const qreal my = box * 0.28;        // taller top/bottom margins → a horizontal rect
    const QRectF r(mx, my, box - 2 * mx - 1, box - 2 * my - 1);
    p.fillRect(r, QColor(0x33, 0x33, 0x33, 90));        // solid (translucent) fill
    p.setBrush(Qt::NoBrush);
    p.end();
    return pm;
}

// A translucent-filled rectangle icon for the rectangle-highlighter button (the
// rect counterpart of the freehand marker; the fill alpha hints "highlighter").
QPixmap markerRectPixmap(int box)
{
    QPixmap pm(box, box);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal m = 2.5;
    const QRectF r(m, m, box - 2 * m, box - 2 * m);
    p.setPen(QPen(QColor(0x33, 0x33, 0x33), 1.4));
    p.setBrush(QColor(0x33, 0x33, 0x33, 90));        // translucent fill = highlighter
    p.drawRect(r);
    p.end();
    return pm;
}

// Custom-drawn glyph icon for the Bold toggle: a single filled, bold letter so the
// button previews exactly what it does.
QPixmap glyphIcon(const QString& s, bool bold, bool italic, int box)
{
    QPixmap pm(box, box);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont f;
    f.setPixelSize(int(box * 0.82));
    f.setBold(bold);
    f.setItalic(italic);
    const QColor ink(0x22, 0x22, 0x22);
    const QFontMetrics fm(f);
    const qreal x = (box - fm.horizontalAdvance(s)) / 2.0;
    const qreal y = (box + fm.ascent() - fm.descent()) / 2.0;
    p.setFont(f);
    p.setPen(ink);
    p.drawText(QPointF(x, y), s);
    p.end();
    return pm;
}

// The Italic toggle: a serifed capital "I" (top + bottom serif bars and a stem),
// sheared to the right so it unmistakably reads as italic. Hand-built from rects so
// it doesn't depend on whatever the system "I" glyph looks like.
QPixmap italicSerifIcon(int box)
{
    QPixmap pm(box, box);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink(0x22, 0x22, 0x22);
    const qreal B = box;
    const qreal top = B * 0.18, bot = B * 0.82, cx = B * 0.50;
    const qreal sw = B * 0.17;     // serif bar half-width
    const qreal th = B * 0.085;    // bar thickness
    const qreal stw = B * 0.06;    // stem half-width
    QPainterPath path;
    path.addRect(QRectF(cx - sw, top,        2 * sw, th));         // top serif
    path.addRect(QRectF(cx - stw, top,       2 * stw, bot - top)); // stem
    path.addRect(QRectF(cx - sw, bot - th,   2 * sw, th));         // bottom serif
    QTransform t;                  // italic shear about the icon centre
    t.translate(cx, B * 0.5);
    t.shear(-0.28, 0);
    t.translate(-cx, -B * 0.5);
    p.setTransform(t);
    p.fillPath(path, ink);
    p.end();
    return pm;
}

// The Outline-text toggle: a hollow (stroked, unfilled) capital "A" — two legs to a
// shared apex plus a crossbar — so it clearly previews outline text.
QPixmap hollowAIcon(int box)
{
    QPixmap pm(box, box);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink(0x22, 0x22, 0x22);
    const qreal B = box;
    const qreal top = B * 0.16, bot = B * 0.84, apexX = B * 0.50;
    const qreal lx = B * 0.20, rx = B * 0.80;   // base feet
    auto legX = [&](qreal footX, qreal yy) {     // x on a leg at height yy
        const qreal t = (yy - top) / (bot - top);
        return apexX + (footX - apexX) * t;
    };
    const qreal yc = B * 0.62;                   // crossbar height
    QPainterPath a;
    a.moveTo(lx, bot);
    a.lineTo(apexX, top);
    a.lineTo(rx, bot);
    p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(a);
    p.drawLine(QPointF(legX(lx, yc), yc), QPointF(legX(rx, yc), yc));
    p.end();
    return pm;
}

// Font size shown in the size box — always a whole number.
QString fmtFontSize(qreal v)
{
    return QString::number(qMax(1, qRound(v)));
}

// Settings key for a tool's remembered width (group [edit], e.g. width_line).
QString widthKey(ToolId t)
{
    return QStringLiteral("edit/width_%1").arg(annotation::toolIdToString(t));
}

// Settings key for a tool's remembered dash pattern (group [edit], e.g. line_style_rect).
// Per-tool like widthKey: rect, ellipse and pencil each keep their own.
QString lineStyleKey(ToolId t)
{
    return QStringLiteral("edit/line_style_%1").arg(annotation::toolIdToString(t));
}

// Settings key for the number-label badge size. Its own key (not widthKey) because the
// badge's size rides on the digit's font size, not penWidth.
const char kLabelSizeKey[] = "edit/label_size";

// Settings key for the number-label numbering style (1.2.3 / A.B.C / a.b.c / I.II.III /
// i.ii.iii). One key for the whole bar: the style applies to every badge, not per tool.
const char kLabelNumberingKey[] = "edit/label_numbering";

// The numbering dropdown's five entries, in toolbar order. `sample` is what the item
// shows — the style spelled out in its own glyphs, so the list reads like the reference.
struct NumberingDef { annotation::NumberStyle style; const char* sample; const char* tip; };
const NumberingDef kNumberings[] = {
    {annotation::NumberStyle::Decimal,    "1.2.3",    "edit.numbering.decimal"},
    {annotation::NumberStyle::UpperAlpha, "A.B.C",    "edit.numbering.upperAlpha"},
    {annotation::NumberStyle::LowerAlpha, "a.b.c",    "edit.numbering.lowerAlpha"},
    {annotation::NumberStyle::UpperRoman, "I.II.III", "edit.numbering.upperRoman"},
    {annotation::NumberStyle::LowerRoman, "i.ii.iii", "edit.numbering.lowerRoman"},
};
const int kNumberingCount = int(sizeof(kNumberings) / sizeof(kNumberings[0]));

int numberingToIndex(annotation::NumberStyle s)
{
    for (int i = 0; i < kNumberingCount; ++i)
        if (kNumberings[i].style == s)
            return i;
    return 0;
}

// Settings key for the last colour the user picked. One key for the whole bar (not
// per-tool like widthKey): the colour is shared by every tool, and both hosts seed their
// style from currentColor() right after setSettings(), so restoring it here is enough.
const char kLastColorKey[] = "edit/last_color";

// Settings keys for the line tool's own state. One key each for the whole bar (the line
// tool is the only one that has them), restored quietly in setSettings() and re-emitted
// by selectGroup(). The line tool's *stroke* style is not here — it rides the shared
// per-tool `edit/line_style_<tool>` key like every other dashable tool.
const char kCurvedKey[]   = "edit/line_curved";
const char kStartCapKey[] = "edit/line_start_cap";
const char kEndCapKey[]   = "edit/line_end_cap";

// The shared 20-colour board (classic MS-Paint palette), laid out as a
// 2-row × 10-column grid; row 0 is the top row in the picker. The picker-entry
// block (m_pickerEntry) sits to its left and covers everything else with alpha.
// ====> Tune the basic colours here. <====
const QColor kBasicColors[2][10] = {
    { QColor("#000000"), QColor("#7F7F7F"), QColor("#880015"), QColor("#ED1C24"),
      QColor("#FF7F27"), QColor("#FFF200"), QColor("#22B14C"), QColor("#00A2E8"),
      QColor("#3F48CC"), QColor("#A349A4") },
    { QColor("#FFFFFF"), QColor("#C3C3C3"), QColor("#B97A57"), QColor("#FFAEC9"),
      QColor("#FFC90E"), QColor("#EFE4B0"), QColor("#B5E61D"), QColor("#99D9EA"),
      QColor("#7092BE"), QColor("#C8BFE7") },
};

const QColor kAccent(0x2d, 0x7d, 0xff);

// A small high-contrast sample patch, run through the real raster effect so the
// redact type button is a live preview of "type at the current degree".
QImage redactPreview(bool blur, int strength, QSize sz, const QColor& accent)
{
    QImage sample(48, 48, QImage::Format_ARGB32);
    sample.fill(Qt::white);
    {
        QPainter sp(&sample);
        sp.setPen(QPen(QColor(40, 40, 40), 3));
        for (int x = -48; x < 96; x += 9)            // diagonal stripes = high frequency
            sp.drawLine(x, 48, x + 48, 0);
        sp.setPen(Qt::NoPen);
        sp.setBrush(accent);
        sp.drawEllipse(QPoint(24, 24), 9, 9);        // an accent blob
    }
    QImage out = blur ? render::blur(sample, std::max(1, strength))
                      : render::mosaic(sample, std::max(2, strength));
    return out.scaled(sz, Qt::IgnoreAspectRatio,
                      blur ? Qt::SmoothTransformation : Qt::FastTransformation);
}
} // namespace

// ===== RedactSlider =========================================================
RedactSlider::RedactSlider(QWidget* parent) : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(22);
    setMinimumWidth(80);
}
QSize RedactSlider::sizeHint() const { return QSize(90, 22); }

void RedactSlider::setRange(int lo, int hi)
{
    m_min = lo; m_max = qMax(lo + 1, hi);
    setValue(m_value);
}
void RedactSlider::setValue(int v)
{
    v = qBound(m_min, v, m_max);
    if (v == m_value) return;
    m_value = v;
    update();
    emit valueChanged(v);
}
void RedactSlider::setSquareHandle(bool square)
{
    if (m_square != square) { m_square = square; update(); }
}

void RedactSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const int r = 7;                                  // handle half-size
    const int x0 = r, x1 = width() - r, cy = height() / 2;
    const double t = double(m_value - m_min) / double(m_max - m_min);
    const int hx = x0 + int(std::lround(t * (x1 - x0)));

    p.setPen(QPen(QColor(0xc0, 0xc0, 0xc0), 2));      // track
    p.drawLine(x0, cy, x1, cy);
    p.setPen(QPen(m_accent, 2));                       // filled portion
    p.drawLine(x0, cy, hx, cy);

    p.setPen(QPen(Qt::white, 1));
    p.setBrush(m_accent);
    if (m_square)
        p.drawRect(QRect(hx - r, cy - r, 2 * r, 2 * r));     // mosaic = square
    else
        p.drawEllipse(QPoint(hx, cy), r, r);                 // blur = round
}
void RedactSlider::setFromX(int x)
{
    const int r = 7, x0 = r, x1 = width() - r;
    const double t = double(qBound(x0, x, x1) - x0) / double(qMax(1, x1 - x0));
    setValue(int(std::lround(m_min + t * (m_max - m_min))));
}
void RedactSlider::mousePressEvent(QMouseEvent* e) { setFromX(e->position().toPoint().x()); }
void RedactSlider::mouseMoveEvent(QMouseEvent* e)  { setFromX(e->position().toPoint().x()); }
void RedactSlider::wheelEvent(QWheelEvent* e)
{
    setValue(m_value + (e->angleDelta().y() > 0 ? 1 : -1));
    e->accept();
}

// ===== Font hover specimen ==================================================
namespace {
constexpr int kPreviewPx  = 20;   // specimen size; deliberately NOT the tool's font size
constexpr int kPreviewGap = 12;   // between the specimen and the drop-down list

// Does this family claim to cover Chinese? QFontDatabase::writingSystems() walks the
// font's OS/2 ranges, which is too slow to redo on every hover — hence the cache.
// (Most Japanese families report Chinese too, so they get the Han line as well.)
bool familyHasChinese(const QString& family)
{
    static QHash<QString, bool> cache;
    const auto it = cache.constFind(family);
    if (it != cache.constEnd())
        return it.value();
    const auto ws = QFontDatabase::writingSystems(family);
    const bool cn = ws.contains(QFontDatabase::SimplifiedChinese)
                    || ws.contains(QFontDatabase::TraditionalChinese);
    cache.insert(family, cn);
    return cn;
}
} // namespace

// ===== PaintBar =============================================================
QIcon PaintBar::loadIcon(const char* name)
{
    // Toolbar icons live in <exe>/icons (copied from the project's icons/ folder by
    // CMake post-build). Source of truth: <repo>/icons.
    //
    // SVG, drawn on the same 40 px grid / #333 / 3.5 px round pen as the code-drawn
    // icons below, so they stay sharp at any display scale. QIcon renders them through
    // the qsvgicon icon-engine plugin (iconengines/, deployed by windeployqt) -- without
    // it every toolbar button would come up blank.
    const QString path = QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("icons/%1.svg").arg(QLatin1String(name)));
    return QIcon(path);
}

QFrame* PaintBar::makeSeparator()
{
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setObjectName(QStringLiteral("sep"));
    return sep;
}

QPushButton* PaintBar::makeIconButton(const char* iconName, bool checkable)
{
    auto* b = new QPushButton(this);
    b->setCheckable(checkable);
    b->setIcon(loadIcon(iconName));
    b->setIconSize(QSize(20, 20));
    b->setFixedSize(30, 30);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

bool PaintBar::ocrChecked() const
{
    return m_ocrBtn && m_ocrBtn->isChecked();
}

void PaintBar::setOcrChecked(bool on)
{
    if (!m_ocrBtn || m_ocrBtn->isChecked() == on)
        return;
    m_ocrBtn->blockSignals(true);          // silent: the caller already owns the state change
    m_ocrBtn->setChecked(on);
    m_ocrBtn->blockSignals(false);
}

bool PaintBar::recordChecked() const
{
    return m_recordBtn && m_recordBtn->isChecked();
}

void PaintBar::setRecordChecked(bool on)
{
    if (!m_recordBtn || m_recordBtn->isChecked() == on)
        return;
    m_recordBtn->blockSignals(true);       // silent: the caller already owns the state change
    m_recordBtn->setChecked(on);
    m_recordBtn->blockSignals(false);
}

void PaintBar::setRecordEnabled(bool on, const QString& disabledReason)
{
    if (!m_recordBtn)
        return;
    m_recordBtn->setEnabled(on);
    if (!on && !disabledReason.isEmpty())
        m_recordBtn->setToolTip(disabledReason);
}

void PaintBar::setScrollChecked(bool on)
{
    if (!m_scrollBtn || m_scrollBtn->isChecked() == on)
        return;
    m_scrollBtn->blockSignals(true);       // silent: the caller already owns the state change
    m_scrollBtn->setChecked(on);
    m_scrollBtn->blockSignals(false);
}

void PaintBar::setToolsEnabledForSession(bool on, const QString& reason)
{
    // A scrolling-capture session takes the tools for its duration (the stitched
    // result is far taller than the region, so it cannot be annotated in place). Only
    // CANCEL gives them back, and only because it lands on a fresh capture of the region
    // that can be annotated like any other snip; Done never does.
    if (!on)
        deselectTool();      // never strand the canvas on a tool the toolbar has withdrawn
    for (int g = 0; g < kGroupCount && g < m_groupBtns.size(); ++g) {
        m_groupBtns[g].btn->setEnabled(on);
        m_groupBtns[g].btn->setToolTip(on ? I18n::instance().t(m_groupBtns[g].tipKey)
                                          : reason);
    }
    // Undo/redo have nothing to act on once the document is cleared, and neither
    // neighbouring mode can run inside a scrolling session. The scroll button itself
    // goes too — it stays CHECKED as the mode indicator, but pressing it again must not
    // be able to start a second session on top of the first. No tooltip is parked on
    // these: retranslate() rewrites undo/redo/OCR tooltips unconditionally, so a reason
    // left here would not survive, and the greying already says it.
    //
    // Re-enabling is deliberately blunt: whoever turns these back on owns re-applying any
    // availability the session did NOT take away — a build with no recorder must not gain
    // a Record button by way of Cancel (SnipOverlay re-runs setRecordEnabled()).
    for (QPushButton* b : {m_undo, m_redo, m_ocrBtn, m_recordBtn, m_scrollBtn})
        if (b)
            b->setEnabled(on);
}

void PaintBar::setOutputEnabled(bool on)
{
    // The output block goes inert for the whole scrolling session, so the first-level bar
    // reads as one disabled unit rather than a half-live one offering to copy or save a
    // capture that is still being built. Done and Cancel both give it back. No tooltip is
    // parked here — retranslate() rewrites all four unconditionally.
    for (QPushButton* b : {m_copy, m_save, m_pin, m_cancel})
        if (b)
            b->setEnabled(on);
}

void PaintBar::setRedactEnabled(bool on, const QString& disabledReason)
{
    // Find the redact group by its flag rather than a hardcoded index, so reordering
    // kGroups can't silently disable the wrong tool.
    for (int g = 0; g < kGroupCount && g < m_groupBtns.size(); ++g) {
        if (!kGroups[g].redact)
            continue;
        QPushButton* btn = m_groupBtns[g].btn;
        // Leaving it selected while disabling would strand the canvas on a tool the
        // toolbar no longer offers.
        if (!on && m_group == g)
            deselectTool();
        btn->setEnabled(on);
        btn->setToolTip(on ? I18n::instance().t(m_groupBtns[g].tipKey) : disabledReason);
        return;
    }
}

void PaintBar::clearOcr()
{
    if (!ocrChecked())
        return;
    setOcrChecked(false);
    emit ocrToggled(false);
}

// The two faces of the line tool's mode button — the button is not a switch, it shows
// which mode is active, so these are a matched pair: the same three-bend rhythm drawn
// once as a smooth wave and once as straight segments. Neutral ink, like the other
// code-drawn glyph icons (there is no icons/curve.svg).
QIcon PaintBar::curveIcon()
{
    const int S = 40;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(0x33, 0x33, 0x33));
    pen.setWidthF(3.5);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(4, 22);
    path.cubicTo(8, 4, 13, 4, 16, 20);
    path.cubicTo(19, 36, 24, 36, 27, 20);
    path.cubicTo(30, 8, 34, 8, 37, 18);
    p.drawPath(path);
    p.end();
    return QIcon(pm);
}

QIcon PaintBar::polylineIcon()
{
    const int S = 40;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(0x33, 0x33, 0x33));
    pen.setWidthF(3.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawPolyline(QPolygon() << QPoint(4, 24) << QPoint(12, 7) << QPoint(20, 31)
                              << QPoint(28, 9) << QPoint(37, 21));
    p.end();
    return QIcon(pm);
}

// A viewfinder "A" — scan-corner brackets around a capital A. Custom-drawn because
// there is no icons/ocr.svg (matches the other code-drawn glyph icons here).
QIcon PaintBar::ocrIcon()
{
    const int S = 40;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(0x33, 0x33, 0x33));
    pen.setWidthF(3.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    const int m = 5, a = 9;
    p.drawPolyline(QPolygon() << QPoint(m, m + a)     << QPoint(m, m)         << QPoint(m + a, m));
    p.drawPolyline(QPolygon() << QPoint(S - m - a, m) << QPoint(S - m, m)     << QPoint(S - m, m + a));
    p.drawPolyline(QPolygon() << QPoint(m, S - m - a) << QPoint(m, S - m)     << QPoint(m + a, S - m));
    p.drawPolyline(QPolygon() << QPoint(S - m - a, S - m) << QPoint(S - m, S - m) << QPoint(S - m, S - m - a));
    QFont f = p.font();
    f.setPixelSize(20);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(0, 0, S, S), Qt::AlignCenter, QStringLiteral("A"));
    p.end();
    return QIcon(pm);
}

// Row-1 icon for scrolling capture: a tall page outline with a down arrow through it,
// drawn in the same neutral ink as the other custom icons. There is no icons/*.svg.
QIcon PaintBar::scrollCaptureIcon()
{
    const int S = 40;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(0x33, 0x33, 0x33));
    pen.setWidthF(3.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    // The page: taller than the frame, so it reads as content running past the edge.
    p.drawRect(QRect(8, 4, 24, 32));
    // The arrow: straight down the middle.
    p.drawLine(20, 11, 20, 27);
    p.drawPolyline(QPolygon() << QPoint(14, 21) << QPoint(20, 28) << QPoint(26, 21));
    p.end();
    return QIcon(pm);
}

// Row-1 icon for the number-label group: a ① badge (ring + digit), drawn in the same
// neutral ink as the other custom icons. There is no icons/number_label.svg.
QIcon PaintBar::numberLabelIcon()
{
    const int S = 40;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink(0x33, 0x33, 0x33);
    p.setPen(QPen(ink, 3.0));
    p.setBrush(Qt::NoBrush);
    const qreal m = 5.5;
    p.drawEllipse(QRectF(m, m, S - 2 * m, S - 2 * m));
    QFont f = p.font();
    f.setPixelSize(20);
    f.setBold(true);
    p.setFont(f);
    p.setPen(ink);
    p.drawText(QRect(0, 0, S, S), Qt::AlignCenter, QStringLiteral("1"));
    p.end();
    return QIcon(pm);
}

QColor PaintBar::accent() const
{
    return m_accent.isValid() ? m_accent : kAccent;
}

QString PaintBar::themedBarQss() const
{
    QString q = m_barQss;
    q.replace(QStringLiteral("#2d7dff"), accent().name(), Qt::CaseInsensitive);
    return q;
}

void PaintBar::setSettings(Settings* s)
{
    m_settings = s;
    // The label panel was built in the constructor with the default size; now that a
    // Settings is attached, restore the size the user last chose.
    if (m_settings) {
        const qreal px = m_settings->value(QString::fromLatin1(kLabelSizeKey), m_labelPx).toReal();
        if (px > 0) {
            m_labelPx = px;
            if (m_labelSizeCombo) {
                m_suppressLabelSize = true;
                m_labelSizeCombo->setEditText(fmtFontSize(m_labelPx));
                m_suppressLabelSize = false;
            }
        }
        // Same for the numbering style. Setting the combo index is what updates m_numberStyle
        // (and the badge previews); the guard keeps the restore from restyling the badges of
        // a pin whose editor is being opened.
        if (m_numberCombo) {
            const annotation::NumberStyle ns = annotation::numberStyleFromString(
                m_settings->value(QString::fromLatin1(kLabelNumberingKey)).toString());
            m_suppressNumbering = true;
            m_numberCombo->setCurrentIndex(numberingToIndex(ns));
            m_suppressNumbering = false;
        }
        // Restore the last colour the same way. Going through applyColor() (rather than
        // assigning m_color) is what repaints the picker blocks + badge previews, so the
        // palette opens showing the remembered colour instead of the default red.
        const QColor last(m_settings->value(QString::fromLatin1(kLastColorKey)).toString());
        if (last.isValid())
            applyColor(last);
        // The line tool's straight/curve mode. Restored QUIETLY: selectGroup() re-emits
        // it when the lines group is picked, and both hosts seed Style::curved from
        // currentCurved() right after this call — same contract as the arrow style.
        m_curved = m_settings->value(QString::fromLatin1(kCurvedKey), m_curved).toBool();
        updateCurveBtn();
        // ...and its two end decorations, on the same quiet contract. The combos are
        // loaded under m_suppressCapStore so the restore can't be mistaken for a click.
        const QString sc = m_settings->value(QString::fromLatin1(kStartCapKey)).toString();
        const QString ec = m_settings->value(QString::fromLatin1(kEndCapKey)).toString();
        if (!sc.isEmpty()) m_startCap = annotation::capStyleFromString(sc);
        if (!ec.isEmpty()) m_endCap = annotation::capStyleFromString(ec);
        m_suppressCapStore = true;
        if (m_startCapCombo) m_startCapCombo->setCurrentIndex(capToIndex(m_startCap));
        if (m_endCapCombo)   m_endCapCombo->setCurrentIndex(capToIndex(m_endCap));
        m_suppressCapStore = false;
    }
}

// The mode button is an indicator, not a switch: icon AND tooltip both follow m_curved.
void PaintBar::updateCurveBtn()
{
    if (!m_curveBtn)
        return;
    m_curveBtn->setIcon(m_curved ? curveIcon() : polylineIcon());
    m_curveBtn->setToolTip(I18n::instance().t(m_curved
        ? QStringLiteral("edit.line.mode.curved")
        : QStringLiteral("edit.line.mode.polyline")));
}

PaintBar::PaintBar(QWidget* parent) : QWidget(parent)
{
    // Read the UI accent up front: the whole palette (swatches, picker entries) is built
    // in this constructor, before setSettings(), so accent() must be valid now.
    m_accent = PrefsStyle::accentColor();

    setAutoFillBackground(true);
    // A custom QWidget only paints a stylesheet `background` when WA_StyledBackground
    // is set — otherwise it falls back to the palette colour and the bar looks blank
    // except where the buttons (which honour stylesheets natively) draw on hover.
    setAttribute(Qt::WA_StyledBackground, true);
    // Light bar so the black line-art icons read. The same
    // sheet styles both the main bar (PaintBar) and the separate sub-bar
    // (QWidget#paintSubBar); generic QPushButton/etc. rules apply to each bar's kids.
    m_barQss = QStringLiteral(
        "PaintBar,QWidget#paintSubBar{background:#f2f2f2;border:1px solid #c4c4c4;border-radius:6px;}"
        "QPushButton{background:transparent;border:1px solid transparent;border-radius:4px;padding:2px;}"
        "QPushButton:hover{background:#e3e3e3;}"
        "QPushButton:checked{background:#d4d4d4;border:1px solid #2d7dff;}"
        "QPushButton:disabled{color:#aaa;}"
        "QLabel{color:#333;}"
        "QSpinBox{color:#222;background:#fff;border:1px solid #c4c4c4;border-radius:4px;padding:1px 2px;}"
        "QFrame#sep{color:#cfcfcf;}"
        // The font-family hover specimen (previewFontAt) rides on QToolTip, so this is
        // what gives it the light panel look instead of the system tooltip colours.
        // Square corners on purpose: a border-radius here leaves the corners opaque.
        "QToolTip{background:#ffffff;color:#222222;border:1px solid #c4c4c4;padding:10px 14px;}");
    setStyleSheet(themedBarQss());   // m_settings null here -> default accent; re-skinned by setSettings()

    // ---- Main bar: tool groups | undo redo | cancel pin save copy (single row) ---
    auto* row1 = new QHBoxLayout(this);
    row1->setContentsMargins(6, 6, 6, 6);
    row1->setSpacing(3);

    m_groupGroup = new QButtonGroup(this);
    m_groupGroup->setExclusive(true);
    for (int i = 0; i < kGroupCount; ++i) {
        QPushButton* b = makeIconButton(kGroups[i].icon, /*checkable*/ true);
        if (kGroups[i].label)                     // custom-drawn: no icons/number_label.svg
            b->setIcon(numberLabelIcon());
        row1->addWidget(b);
        m_groupGroup->addButton(b);
        m_groupBtns.push_back({b, QString::fromLatin1(kGroups[i].tip)});
        connect(b, &QPushButton::clicked, this, [this, i] { selectGroup(i); });
    }

    row1->addWidget(makeSeparator());
    m_undo = makeIconButton("undo", false);
    m_redo = makeIconButton("redo", false);
    row1->addWidget(m_undo);
    row1->addWidget(m_redo);
    connect(m_undo, &QPushButton::clicked, this, &PaintBar::undoRequested);
    connect(m_redo, &QPushButton::clicked, this, &PaintBar::redoRequested);

    // OCR mode toggle — not part of the exclusive tool-group set; mutually exclusive
    // with drawing, so turning it on clears any active tool (and selectGroup clears it).
    m_ocrBtn = new QPushButton(this);
    m_ocrBtn->setCheckable(true);
    m_ocrBtn->setIcon(ocrIcon());
    m_ocrBtn->setIconSize(QSize(20, 20));
    m_ocrBtn->setFixedSize(30, 30);
    m_ocrBtn->setCursor(Qt::PointingHandCursor);
    row1->addWidget(m_ocrBtn);
    connect(m_ocrBtn, &QPushButton::toggled, this, [this](bool on) {
        if (on)
            deselectTool();                 // OCR is exclusive with the drawing tools
        emit ocrToggled(on);
    });

    // Scrolling capture — snip host only, immediately right of OCR. Exclusive with the
    // drawing tools like the OCR button, but it claims the whole session: the host greys
    // the tools out on scrollToggled(true) and never re-enables them.
    m_scrollBtn = new QPushButton(this);
    m_scrollBtn->setCheckable(true);
    m_scrollBtn->setIcon(scrollCaptureIcon());
    m_scrollBtn->setIconSize(QSize(20, 20));
    m_scrollBtn->setFixedSize(30, 30);
    m_scrollBtn->setCursor(Qt::PointingHandCursor);
    row1->addWidget(m_scrollBtn);
    connect(m_scrollBtn, &QPushButton::toggled, this, [this](bool on) {
        if (on)
            deselectTool();                 // exclusive with the drawing tools
        emit scrollToggled(on);
    });

    // Record mode toggle — snip host only. UNLIKE the OCR button this is NOT exclusive
    // with the drawing tools: annotating while recording is the point, so selectGroup()
    // deliberately leaves it alone and the host just moves its bar to a third row.
    m_recordBtn = makeIconButton("record", /*checkable*/ true);
    row1->addWidget(m_recordBtn);
    connect(m_recordBtn, &QPushButton::toggled, this, &PaintBar::recordToggled);

    row1->addWidget(makeSeparator());
    m_cancel = makeIconButton("cancel", false);
    m_pin    = makeIconButton("push-pin", false);
    m_save   = makeIconButton("save", false);
    m_copy   = makeIconButton("copy", false);
    row1->addWidget(m_cancel);
    row1->addWidget(m_pin);
    row1->addWidget(m_save);
    row1->addWidget(m_copy);
    connect(m_cancel, &QPushButton::clicked, this, &PaintBar::cancelRequested);
    connect(m_pin,    &QPushButton::clicked, this, &PaintBar::pinRequested);
    connect(m_save,   &QPushButton::clicked, this, &PaintBar::saveRequested);
    connect(m_copy,   &QPushButton::clicked, this, &PaintBar::copyRequested);
    row1->addStretch(1);

    // ---- Sub-bar: a SEPARATE 2nd-level toolbar (generic OR redact panel) ------
    // Its own framed bar; the controller reparents + positions it beneath the main
    // bar and shows it only while a tool group is selected.
    m_subBar = new QWidget(this);
    m_subBar->setObjectName(QStringLiteral("paintSubBar"));
    m_subBar->setAttribute(Qt::WA_StyledBackground, true);
    m_subBar->setStyleSheet(themedBarQss());
    auto* row2 = new QHBoxLayout(m_subBar);
    // Drive the sub-bar's size from its layout so it re-fits to exactly the visible
    // panel on every group switch (otherwise it keeps the widest panel's width and
    // shows empty padding when a narrower tool is selected).
    row2->setSizeConstraint(QLayout::SetFixedSize);
    row2->setContentsMargins(6, 6, 6, 6);
    row2->setSpacing(3);

    // -- generic panel: [hollow|solid] (shapes) · variants · width (others) · palette
    m_genericPanel = new QWidget(m_subBar);
    auto* gp = new QHBoxLayout(m_genericPanel);
    gp->setContentsMargins(0, 0, 0, 0);
    gp->setSpacing(3);

    // Lines-only: straight ↔ smooth-curve mode button, immediately left of the width dot.
    // Deliberately NOT checkable — its icon reports which mode is active, so a pressed
    // state would say the same thing twice (and disagree with itself half the time).
    m_curveBtn = new QPushButton(m_genericPanel);
    m_curveBtn->setCursor(Qt::PointingHandCursor);
    m_curveBtn->setIconSize(QSize(20, 20));
    m_curveBtn->setFixedSize(30, 30);
    gp->addWidget(m_curveBtn);
    updateCurveBtn();
    connect(m_curveBtn, &QPushButton::clicked, this, [this] {
        m_curved = !m_curved;
        updateCurveBtn();
        if (m_settings)
            m_settings->setValue(QString::fromLatin1(kCurvedKey), m_curved);
        emit curvedChanged(m_curved);
    });

    // The scroll-resizable width dot, then (shapes only) the outline/solid fill toggle.
    // Neither is checkable: the dot is a preview + wheel target, not a choice, and the
    // toggle shows its state in its icon rather than a pressed look.
    m_hollowBtn = makeIconButton("point", /*checkable*/ false);
    m_hollowBtn->setParent(m_genericPanel);
    m_fillBtn = makeIconButton("fill_rect_hollow", /*checkable*/ false);
    m_fillBtn->setParent(m_genericPanel);
    gp->addWidget(m_hollowBtn);
    gp->addWidget(m_fillBtn);
    connect(m_fillBtn, &QPushButton::clicked, this, [this] { selectFill(!m_filled); });
    m_hollowBtn->installEventFilter(this);   // wheel over the dot adjusts the width
    m_shapeSep = makeSeparator(); m_shapeSep->setParent(m_genericPanel); gp->addWidget(m_shapeSep);

    // Lines-only, left to right: [start cap] [line style] [end cap]. Icon-only items;
    // native combo styling (the bar sheet only targets QPushButton/QSpinBox/QLabel)
    // gives the chevron + popup.
    auto makeLineCombo = [this](int iconW, int iconH) {
        auto* c = new QComboBox(m_genericPanel);
        c->setIconSize(QSize(iconW, iconH));
        c->setFixedHeight(38);   // matches the picker entry height
        c->setCursor(Qt::PointingHandCursor);
        // Room for the icon PLUS the chevron and frame, or the collapsed control clips it.
        c->setMinimumWidth(iconW + 30);
        // Rows are exactly the icon plus a hairline. ⚠️ No `margin` here: a margin on the
        // item both inflates the row height and shrinks its content box, which clips the
        // icon horizontally — the two symptoms together.
        c->view()->setStyleSheet(
            QStringLiteral("QAbstractItemView::item { height: %1px; padding: 0px; margin: 0px; }")
                .arg(iconH + 4));
        // ...and the popup itself must be at least one item wide, scrollbar included.
        c->view()->setMinimumWidth(iconW + 12
                                   + c->style()->pixelMetric(QStyle::PM_ScrollBarExtent));
        c->setItemDelegate(new CenteredIconDelegate(c));   // icons centred, not left-hugged
        return c;
    };

    m_startCapCombo = makeLineCombo(kCapIconW, kCapIconH);
    for (int i = 0; i < kCapCount; ++i)
        m_startCapCombo->addItem(capIcon(kCaps[i].style, /*atStart*/ true), QString());
    m_startCapCombo->setCurrentIndex(capToIndex(m_startCap));
    gp->addWidget(m_startCapCombo);
    connect(m_startCapCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (i < 0 || i >= kCapCount)
            return;
        m_startCap = kCaps[i].style;
        if (m_suppressCapStore)              // programmatic load (selectGroup): don't echo
            return;
        if (m_settings)
            m_settings->setValue(QString::fromLatin1(kStartCapKey), annotation::capStyleToString(m_startCap));
        emit startCapChanged(m_startCap);
    });

    m_lineDashCombo = makeLineCombo(44, 24);
    for (int i = 0; i < kLineDashCount; ++i)
        m_lineDashCombo->addItem(dashIcon(kDashes[i].style), QString());
    m_lineDashCombo->setCurrentIndex(dashToIndex(m_lineStyle));
    gp->addWidget(m_lineDashCombo);
    connect(m_lineDashCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (i < 0 || i >= kLineDashCount)
            return;
        m_lineStyle = kDashes[i].style;
        if (m_suppressDashStore)             // programmatic load (selectVariant): don't echo
            return;
        storeToolLineStyle(m_tool, m_lineStyle);
        emit lineStyleChanged(m_lineStyle);
    });

    m_endCapCombo = makeLineCombo(kCapIconW, kCapIconH);
    for (int i = 0; i < kCapCount; ++i)
        m_endCapCombo->addItem(capIcon(kCaps[i].style, /*atStart*/ false), QString());
    m_endCapCombo->setCurrentIndex(capToIndex(m_endCap));
    gp->addWidget(m_endCapCombo);
    connect(m_endCapCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (i < 0 || i >= kCapCount)
            return;
        m_endCap = kCaps[i].style;
        if (m_suppressCapStore)
            return;
        if (m_settings)
            m_settings->setValue(QString::fromLatin1(kEndCapKey), annotation::capStyleToString(m_endCap));
        emit endCapChanged(m_endCap);
    });

    m_capSep = makeSeparator(); m_capSep->setParent(m_genericPanel); gp->addWidget(m_capSep);

    m_variantGroup = new QButtonGroup(this);
    m_variantGroup->setExclusive(true);
    for (int g = 0; g < kGroupCount; ++g) {
        if (kGroups[g].count < 2)
            continue;                       // single-variant / redact groups: no sub-row
        for (int v = 0; v < kGroups[g].count; ++v) {
            const VariantDef& vd = kGroups[g].vars[v];
            auto* b = new QPushButton(m_genericPanel);
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            if (vd.icon) {
                b->setIcon(loadIcon(vd.icon));
                b->setIconSize(QSize(20, 20));
                b->setFixedSize(30, 30);
            }
            b->hide();
            gp->addWidget(b);
            m_variantGroup->addButton(b);
            m_variantBtns.push_back({g, vd.id, b, QString::fromLatin1(vd.key), vd.icon != nullptr});
            connect(b, &QPushButton::clicked, this, [this, id = vd.id] { selectVariant(id); });
        }
    }
    m_variantSep = makeSeparator();
    m_variantSep->setParent(m_genericPanel);
    m_variantSep->hide();
    gp->addWidget(m_variantSep);
    m_widthLabel = new QLabel(m_genericPanel);
    gp->addWidget(m_widthLabel);
    m_width = new QSpinBox(m_genericPanel);
    m_width->setRange(1, 50);
    m_width->setValue(3);
    gp->addWidget(m_width);
    connect(m_width, qOverload<int>(&QSpinBox::valueChanged), this, [this](int w) {
        updateHollowIcon();                  // keep the shapes/line dot in sync with the width
        if (m_suppressWidthStore)            // programmatic load (selectVariant): don't echo
            return;
        storeToolWidth(m_tool, w);           // remember this tool's width (persisted)
        emit penWidthChanged(w);
    });
    m_widthSep = makeSeparator(); m_widthSep->setParent(m_genericPanel); gp->addWidget(m_widthSep);

    // Shapes/pencil-only: stroke dash-pattern dropdown, immediately left of the palette.
    // Same icon-only construction as the arrow combo above.
    m_dashCombo = new QComboBox(m_genericPanel);
    m_dashCombo->setIconSize(QSize(44, 24));
    m_dashCombo->setFixedHeight(38);
    m_dashCombo->setCursor(Qt::PointingHandCursor);
    m_dashCombo->view()->setStyleSheet(
        QStringLiteral("QAbstractItemView::item { height: 28px; padding: 0px; margin: 20px; }"));
    for (int i = 0; i < kDashCount; ++i)
        m_dashCombo->addItem(dashIcon(kDashes[i].style), QString());
    m_dashCombo->setCurrentIndex(dashToIndex(m_lineStyle));
    gp->addWidget(m_dashCombo);
    connect(m_dashCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (i < 0 || i >= kDashCount)
            return;
        m_lineStyle = kDashes[i].style;
        if (m_suppressDashStore)             // programmatic load (selectVariant): don't echo
            return;
        storeToolLineStyle(m_tool, m_lineStyle);
        emit lineStyleChanged(m_lineStyle);
    });
    m_dashSep = makeSeparator(); m_dashSep->setParent(m_genericPanel); gp->addWidget(m_dashSep);

    // Shared colour palette: picker-entry block (last-chosen colour / ColorDialog) +
    // a 2×10 grid of the basic colours. Identical on every tool's sub-bar.
    addColorPalette(gp, m_genericPanel);
    gp->addStretch(1);
    row2->addWidget(m_genericPanel);

    // -- redact panel: brush-size / rect-region | type toggle + degree slider --
    m_redactPanel = new QWidget(m_subBar);
    auto* rp = new QHBoxLayout(m_redactPanel);
    rp->setContentsMargins(0, 0, 0, 0);
    rp->setSpacing(3);
    m_redModeGroup = new QButtonGroup(this);
    m_redModeGroup->setExclusive(true);
    m_redBrushBtn = new QPushButton(m_redactPanel);
    m_redBrushBtn->setCheckable(true);
    m_redBrushBtn->setCursor(Qt::PointingHandCursor);
    m_redBrushBtn->setIconSize(QSize(24, 24));
    m_redBrushBtn->setFixedHeight(30);
    m_redBrushBtn->setMinimumWidth(34);
    m_redRectBtn = new QPushButton(m_redactPanel);
    m_redRectBtn->setCheckable(true);
    m_redRectBtn->setCursor(Qt::PointingHandCursor);
    m_redRectBtn->setIcon(QIcon(dottedHRectPixmap(22)));
    m_redRectBtn->setIconSize(QSize(22, 22));
    m_redRectBtn->setFixedSize(30, 30);
    m_redModeGroup->addButton(m_redBrushBtn);
    m_redModeGroup->addButton(m_redRectBtn);
    rp->addWidget(m_redBrushBtn);
    rp->addWidget(m_redRectBtn);
    connect(m_redBrushBtn, &QPushButton::clicked, this, [this] { selectRedactMode(true); });
    connect(m_redRectBtn,  &QPushButton::clicked, this, [this] { selectRedactMode(false); });

    rp->addWidget(makeSeparator());
    m_redTypeBtn = new QPushButton(m_redactPanel);
    m_redTypeBtn->setCursor(Qt::PointingHandCursor);
    m_redTypeBtn->setIconSize(QSize(24, 24));
    m_redTypeBtn->setFixedSize(32, 30);
    rp->addWidget(m_redTypeBtn);
    connect(m_redTypeBtn, &QPushButton::clicked, this, [this] { setRedactType(!m_redactBlur); });

    m_redSlider = new RedactSlider(m_redactPanel);
    m_redSlider->setAccent(accent());
    m_redSlider->setRange(1, 20);
    m_redSlider->setValue(m_redactStrength);
    rp->addWidget(m_redSlider, /*stretch*/ 1);
    connect(m_redSlider, &RedactSlider::valueChanged, this, [this](int v) {
        m_redactStrength = v;
        refreshRedactPreview();
        emit redactStrengthChanged(v);
    });
    m_redValueLabel = new QLabel(m_redactPanel);
    m_redValueLabel->setMinimumWidth(18);
    rp->addWidget(m_redValueLabel);

    // Wheel over brush-size / type buttons adjusts them (per the spec).
    m_redBrushBtn->installEventFilter(this);
    m_redTypeBtn->installEventFilter(this);

    row2->addWidget(m_redactPanel);

    // -- eraser panel: circle brush (dot/number + scroll-resize) | rectangle region --
    m_eraserPanel = new QWidget(m_subBar);
    auto* ep = new QHBoxLayout(m_eraserPanel);
    ep->setContentsMargins(0, 0, 0, 0);
    ep->setSpacing(3);
    m_eraseModeGroup = new QButtonGroup(this);
    m_eraseModeGroup->setExclusive(true);
    m_eraseCircleBtn = new QPushButton(m_eraserPanel);
    m_eraseCircleBtn->setCheckable(true);
    m_eraseCircleBtn->setCursor(Qt::PointingHandCursor);
    m_eraseCircleBtn->setIconSize(QSize(24, 24));
    m_eraseCircleBtn->setFixedHeight(30);
    m_eraseCircleBtn->setMinimumWidth(34);
    m_eraseRectBtn = new QPushButton(m_eraserPanel);
    m_eraseRectBtn->setCheckable(true);
    m_eraseRectBtn->setCursor(Qt::PointingHandCursor);
    m_eraseRectBtn->setIcon(QIcon(dottedRectPixmap(22)));
    m_eraseRectBtn->setIconSize(QSize(22, 22));
    m_eraseRectBtn->setFixedSize(30, 30);
    m_eraseModeGroup->addButton(m_eraseCircleBtn);
    m_eraseModeGroup->addButton(m_eraseRectBtn);
    ep->addWidget(m_eraseCircleBtn);
    ep->addWidget(m_eraseRectBtn);
    ep->addStretch(1);
    connect(m_eraseCircleBtn, &QPushButton::clicked, this, [this] { selectEraserMode(false); });
    connect(m_eraseRectBtn,   &QPushButton::clicked, this, [this] { selectEraserMode(true); });
    m_eraseCircleBtn->installEventFilter(this);   // wheel over the dot resizes the circle
    row2->addWidget(m_eraserPanel);

    // -- marker panel: freehand highlighter (width dot) | rectangle region | palette --
    m_markerPanel = new QWidget(m_subBar);
    auto* mp = new QHBoxLayout(m_markerPanel);
    mp->setContentsMargins(0, 0, 0, 0);
    mp->setSpacing(3);
    m_markModeGroup = new QButtonGroup(this);
    m_markModeGroup->setExclusive(true);
    m_markDotBtn = new QPushButton(m_markerPanel);
    m_markDotBtn->setCheckable(true);
    m_markDotBtn->setCursor(Qt::PointingHandCursor);
    m_markDotBtn->setIconSize(QSize(20, 20));
    m_markDotBtn->setFixedHeight(30);
    m_markDotBtn->setMinimumWidth(34);
    m_markRectBtn = new QPushButton(m_markerPanel);
    m_markRectBtn->setCheckable(true);
    m_markRectBtn->setCursor(Qt::PointingHandCursor);
    m_markRectBtn->setIcon(QIcon(markerRectPixmap(22)));
    m_markRectBtn->setIconSize(QSize(22, 22));
    m_markRectBtn->setFixedSize(30, 30);
    m_markModeGroup->addButton(m_markDotBtn);
    m_markModeGroup->addButton(m_markRectBtn);
    mp->addWidget(m_markDotBtn);
    mp->addWidget(m_markRectBtn);
    mp->addWidget(makeSeparator());
    connect(m_markDotBtn,  &QPushButton::clicked, this, [this] { selectMarkerMode(false); });
    connect(m_markRectBtn, &QPushButton::clicked, this, [this] { selectMarkerMode(true); });
    m_markDotBtn->installEventFilter(this);        // wheel over the dot resizes the width
    addColorPalette(mp, m_markerPanel);
    mp->addStretch(1);
    row2->addWidget(m_markerPanel);

    // -- text panel: bold | italic | hollow | family | size | palette --
    buildTextPanel(row2);

    // -- number-label panel: filled | ringed | hollow | size | palette --
    buildLabelPanel(row2);

    // The whole sub-bar stays hidden until a tool group is selected.
    m_genericPanel->hide();
    m_redactPanel->hide();
    m_eraserPanel->hide();
    m_markerPanel->hide();
    m_textPanel->hide();
    m_labelPanel->hide();
    m_subBar->hide();

    applyColor(m_color);                    // paints the picker-entry block(s) + label icons
    updateHollowIcon();                     // initial width dot
    updateEraserCircleIcon();               // initial eraser dot
    updateMarkerDotIcon();                  // initial marker dot
    updateRedactBrushIcon();                // initial redact brush dot
    refreshRedactPreview();                 // initial type icon + slider handle + label
    retranslate();
}

int PaintBar::penWidth() const
{
    return m_width ? m_width->value() : 3;
}

int PaintBar::toolWidth(ToolId id) const
{
    const int def = defaultWidthFor(id);
    if (m_settings)                          // persisted value wins (survives restarts)
        return m_settings->value(widthKey(id), def).toInt();
    return m_toolWidth.value(int(id), def);  // no Settings (tests): in-memory only
}

void PaintBar::storeToolWidth(ToolId id, int w)
{
    m_toolWidth.insert(int(id), w);
    if (m_settings)
        m_settings->setValue(widthKey(id), w);
}

annotation::LineStyle PaintBar::toolLineStyle(ToolId id) const
{
    if (m_settings)                          // persisted value wins (survives restarts)
        return annotation::lineStyleFromString(
            m_settings->value(lineStyleKey(id)).toString());
    return annotation::LineStyle(            // no Settings (tests): in-memory only
        m_toolLineStyle.value(int(id), int(annotation::LineStyle::Solid)));
}

void PaintBar::storeToolLineStyle(ToolId id, annotation::LineStyle s)
{
    m_toolLineStyle.insert(int(id), int(s));
    if (m_settings)
        m_settings->setValue(lineStyleKey(id), annotation::lineStyleToString(s));
}

void PaintBar::selectGroup(int g)
{
    if (g < 0 || g >= kGroupCount)
        return;
    if (g == m_group) {            // re-click the active group → cancel it (read-only)
        deselectTool();
        return;
    }
    clearOcr();                    // selecting a drawing tool leaves OCR mode
    m_group = g;
    m_groupBtns[g].btn->setChecked(true);

    if (kGroups[g].text) {
        selectTextGroup();
        return;
    }

    if (kGroups[g].label) {
        selectLabelGroup();
        return;
    }

    if (kGroups[g].redact) {
        m_genericPanel->hide();
        m_eraserPanel->hide();
        m_markerPanel->hide();
        m_textPanel->hide();
        m_labelPanel->hide();
        m_redactPanel->show();
        // Ensure a mode button reflects the current mode.
        (m_redactBrush ? m_redBrushBtn : m_redRectBtn)->setChecked(true);
        refreshRedactPreview();
        emitRedactTool();
        return;
    }

    if (kGroups[g].eraser) {
        m_genericPanel->hide();
        m_redactPanel->hide();
        m_markerPanel->hide();
        m_textPanel->hide();
        m_labelPanel->hide();
        m_eraserPanel->show();
        m_eraserSize = toolWidth(ToolId::Eraser);   // restore the persisted circle size
        updateEraserCircleIcon();
        (m_eraserRect ? m_eraseRectBtn : m_eraseCircleBtn)->setChecked(true);
        emitEraserTool();
        return;
    }

    if (kGroups[g].marker) {
        m_genericPanel->hide();
        m_redactPanel->hide();
        m_eraserPanel->hide();
        m_textPanel->hide();
        m_labelPanel->hide();
        m_markerPanel->show();
        m_markerWidth = toolWidth(ToolId::Marker);   // restore the persisted freehand width
        updateMarkerDotIcon();
        (m_markerRect ? m_markRectBtn : m_markDotBtn)->setChecked(true);
        emitMarkerTool();
        return;
    }

    m_redactPanel->hide();
    m_eraserPanel->hide();
    m_markerPanel->hide();
    m_textPanel->hide();
    m_labelPanel->hide();
    m_genericPanel->show();

    // The width dot (hollow button) is the width control for shapes AND lines; every
    // other tool uses the width spin box instead. Shapes pair the dot with the fill
    // toggle; lines pair it with the arrow-style dropdown.
    const bool fillable = kGroups[g].fillable;
    const bool arrowy   = kGroups[g].arrowy;
    const bool useDot   = fillable || arrowy || kGroups[g].widthDot;
    m_curveBtn->setVisible(arrowy);
    m_hollowBtn->setVisible(useDot);
    m_fillBtn->setVisible(fillable);
    m_shapeSep->setVisible(useDot);
    m_startCapCombo->setVisible(arrowy);
    m_lineDashCombo->setVisible(arrowy);
    m_endCapCombo->setVisible(arrowy);
    m_capSep->setVisible(arrowy);
    showDashCombo(g);
    m_widthLabel->setVisible(!useDot);
    m_width->setVisible(!useDot);
    m_widthSep->setVisible(!useDot);
    if (useDot)
        updateHollowIcon();
    updateFillToggle();
    if (fillable)
        emit fillSelected(m_filled);
    if (arrowy) {
        // Load the combos without echoing back through their handlers, then emit once
        // each so a host that never saw a click still gets the current values.
        m_suppressCapStore = true;
        m_startCapCombo->setCurrentIndex(capToIndex(m_startCap));
        m_endCapCombo->setCurrentIndex(capToIndex(m_endCap));
        m_suppressCapStore = false;
        emit startCapChanged(m_startCap);
        emit endCapChanged(m_endCap);
        emit curvedChanged(m_curved);
    }

    // Show only this group's variant buttons.
    bool anyVar = false;
    ToolId firstVar = ToolId::None;
    for (const VariantBtn& vb : m_variantBtns) {
        const bool mine = vb.group == g;
        vb.btn->setVisible(mine);
        if (mine) {
            anyVar = true;
            if (firstVar == ToolId::None)
                firstVar = vb.id;
        }
    }
    if (m_variantSep)
        m_variantSep->setVisible(anyVar);

    ToolId chosen = anyVar ? firstVar : kGroups[g].vars[0].id;
    if (anyVar) {
        for (const VariantBtn& vb : m_variantBtns)
            if (vb.group == g && vb.id == m_tool) { chosen = m_tool; break; }
    }
    selectVariant(chosen);
}

void PaintBar::deselectTool()
{
    m_group = -1;
    m_tool = ToolId::None;
    // Uncheck the (exclusive) group + variant buttons. Exclusive groups won't let the
    // checked button be cleared by a click, so flip exclusivity off to uncheck.
    m_groupGroup->setExclusive(false);
    for (const GroupBtn& gb : m_groupBtns)
        gb.btn->setChecked(false);
    m_groupGroup->setExclusive(true);
    if (m_variantGroup) {
        m_variantGroup->setExclusive(false);
        for (const VariantBtn& vb : m_variantBtns)
            vb.btn->setChecked(false);
        m_variantGroup->setExclusive(true);
    }
    m_genericPanel->hide();
    m_redactPanel->hide();         // hide the whole of row 2
    m_eraserPanel->hide();
    m_markerPanel->hide();
    m_textPanel->hide();
    m_labelPanel->hide();
    emit toolSelected(ToolId::None);
}

void PaintBar::selectVariant(ToolId id)
{
    m_tool = id;
    for (const VariantBtn& vb : m_variantBtns)
        if (vb.id == id)
            vb.btn->setChecked(true);
    // Load this tool's own remembered width into the control (dot or spin box). Suppress
    // the store/echo so loading isn't mistaken for a user edit; emit it explicitly below.
    const int w = toolWidth(id);
    m_suppressWidthStore = true;
    m_width->setValue(w);
    m_suppressWidthStore = false;
    updateHollowIcon();
    updateFillToggle();                       // square <-> circle follows the variant
    // Same for the stroke style, on the groups that have a dropdown for it — the shapes/
    // pencil one (m_dashCombo) or the line tool's own wider one (m_lineDashCombo). Tools
    // without either always stroke solid, so there is nothing to load or announce.
    const bool inGroup  = m_group >= 0 && m_group < kGroupCount;
    const bool dashable = inGroup && kGroups[m_group].dashable;
    const bool lineDash = inGroup && kGroups[m_group].arrowy;
    if ((dashable || lineDash)) {
        m_lineStyle = toolLineStyle(id);
        m_suppressDashStore = true;
        if (dashable && m_dashCombo)
            m_dashCombo->setCurrentIndex(dashToIndex(m_lineStyle));
        if (lineDash && m_lineDashCombo)
            m_lineDashCombo->setCurrentIndex(dashToIndex(m_lineStyle));
        m_suppressDashStore = false;
    }
    emit toolSelected(id);
    emit penWidthChanged(w);                  // apply this tool's pen width
    if (dashable || lineDash)
        emit lineStyleChanged(m_lineStyle);   // ...and its stroke style
}

// ---- shapes: hollow/solid + width dot --------------------------------------
void PaintBar::selectFill(bool filled)
{
    m_filled = filled;
    updateFillToggle();
    showDashCombo(m_group);        // a filled shape has no stroke to dash
    emit fillSelected(filled);
}

void PaintBar::updateFillToggle()
{
    if (!m_fillBtn || !m_hollowBtn)
        return;
    const bool ellipse = m_tool == ToolId::Ellipse;
    m_fillBtn->setIcon(loadIcon(ellipse ? (m_filled ? "fill_ellipse_filled" : "fill_ellipse_hollow")
                                        : (m_filled ? "fill_rect_filled" : "fill_rect_hollow")));
    // The tooltip names the current mode and what a click does, since the face alone
    // shows only the former.
    const I18n& tr = I18n::instance();
    m_fillBtn->setToolTip(tr.t(m_filled ? QStringLiteral("edit.shape.filledToggle")
                                        : QStringLiteral("edit.shape.outlineToggle")));
    // A filled shape has no outline, so its width means nothing: grey the dot (which
    // also stops the wheel reaching it) rather than let it change an unused value.
    const bool fillable = m_group >= 0 && m_group < kGroupCount && kGroups[m_group].fillable;
    m_hollowBtn->setEnabled(!(fillable && m_filled));
}

void PaintBar::showDashCombo(int group)
{
    if (!m_dashCombo)
        return;
    const bool on = group >= 0 && group < kGroupCount && kGroups[group].dashable
                 && !(kGroups[group].fillable && m_filled);
    m_dashCombo->setVisible(on);
    m_dashSep->setVisible(on);
}

void PaintBar::updateHollowIcon()
{
    if (!m_hollowBtn)
        return;
    // A centred black dot whose diameter previews the current pen width, so scrolling
    // the wheel visibly grows/shrinks it. Once the dot would overflow the icon box,
    // show the number instead (same behaviour as the circle eraser's dot).
    const int box = 20;
    const int w = m_width ? m_width->value() : 3;
    if (w <= box) {
        QPixmap pm(box, box);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x33, 0x33, 0x33));
        const int d = qMax(2, w);
        p.drawEllipse(QRectF((box - d) / 2.0, (box - d) / 2.0, d, d));
        p.end();
        m_hollowBtn->setIcon(QIcon(pm));
        m_hollowBtn->setText(QString());
    } else {
        m_hollowBtn->setIcon(QIcon());
        m_hollowBtn->setText(QString::number(w));
    }
}

// ---- redact panel ----------------------------------------------------------
void PaintBar::selectRedactMode(bool brush)
{
    m_redactBrush = brush;
    (brush ? m_redBrushBtn : m_redRectBtn)->setChecked(true);
    emitRedactTool();
}

void PaintBar::setRedactType(bool blur)
{
    m_redactBlur = blur;
    refreshRedactPreview();
    emitRedactTool();
}

void PaintBar::adjustRedactBrush(int step)
{
    m_redBrushSize = qBound(2, m_redBrushSize + step, 100);
    updateRedactBrushIcon();
    if (!m_redactBrush) selectRedactMode(true);   // scrolling the size implies brush mode
    else                emitRedactTool();
}

void PaintBar::emitRedactTool()
{
    m_tool = m_redactBlur ? (m_redactBrush ? ToolId::BlurBrush   : ToolId::Blur)
                          : (m_redactBrush ? ToolId::MosaicBrush : ToolId::Mosaic);
    emit toolSelected(m_tool);
    emit penWidthChanged(m_redBrushSize);         // brush diameter (used in brush mode)
    emit redactStrengthChanged(m_redactStrength);
}

void PaintBar::refreshRedactPreview()
{
    if (m_redTypeBtn)
        m_redTypeBtn->setIcon(QIcon(QPixmap::fromImage(
            redactPreview(m_redactBlur, m_redactStrength, QSize(24, 24), accent()))));
    if (m_redSlider)
        m_redSlider->setSquareHandle(!m_redactBlur);   // mosaic = square handle
    if (m_redValueLabel)
        m_redValueLabel->setText(QString::number(m_redactStrength));
}

void PaintBar::updateRedactBrushIcon()
{
    if (!m_redBrushBtn)
        return;
    // A centred dot whose diameter previews the brush size — same convention as the
    // circle eraser / pen width dot. Once the dot would overflow the icon box, show the
    // number instead.
    const int box = 24;
    if (m_redBrushSize <= box) {
        QPixmap pm(box, box);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x33, 0x33, 0x33));
        const int d = qMax(3, m_redBrushSize);
        p.drawEllipse(QRectF((box - d) / 2.0, (box - d) / 2.0, d, d));
        p.end();
        m_redBrushBtn->setIcon(QIcon(pm));
        m_redBrushBtn->setText(QString());
    } else {
        m_redBrushBtn->setIcon(QIcon());
        m_redBrushBtn->setText(QString::number(m_redBrushSize));
    }
}

// ---- eraser panel ----------------------------------------------------------
void PaintBar::selectEraserMode(bool rect)
{
    m_eraserRect = rect;
    (rect ? m_eraseRectBtn : m_eraseCircleBtn)->setChecked(true);
    emitEraserTool();
}

void PaintBar::adjustEraserSize(int step)
{
    m_eraserSize = qBound(4, m_eraserSize + step * 2, 200);   // 2px per wheel notch
    storeToolWidth(ToolId::Eraser, m_eraserSize);             // persist (shared width key)
    updateEraserCircleIcon();
    if (m_eraserRect) selectEraserMode(false);   // scrolling the size implies circle mode
    else              emitEraserTool();
}

void PaintBar::emitEraserTool()
{
    m_tool = m_eraserRect ? ToolId::EraserRect : ToolId::Eraser;
    emit toolSelected(m_tool);
    emit penWidthChanged(m_eraserSize);          // circle: brush diameter (rect ignores it)
}

void PaintBar::wheelAdjustWidth(int steps)
{
    switch (m_tool) {
        case ToolId::Eraser:                 // circle eraser: resize the brush
            adjustEraserSize(steps);
            break;
        case ToolId::Marker:                 // freehand highlighter: resize the width
            adjustMarkerWidth(steps);
            break;
        case ToolId::MosaicBrush: case ToolId::BlurBrush:   // redact brush: resize the brush
            adjustRedactBrush(steps);
            break;
        case ToolId::NumberLabel:            // number label: resize the badge + its digit
            adjustLabelSize(steps);
            break;
        case ToolId::EraserRect:             // rect eraser/marker / region redact / text: no scroll width
        case ToolId::MarkerRect:
        case ToolId::Mosaic:    case ToolId::Blur:
        case ToolId::Text:      case ToolId::None:
            break;
        default:                             // rect/ellipse/line/arrow/pencil
            if (m_width)                     // → updateHollowIcon + persist + penWidthChanged
                m_width->setValue(m_width->value() + steps);
            break;
    }
}

void PaintBar::updateEraserCircleIcon()
{
    if (!m_eraseCircleBtn)
        return;
    // A centred dot whose diameter previews the eraser size. Once the dot would
    // overflow the icon box, show the number instead.
    const int box = 24;
    if (m_eraserSize <= box) {
        QPixmap pm(box, box);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x33, 0x33, 0x33));
        const int d = qMax(3, m_eraserSize);
        p.drawEllipse(QRectF((box - d) / 2.0, (box - d) / 2.0, d, d));
        p.end();
        m_eraseCircleBtn->setIcon(QIcon(pm));
        m_eraseCircleBtn->setText(QString());
    } else {
        m_eraseCircleBtn->setIcon(QIcon());
        m_eraseCircleBtn->setText(QString::number(m_eraserSize));
    }
}

// ---- marker (highlighter) panel --------------------------------------------
void PaintBar::selectMarkerMode(bool rect)
{
    m_markerRect = rect;
    (rect ? m_markRectBtn : m_markDotBtn)->setChecked(true);
    emitMarkerTool();
}

void PaintBar::adjustMarkerWidth(int step)
{
    m_markerWidth = qBound(1, m_markerWidth + step, 50);
    storeToolWidth(ToolId::Marker, m_markerWidth);   // persist (shared marker width key)
    updateMarkerDotIcon();
    if (m_markerRect) selectMarkerMode(false);   // scrolling the width implies freehand mode
    else              emitMarkerTool();
}

void PaintBar::emitMarkerTool()
{
    m_tool = m_markerRect ? ToolId::MarkerRect : ToolId::Marker;
    emit toolSelected(m_tool);
    emit penWidthChanged(m_markerWidth);         // freehand base width (rect ignores it)
}

void PaintBar::updateMarkerDotIcon()
{
    if (!m_markDotBtn)
        return;
    // A centred dot previewing the freehand width; once it would overflow the icon
    // box, show the number instead (same behaviour as the pencil/eraser dots).
    const int box = 20;
    if (m_markerWidth <= box) {
        QPixmap pm(box, box);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x33, 0x33, 0x33));
        const int d = qMax(2, m_markerWidth);
        p.drawEllipse(QRectF((box - d) / 2.0, (box - d) / 2.0, d, d));
        p.end();
        m_markDotBtn->setIcon(QIcon(pm));
        m_markDotBtn->setText(QString());
    } else {
        m_markDotBtn->setIcon(QIcon());
        m_markDotBtn->setText(QString::number(m_markerWidth));
    }
}

// ---- text panel ------------------------------------------------------------
void PaintBar::buildTextPanel(QHBoxLayout* row2)
{
    m_textPanel = new QWidget(m_subBar);
    auto* tp = new QHBoxLayout(m_textPanel);
    tp->setContentsMargins(0, 0, 0, 0);
    tp->setSpacing(3);

    auto makeToggle = [this](const QPixmap& pm) {
        auto* b = new QPushButton(m_textPanel);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setIcon(QIcon(pm));
        b->setIconSize(QSize(22, 22));
        b->setFixedSize(30, 30);
        return b;
    };
    m_textBoldBtn   = makeToggle(glyphIcon(QStringLiteral("B"), true, false, 22));
    m_textItalicBtn = makeToggle(italicSerifIcon(22));
    m_textHollowBtn = makeToggle(hollowAIcon(22));
    tp->addWidget(m_textBoldBtn);
    tp->addWidget(m_textItalicBtn);
    tp->addWidget(m_textHollowBtn);
    connect(m_textBoldBtn,   &QPushButton::toggled, this, [this](bool on) { m_textBold = on;   emit boldChanged(on); });
    connect(m_textItalicBtn, &QPushButton::toggled, this, [this](bool on) { m_textItalic = on; emit italicChanged(on); });
    connect(m_textHollowBtn, &QPushButton::toggled, this, [this](bool on) { m_textHollow = on; emit hollowTextChanged(on); });

    tp->addWidget(makeSeparator());

    // Font family: every installed family, defaulting to Consolas when present.
    m_fontFamilyCombo = new WideMenuCombo(m_textPanel);
    m_fontFamilyCombo->setCursor(Qt::PointingHandCursor);
    m_fontFamilyCombo->setFixedHeight(28);
    m_fontFamilyCombo->setFixedWidth(124);              // compact; the drop-down list widens
    m_fontFamilyCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fontFamilyCombo->addItems(QFontDatabase::families());
    const int famIdx = m_fontFamilyCombo->findText(m_fontFamily);
    if (famIdx >= 0) m_fontFamilyCombo->setCurrentIndex(famIdx);
    else             m_fontFamily = m_fontFamilyCombo->currentText();
    tp->addWidget(m_fontFamilyCombo);
    connect(m_fontFamilyCombo, &QComboBox::currentTextChanged, this, [this](const QString& f) {
        m_fontFamily = f;
        emit fontFamilyChanged(f);
    });

    // Hover preview: a specimen of the hovered family, beside the open drop-down, so a
    // typeface can be judged without selecting it. Driven off the VIEWPORT's hover/move
    // events, not QAbstractItemView::entered — that signal comes out of the view's
    // mouseMoveEvent, which needs WA_MouseTracking on the *viewport* (setting it on the
    // view does nothing, since the view's mouse events are delivered to its viewport
    // child). Filtering the viewport is what the combo's own row highlight does.
    QAbstractItemView* famView = m_fontFamilyCombo->view();
    famView->viewport()->setMouseTracking(true);
    famView->viewport()->installEventFilter(this);   // hover moves + Leave
    famView->installEventFilter(this);               // Hide = the drop-down closed

    // Font size: an editable combo (presets + free entry); always an integer.
    m_fontSizeCombo = new QComboBox(m_textPanel);
    m_fontSizeCombo->setEditable(true);
    m_fontSizeCombo->setInsertPolicy(QComboBox::NoInsert);
    m_fontSizeCombo->setCursor(Qt::PointingHandCursor);
    m_fontSizeCombo->setFixedHeight(28);
    m_fontSizeCombo->setFixedWidth(56);
    for (int s : {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72})
        m_fontSizeCombo->addItem(QString::number(s));
    m_fontSizeCombo->setValidator(new QIntValidator(1, 999, m_fontSizeCombo));
    m_fontSizeCombo->setEditText(fmtFontSize(m_fontPx));
    tp->addWidget(m_fontSizeCombo);
    connect(m_fontSizeCombo, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        if (m_suppressFontSize) return;
        bool ok = false;
        const int v = t.toInt(&ok);
        if (ok && v > 0) { m_fontPx = v; emit fontSizeChanged(v); }
    });

    tp->addWidget(makeSeparator());
    addColorPalette(tp, m_textPanel);
    tp->addStretch(1);
    row2->addWidget(m_textPanel);
}

void PaintBar::selectTextGroup()
{
    m_genericPanel->hide();
    m_redactPanel->hide();
    m_eraserPanel->hide();
    m_markerPanel->hide();
    m_textPanel->show();
    m_tool = ToolId::Text;
    emit toolSelected(ToolId::Text);
    // Seed the overlay's live text style from the current control states.
    emit boldChanged(m_textBold);
    emit italicChanged(m_textItalic);
    emit hollowTextChanged(m_textHollow);
    emit fontFamilyChanged(m_fontFamily);
    emit fontSizeChanged(m_fontPx);
}

// ---- number-label panel ----------------------------------------------------
void PaintBar::buildLabelPanel(QHBoxLayout* row2)
{
    m_labelPanel = new QWidget(m_subBar);
    auto* lp = new QHBoxLayout(m_labelPanel);
    lp->setContentsMargins(0, 0, 0, 0);
    lp->setSpacing(3);

    // The three badge looks, in spec order. Their icons are real badges painted by the
    // annotation renderer (updateLabelIcons), so the button previews what you'll stamp.
    m_labelStyleGroup = new QButtonGroup(this);
    m_labelStyleGroup->setExclusive(true);
    const annotation::LabelStyle styles[] = {annotation::LabelStyle::Filled,
                                             annotation::LabelStyle::RingedFilled,
                                             annotation::LabelStyle::Hollow};
    for (annotation::LabelStyle s : styles) {
        auto* b = new QPushButton(m_labelPanel);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setIconSize(QSize(24, 24));
        b->setFixedSize(30, 30);
        m_labelStyleGroup->addButton(b);
        lp->addWidget(b);
        m_labelBtns.push_back(b);
        connect(b, &QPushButton::clicked, this, [this, s] { selectLabelStyle(s); });
    }
    m_labelBtns[int(m_labelStyle)]->setChecked(true);

    lp->addWidget(makeSeparator());

    // Numbering style: which glyphs the badges count in. Unlike every other control on
    // this bar it applies to the badges ALREADY stamped as well as the next one — the
    // controllers restyle the whole set in one undo step (see selectLabelGroup's note).
    m_numberCombo = new QComboBox(m_labelPanel);
    m_numberCombo->setCursor(Qt::PointingHandCursor);
    m_numberCombo->setFixedHeight(28);
    for (int i = 0; i < kNumberingCount; ++i)
        m_numberCombo->addItem(QString::fromLatin1(kNumberings[i].sample));
    m_numberCombo->setCurrentIndex(numberingToIndex(m_numberStyle));
    lp->addWidget(m_numberCombo);
    connect(m_numberCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (i < 0 || i >= kNumberingCount)
            return;
        m_numberStyle = kNumberings[i].style;
        updateLabelIcons();               // the look buttons preview A / I / i too
        if (m_suppressNumbering)          // programmatic load (setSettings): don't echo
            return;
        if (m_settings)
            m_settings->setValue(QString::fromLatin1(kLabelNumberingKey),
                                 annotation::numberStyleToString(m_numberStyle));
        emit numberStyleChanged(m_numberStyle);
    });

    lp->addWidget(makeSeparator());

    // Badge size: an editable combo (presets + free entry), mirroring the text tool's
    // size box. The canvas wheel nudges this same value.
    m_labelSizeCombo = new QComboBox(m_labelPanel);
    m_labelSizeCombo->setEditable(true);
    m_labelSizeCombo->setInsertPolicy(QComboBox::NoInsert);
    m_labelSizeCombo->setCursor(Qt::PointingHandCursor);
    m_labelSizeCombo->setFixedHeight(28);
    m_labelSizeCombo->setFixedWidth(56);
    for (int s : {8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48})
        m_labelSizeCombo->addItem(QString::number(s));
    m_labelSizeCombo->setValidator(new QIntValidator(1, 999, m_labelSizeCombo));
    m_labelSizeCombo->setEditText(fmtFontSize(m_labelPx));
    lp->addWidget(m_labelSizeCombo);
    connect(m_labelSizeCombo, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        if (m_suppressLabelSize) return;
        bool ok = false;
        const int v = t.toInt(&ok);
        if (ok && v > 0) {
            m_labelPx = v;
            storeLabelPx();
            updateLabelIcons();
            emit fontSizeChanged(v);      // the badge size IS the digit's font size
        }
    });

    lp->addWidget(makeSeparator());
    addColorPalette(lp, m_labelPanel);
    lp->addStretch(1);
    row2->addWidget(m_labelPanel);
}

void PaintBar::selectLabelGroup()
{
    m_genericPanel->hide();
    m_redactPanel->hide();
    m_eraserPanel->hide();
    m_markerPanel->hide();
    m_textPanel->hide();
    m_labelPanel->show();
    m_labelBtns[int(m_labelStyle)]->setChecked(true);
    updateLabelIcons();
    m_tool = ToolId::NumberLabel;
    emit toolSelected(ToolId::NumberLabel);
    // Seed the overlay's live style from the current control states. numberStyleChanged is
    // deliberately NOT re-emitted here: it restyles every badge already on the canvas (and
    // pushes an undo step), so merely picking the tool must not fire it. The hosts seed
    // their style from currentNumberStyle() when they wire the bar up instead.
    emit labelStyleChanged(m_labelStyle);
    emit fontSizeChanged(m_labelPx);
}

void PaintBar::setNumberStyleValue(annotation::NumberStyle s)
{
    if (!m_numberCombo || m_numberStyle == s)
        return;
    // Setting the index is what updates m_numberStyle + the badge previews; the guard stops
    // it echoing back out as a user pick. Deliberately NOT persisted: the remembered value
    // is the last style the user *chose*, and an undo is a canvas operation, not a choice.
    m_suppressNumbering = true;
    m_numberCombo->setCurrentIndex(numberingToIndex(s));
    m_suppressNumbering = false;
}

void PaintBar::selectLabelStyle(annotation::LabelStyle s)
{
    m_labelStyle = s;
    m_labelBtns[int(s)]->setChecked(true);
    emit labelStyleChanged(s);
}

void PaintBar::adjustLabelSize(int steps)
{
    const qreal next = qBound(4.0, m_labelPx + steps, 200.0);
    if (qFuzzyCompare(next, m_labelPx))
        return;
    m_labelPx = next;
    if (m_labelSizeCombo) {
        m_suppressLabelSize = true;       // set the box, then emit once below
        m_labelSizeCombo->setEditText(fmtFontSize(m_labelPx));
        m_suppressLabelSize = false;
    }
    storeLabelPx();
    updateLabelIcons();
    emit fontSizeChanged(m_labelPx);
}

void PaintBar::storeLabelPx()
{
    if (m_settings)
        m_settings->setValue(QString::fromLatin1(kLabelSizeKey), m_labelPx);
}

void PaintBar::updateLabelIcons()
{
    if (m_labelBtns.isEmpty())
        return;
    // Draw each button's icon with the real badge renderer at a fixed icon-sized
    // scale, in the currently selected colour — so the toolbar previews the look, not
    // the size (the size box already shows that).
    const int box = 24;
    const annotation::LabelStyle styles[] = {annotation::LabelStyle::Filled,
                                             annotation::LabelStyle::RingedFilled,
                                             annotation::LabelStyle::Hollow};
    for (int i = 0; i < m_labelBtns.size(); ++i) {
        annotation::Style st;
        st.color = m_color;
        st.label = styles[i];
        st.numberStyle = m_numberStyle;           // preview the numbering in use (1 / A / I)
        st.fontPx = box / 2.0;                    // → a badge exactly `box` px across
        QPixmap pm(box, box);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        // Always "1": the buttons differ by LOOK, not by number, so varying the digit
        // would read as "these stamp different numbers".
        annotation::paintNumberBadge(p, st, QPointF(box / 2.0, box / 2.0),
                                     QStringLiteral("1"));
        p.end();
        m_labelBtns[i]->setIcon(QIcon(pm));
    }
}

void PaintBar::setFontSizeValue(qreal px)
{
    m_fontPx = px;
    if (!m_fontSizeCombo)
        return;
    m_suppressFontSize = true;                 // reflect a canvas-driven scale, don't echo
    m_fontSizeCombo->setEditText(fmtFontSize(px));
    m_suppressFontSize = false;
}

void PaintBar::setPenWidthValue(int w)
{
    if (!m_width)
        return;
    m_suppressWidthStore = true;               // reflect a canvas-driven width, don't persist/echo
    m_width->setValue(w);                       // clamps to the spin box range; keeps the dot in sync
    m_suppressWidthStore = false;
}

void PaintBar::setRedactStrengthValue(int v)
{
    m_redactStrength = v;
    if (m_redSlider) {
        const QSignalBlocker block(m_redSlider);   // reflect the value, don't re-emit
        m_redSlider->setValue(v);
    }
    refreshRedactPreview();                     // updates the type-icon preview + value label
}

// Show the specimen for whichever family sits under `viewportPos` in the open
// font-family list (viewport coords), or drop it in the gap below the last row.
//
// This goes through QToolTip rather than a window of our own. A hand-rolled
// Qt::ToolTip window kept killing the drop-down: the viewport took a Leave event 1 ms
// after the panel appeared and the popup closed with it — the panel's DWM shadow
// reaches past its geometry() and across the gap to the list. QToolTip already has
// every one of those problems solved (shadow, placement, input transparency,
// z-order), and tooltips over an open combo popup are proven here — the arrow and
// dash combos below use Qt::ToolTipRole on their items the same way.
void PaintBar::previewFontAt(const QPoint& viewportPos)
{
    QAbstractItemView* view = m_fontFamilyCombo->view();
    const QModelIndex idx = view->indexAt(viewportPos);
    if (!idx.isValid()) {
        QToolTip::hideText();
        m_previewedFamily.clear();
        return;
    }
    const QString family = idx.data(Qt::DisplayRole).toString();
    if (family == m_previewedFamily)     // same row: nothing to redo. Without this the
        return;                          // specimen was rebuilt ~60x per 40 ms of hover.
    m_previewedFamily = family;

    const I18n& tr = I18n::instance();
    QStringList lines{tr.t(QStringLiteral("edit.text.preview.latin")),
                      tr.t(QStringLiteral("edit.text.preview.digits"))};
    if (familyHasChinese(family))
        lines << tr.t(QStringLiteral("edit.text.preview.cjk"));

    // Rich text is what makes the sample render in the hovered family: QToolTip lays
    // it out with QTextDocument, so the CSS font-family picks the typeface per line.
    const QString html =
        QStringLiteral("<div style=\"font-family:'%1'; font-size:%2px; white-space:nowrap\">%3</div>")
            .arg(family.toHtmlEscaped())
            .arg(kPreviewPx)
            .arg(lines.join(QStringLiteral("<br>")));

    // Place it left of the list. QToolTip offsets by (2,16) from the
    // point it is given and then clamps to the screen, so aim off by that much. The
    // width has to be predicted (QToolTip sizes itself), so measure the longest line
    // in the real font and leave slack for the tip's own padding.
    QFont f(family);
    f.setPixelSize(kPreviewPx);
    const QFontMetrics fm(f);
    int textW = 0;
    for (const QString& line : std::as_const(lines))
        textW = qMax(textW, fm.horizontalAdvance(line));

    const QRect vr = view->visualRect(idx);
    const QRect list = view->window()->frameGeometry();
    const QPoint at(list.left() - textW - 48 - kPreviewGap,
                    view->viewport()->mapToGlobal(vr.topLeft()).y() - 16);
    // Anchored to the hovered row: Qt takes the specimen down by itself once the
    // cursor leaves that row, and the next row re-shows it.
    QToolTip::showText(at, html, view->viewport(), vr, 60000);
}

void PaintBar::hideFontPreview()
{
    QToolTip::hideText();
    m_previewedFamily.clear();     // so re-entering the same row shows it again
}

bool PaintBar::eventFilter(QObject* obj, QEvent* ev)
{
    // Font-family drop-down: hover a row -> specimen panel. Both event types are
    // handled because which one arrives depends on how the popup's mouse grab
    // delivers the move — HoverMove is synthesized for the viewport (QAbstractItemView
    // sets WA_Hover on it), MouseMove needs the tracking flag set above.
    if (m_fontFamilyCombo && obj == m_fontFamilyCombo->view()->viewport()) {
        if (ev->type() == QEvent::MouseMove)
            previewFontAt(static_cast<QMouseEvent*>(ev)->position().toPoint());
        else if (ev->type() == QEvent::HoverMove)
            previewFontAt(static_cast<QHoverEvent*>(ev)->position().toPoint());
        else if (ev->type() == QEvent::Leave)
            hideFontPreview();
        return QWidget::eventFilter(obj, ev);
    }
    // The list closing has to take the specimen with it: QToolTip anchors it to a row
    // of a viewport that is about to go away.
    if (m_fontFamilyCombo && obj == m_fontFamilyCombo->view() && ev->type() == QEvent::Hide) {
        hideFontPreview();
        return QWidget::eventFilter(obj, ev);
    }
    if (ev->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(ev);
        const int step = we->angleDelta().y() > 0 ? 1 : -1;
        if (obj == m_hollowBtn) {                 // scroll the dot to set the shape width
            if (m_width) m_width->setValue(m_width->value() + step);   // → updateHollowIcon + penWidthChanged
            return true;
        }
        if (obj == m_redBrushBtn) { adjustRedactBrush(step); return true; }
        if (obj == m_redTypeBtn)  { setRedactType(!m_redactBlur); return true; }  // scroll switches type
        if (obj == m_eraseCircleBtn) { adjustEraserSize(step); return true; }
        if (obj == m_markDotBtn)     { adjustMarkerWidth(step); return true; }
    }
    return QWidget::eventFilter(obj, ev);
}

void PaintBar::addColorPalette(QHBoxLayout* into, QWidget* parent)
{
    // Picker-entry block: opens the custom ColorDialog and mirrors the last-chosen
    // colour. Registered so applyColor() can keep every palette's block in sync.
    auto* entry = new QPushButton(parent);
    entry->setFixedSize(38, 38);
    entry->setCursor(Qt::PointingHandCursor);
    into->addWidget(entry);
    connect(entry, &QPushButton::clicked, this, &PaintBar::pickCustomColor);
    m_pickerEntries.push_back(entry);

    auto* gridW = new QWidget(parent);
    auto* grid = new QGridLayout(gridW);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 10; ++c) {
            const QColor col = kBasicColors[r][c];
            auto* sw = new QPushButton(gridW);
            sw->setFixedSize(17, 17);
            sw->setCursor(Qt::PointingHandCursor);
            sw->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:1px solid #888;border-radius:2px;}"
                                             "QPushButton:hover{border:2px solid %2;}")
                                  .arg(col.name(), accent().name()));
            grid->addWidget(sw, r, c);
            connect(sw, &QPushButton::clicked, this, [this, col] { applyColor(col); });
        }
    into->addWidget(gridW);
}

QPixmap PaintBar::swatchPixmap(const QColor& c, int box)
{
    QPixmap pm(box, box);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    // Checkerboard underlay — only visible where the colour is translucent, so the
    // swatch reads as "this alpha" the same way the ColorDialog's preview does.
    const int cell = 5;
    for (int y = 0; y < box; y += cell)
        for (int x = 0; x < box; x += cell) {
            const bool dark = (((x / cell) + (y / cell)) & 1) != 0;
            p.fillRect(x, y, cell, cell,
                       dark ? QColor(0xC0, 0xC0, 0xC0) : QColor(0xFF, 0xFF, 0xFF));
        }
    p.fillRect(0, 0, box, box, c);               // the colour at its real alpha
    p.end();
    return pm;
}

void PaintBar::applyColor(const QColor& c)
{
    if (!c.isValid())
        return;
    m_color = c;
    const QPixmap sw = swatchPixmap(c, 32);
    for (QPushButton* entry : m_pickerEntries) {   // every palette's block mirrors the colour
        // Transparent background + a pixmap icon: the checkerboard shows the alpha that a
        // flat stylesheet `background` (#rrggbb only) would have dropped.
        entry->setStyleSheet(
            QStringLiteral("QPushButton{background:transparent;border:2px solid %1;border-radius:3px;}")
                .arg(accent().name()));
        entry->setIcon(QIcon(sw));
        entry->setIconSize(QSize(32, 32));
    }
    updateLabelIcons();                            // badge previews follow the colour
    if (m_settings)                                // remembered across snips and restarts
        m_settings->setValue(QString::fromLatin1(kLastColorKey), c.name(QColor::HexArgb));
    emit colorSelected(c);
}

void PaintBar::pickCustomColor()
{
    // Parent to the top-level host window, NOT to PaintBar: PaintBar's stylesheet
    // (QPushButton/QSpinBox/QLabel rules) would otherwise cascade into the dialog
    // and strip its native control styling. window() has no stylesheet.
    QWidget* host = window();
    const QColor c = ColorDialog::getColor(m_color, m_settings, host ? host : this);
    applyColor(c);
}

void PaintBar::setHost(Host h)
{
    m_host = h;
    // A pin has no snip to copy/save/pin — hide those, keep cancel (= close the toolbar)
    // and undo/redo. The bar re-lays out around the hidden buttons on the next adjustSize().
    const bool snip = (h == Host::Snip);
    if (m_pin)  m_pin->setVisible(snip);
    if (m_save) m_save->setVisible(snip);
    if (m_copy) m_copy->setVisible(snip);
    // Recording is a snip-side flow (it needs a screen region, not a pin's pixels).
    if (m_recordBtn) m_recordBtn->setVisible(snip);
    // Same for scrolling capture: it scrolls the live window under a screen region.
    if (m_scrollBtn) m_scrollBtn->setVisible(snip);
    retranslate();                 // `cancel` means something different on a pin
}

void PaintBar::setUndoRedoState(bool canUndo, bool canRedo)
{
    if (m_undo) m_undo->setEnabled(canUndo);
    if (m_redo) m_redo->setEnabled(canRedo);
}

void PaintBar::retranslate()
{
    auto& tr = I18n::instance();
    for (const GroupBtn& gb : m_groupBtns)
        gb.btn->setToolTip(tr.t(gb.tipKey));
    for (const VariantBtn& vb : m_variantBtns) {
        const QString label = tr.t(vb.key);
        if (!vb.hasIcon)                     // icon buttons stay icon-only (tooltip only)
            vb.btn->setText(label);
        vb.btn->setToolTip(label);
    }
    if (m_widthLabel)  m_widthLabel->setText(tr.t(QStringLiteral("edit.penwidth")));
    updateCurveBtn();                    // its tooltip is mode-dependent
    if (m_hollowBtn)   m_hollowBtn->setToolTip(tr.t(QStringLiteral("edit.shape.widthDot")));
    updateFillToggle();                  // its tooltip is mode-dependent
    for (QComboBox* c : {m_startCapCombo, m_endCapCombo}) {
        if (!c) continue;
        c->setToolTip(tr.t(c == m_startCapCombo ? QStringLiteral("edit.cap.startStyle")
                                                : QStringLiteral("edit.cap.endStyle")));
        for (int i = 0; i < kCapCount && i < c->count(); ++i)
            c->setItemData(i, tr.t(QString::fromLatin1(kCaps[i].tip)), Qt::ToolTipRole);
    }
    if (m_lineDashCombo) {
        m_lineDashCombo->setToolTip(tr.t(QStringLiteral("edit.linestyle.style")));
        for (int i = 0; i < kLineDashCount && i < m_lineDashCombo->count(); ++i)
            m_lineDashCombo->setItemData(i, tr.t(QString::fromLatin1(kDashes[i].tip)),
                                         Qt::ToolTipRole);
    }
    if (m_dashCombo) {
        m_dashCombo->setToolTip(tr.t(QStringLiteral("edit.linestyle.style")));
        for (int i = 0; i < kDashCount && i < m_dashCombo->count(); ++i)
            m_dashCombo->setItemData(i, tr.t(QString::fromLatin1(kDashes[i].tip)), Qt::ToolTipRole);
    }
    if (m_numberCombo) {
        m_numberCombo->setToolTip(tr.t(QStringLiteral("edit.numbering.style")));
        for (int i = 0; i < kNumberingCount && i < m_numberCombo->count(); ++i)
            m_numberCombo->setItemData(i, tr.t(QString::fromLatin1(kNumberings[i].tip)),
                                       Qt::ToolTipRole);
    }
    for (QPushButton* entry : m_pickerEntries)
        entry->setToolTip(tr.t(QStringLiteral("edit.color.custom")));
    if (m_undo)   m_undo->setToolTip(tr.t(QStringLiteral("edit.undo")));
    if (m_redo)   m_redo->setToolTip(tr.t(QStringLiteral("edit.redo")));
    if (m_ocrBtn) m_ocrBtn->setToolTip(tr.t(QStringLiteral("ocr.toolbar.ocr")));
    if (m_scrollBtn) m_scrollBtn->setToolTip(tr.t(QStringLiteral("scroll.toolbar.scroll")));
    // Only overwrite the record tooltip while the button is usable — setRecordEnabled()
    // parks the "why it's off" reason there, and retranslate() must not clobber it.
    if (m_recordBtn && m_recordBtn->isEnabled())
        m_recordBtn->setToolTip(tr.t(QStringLiteral("record.toolbar.record")));
    if (m_copy)   m_copy->setToolTip(tr.t(QStringLiteral("snip.toolbar.copy")));
    if (m_save)   m_save->setToolTip(tr.t(QStringLiteral("snip.toolbar.save")));
    if (m_pin)    m_pin->setToolTip(tr.t(QStringLiteral("snip.toolbar.pin")));
    if (m_cancel)
        m_cancel->setToolTip(tr.t(m_host == Host::Pin
                                      ? QStringLiteral("edit.close_toolbar")
                                      : QStringLiteral("snip.toolbar.close")));
    if (m_labelBtns.size() == 3) {
        m_labelBtns[0]->setToolTip(tr.t(QStringLiteral("edit.label.filled")));
        m_labelBtns[1]->setToolTip(tr.t(QStringLiteral("edit.label.ringed")));
        m_labelBtns[2]->setToolTip(tr.t(QStringLiteral("edit.label.hollow")));
    }
    if (m_labelSizeCombo) m_labelSizeCombo->setToolTip(tr.t(QStringLiteral("edit.label.size")));
    if (m_redBrushBtn) m_redBrushBtn->setToolTip(tr.t(QStringLiteral("edit.redact.brush")));
    if (m_redRectBtn)  m_redRectBtn->setToolTip(tr.t(QStringLiteral("edit.redact.region")));
    if (m_redTypeBtn)  m_redTypeBtn->setToolTip(tr.t(QStringLiteral("edit.redact.type")));
    if (m_redSlider)   m_redSlider->setToolTip(tr.t(QStringLiteral("edit.redact.degree")));
    if (m_eraseCircleBtn) m_eraseCircleBtn->setToolTip(tr.t(QStringLiteral("edit.eraser.circle")));
    if (m_eraseRectBtn)   m_eraseRectBtn->setToolTip(tr.t(QStringLiteral("edit.eraser.rect")));
    if (m_markDotBtn)  m_markDotBtn->setToolTip(tr.t(QStringLiteral("edit.marker.pen")));
    if (m_markRectBtn) m_markRectBtn->setToolTip(tr.t(QStringLiteral("edit.marker.rect")));
    if (m_textBoldBtn)     m_textBoldBtn->setToolTip(tr.t(QStringLiteral("edit.text.bold")));
    if (m_textItalicBtn)   m_textItalicBtn->setToolTip(tr.t(QStringLiteral("edit.text.italic")));
    if (m_textHollowBtn)   m_textHollowBtn->setToolTip(tr.t(QStringLiteral("edit.text.hollow")));
    if (m_fontFamilyCombo) m_fontFamilyCombo->setToolTip(tr.t(QStringLiteral("edit.text.family")));
    if (m_fontSizeCombo)   m_fontSizeCombo->setToolTip(tr.t(QStringLiteral("edit.text.size")));
}
