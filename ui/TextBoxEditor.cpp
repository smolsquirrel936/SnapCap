#include "ui/TextBoxEditor.h"

#include "ui/PrefsStyle.h"
#include "core/Settings.h"

#include <QSettings>
#include <QTextEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRegion>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QCursor>
#include <QPixmap>
#include <QTransform>
#include <QFont>
#include <QFontMetrics>
#include <QStringList>
#include <QtMath>
#include <cmath>

namespace {
constexpr int kStalk   = 28;   // gap above the top edge to the rotate/close handles
constexpr int kEditPad = 10;   // inset of the QTextEdit inside the dashed border
constexpr int kGripR   = 8;    // corner grip hit radius
constexpr int kHandleR = 9;    // rotate/close handle radius

double angleDeg(const QPointF& v) { return std::atan2(v.y(), v.x()) * 180.0 / M_PI; }

QTransform rotateAbout(const QPointF& c, qreal deg)
{
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(deg);
    t.translate(-c.x(), -c.y());
    return t;
}

// Word-wrap matching core/annotation TextShape::wrapLines exactly, so the live rotated
// preview lays out identically to the committed TextShape (WYSIWYG).
QStringList wrapLines(const QString& text, const QFontMetrics& fm, int width)
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

// A custom cursor for the rotate handle: two circular "sync" arrows — white bodies +
// heads with a thin dark edge so they read on any background. Mirrored horizontally
// so the rotation reads counter-clockwise. Built once and reused.
QCursor rotateCursor()
{
    constexpr int S = 32;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF c(S / 2.0, S / 2.0);
    const double R = 8.5;        // centre-line radius of the ring
    const double hw = 1.9;       // body half-width (band thickness / 2)
    const double headHalf = 4.2; // arrowhead half-width at its base
    const QColor ink(0x10, 0x10, 0x10);

    auto dir = [](double deg) {
        const double t = deg * M_PI / 180.0;
        return QPointF(std::cos(t), std::sin(t));   // screen coords (y down)
    };
    auto arrow = [&](double a0, double a1, double aHead) {
        QPolygonF poly;
        const int steps = 20;
        for (int i = 0; i <= steps; ++i)               // outer edge, a0 -> a1
            poly << c + (R + hw) * dir(a0 + (a1 - a0) * i / steps);
        poly << c + (R + headHalf) * dir(a1);          // outer wing
        poly << c + R * dir(aHead);                    // tip
        poly << c + (R - headHalf) * dir(a1);          // inner wing
        for (int i = steps; i >= 0; --i)               // inner edge, a1 -> a0
            poly << c + (R - hw) * dir(a0 + (a1 - a0) * i / steps);
        QPainterPath path;
        path.addPolygon(poly);
        path.closeSubpath();
        return path;
    };

    QPainterPath arrows = arrow(205, 340, 372);
    arrows.addPath(arrow(25, 160, 192));

    p.setBrush(Qt::white);
    p.setPen(QPen(ink, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(arrows);
    p.end();
    return QCursor(pm.transformed(QTransform().scale(-1, 1)), S / 2, S / 2);
}
}

TextBoxEditor::TextBoxEditor(QWidget* parent) : QWidget(parent)
{
    m_accent = PrefsStyle::accentColor();   // chrome (border/handles/grips) follows the theme
    // The dashed input-box border tracks 1/3 of the snip border-width setting (rounded up).
    // Read read-only from the portable config (same source as PrefsStyle::accentColor()).
    {
        QSettings qs(Settings::resolveConfigPath(), QSettings::IniFormat);
        const int bw = qMax(1, qs.value(QStringLiteral("snip/display/borderWidth"), 3).toInt());
        m_boxLineWidth = (bw + 2) / 3;      // ceil(bw / 3)
    }
    // Transparent container so the snip shows through the margins; only the chrome and
    // (when rotated) the text we paint ourselves appear.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMouseTracking(true);

    m_edit = new QTextEdit(this);
    m_edit->setFrameStyle(QFrame::NoFrame);
    m_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // No wrapping: the box grows to the right as you type; only Enter adds a line.
    m_edit->setLineWrapMode(QTextEdit::NoWrap);
    // No document margin: text lays out flush with the widget's top-left so the inner
    // textRect maps 1:1 onto the committed TextShape (no preview→result drift).
    m_edit->document()->setDocumentMargin(0);
    m_edit->setContextMenuPolicy(Qt::NoContextMenu);
    m_edit->setAttribute(Qt::WA_TranslucentBackground, true);
    m_edit->viewport()->setAutoFillBackground(false);
    m_edit->installEventFilter(this);
    // The box grows to fit the text as the user types (rightward) / presses Enter (down).
    connect(m_edit, &QTextEdit::textChanged, this, [this] { autoFitOnText(); update(); });
    applyFont();
    setTextColor(m_color);
}

QFont TextBoxEditor::previewFont() const
{
    QFont f(m_family);
    f.setPixelSize(qMax(6, int(qRound(m_fontPx))));
    f.setBold(m_bold);
    f.setItalic(m_italic);
    return f;
}

void TextBoxEditor::applyFont() { m_edit->setFont(previewFont()); }

void TextBoxEditor::setTextColor(const QColor& c)
{
    m_color = c;
    applyEditPalette();
}

void TextBoxEditor::applyEditPalette()
{
    // Transparent box (no background). While rotated we paint the text ourselves, so
    // the QTextEdit's own glyphs are made transparent (it only catches input then);
    // upright, it shows the real colour.
    const QColor c = m_selfRender ? QColor(0, 0, 0, 0) : m_color;
    m_edit->setStyleSheet(QStringLiteral(
        "QTextEdit{background:transparent;border:none;color:rgba(%1,%2,%3,%4);}")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
}

void TextBoxEditor::updateRotationVisuals()
{
    const bool sr = !qFuzzyIsNull(m_rotation);
    if (sr != m_selfRender) {
        m_selfRender = sr;
        m_edit->setCursorWidth(sr ? 0 : 1);                       // hide upright caret
        m_edit->setAttribute(Qt::WA_TransparentForMouseEvents, sr); // chrome gets clicks
        applyEditPalette();
    }
    updateMask();
    update();
}

void TextBoxEditor::setFontFamily(const QString& family) { m_family = family; applyFont(); autoFitOnText(); }
void TextBoxEditor::setFontPixelSize(qreal px)           { m_fontPx = px;     applyFont(); autoFitOnText(); }
void TextBoxEditor::setBold(bool b)                      { m_bold = b;        applyFont(); autoFitOnText(); }
void TextBoxEditor::setItalic(bool b)                    { m_italic = b;      applyFont(); autoFitOnText(); }
void TextBoxEditor::setHollow(bool h)                    { m_hollow = h;      update(); }

// ---------------------------------------------------------------------------- geometry

int TextBoxEditor::reqRadius() const
{
    const double w = m_box.width(), h = m_box.height();
    const double cornerR = 0.5 * std::hypot(w, h);                       // plain corner
    // raised handle + room for the degree-readout pill drawn just beyond it
    const double handleR = std::hypot(w / 2.0, h / 2.0 + kStalk + kHandleR) + 48;
    return int(std::ceil(qMax(cornerR, handleR))) + kGripR + 4;
}

QRect TextBoxEditor::contentRectLocal() const
{
    const int R = reqRadius();
    return QRect(R - m_box.width() / 2, R - m_box.height() / 2, m_box.width(), m_box.height());
}

QRect TextBoxEditor::textRectLocal() const
{
    return contentRectLocal().adjusted(kEditPad, kEditPad, -kEditPad, -kEditPad);
}

QRect TextBoxEditor::textRect() const
{
    const QRect t = textRectLocal();
    return QRect(mapToParent(t.topLeft()), t.size());   // parent (host-local) coords
}

QPoint TextBoxEditor::cornerLocal(Grip g) const
{
    const QRect r = contentRectLocal();
    switch (g) {
        case Grip::TL: return r.topLeft();
        case Grip::TR: return r.topRight();
        case Grip::BL: return r.bottomLeft();
        case Grip::BR: return r.bottomRight();
        default:       return r.center();
    }
}

QPoint TextBoxEditor::rotateCenterLocal() const
{
    const QRect r = contentRectLocal();
    return QPoint(r.center().x(), r.top() - kStalk);
}

QPoint TextBoxEditor::closeCenterLocal() const
{
    const QRect r = contentRectLocal();
    return QPoint(r.right(), r.top() - kStalk);
}

QTransform TextBoxEditor::rotationXform() const
{
    return rotateAbout(QRectF(contentRectLocal()).center(), m_rotation);
}

QPoint TextBoxEditor::toUnrotated(const QPoint& local) const
{
    if (qFuzzyIsNull(m_rotation))
        return local;
    return rotationXform().inverted().map(QPointF(local)).toPoint();
}

TextBoxEditor::Grip TextBoxEditor::gripAt(const QPoint& p) const
{
    auto near = [&](const QPoint& c, int r) {
        return std::hypot(double(p.x() - c.x()), double(p.y() - c.y())) <= r;
    };
    if (near(closeCenterLocal(), kHandleR))  return Grip::Close;
    if (near(rotateCenterLocal(), kHandleR)) return Grip::Rotate;
    for (Grip g : {Grip::TL, Grip::TR, Grip::BL, Grip::BR})
        if (near(cornerLocal(g), kGripR)) return g;
    if (contentRectLocal().adjusted(-kGripR, -kGripR, kGripR, kGripR).contains(p))
        return Grip::Move;
    return Grip::None;
}

void TextBoxEditor::setContentBox(const QRect& box)
{
    m_box = box;
    const QPoint ctr = box.center();
    const int R = reqRadius();
    setGeometry(QRect(ctr.x() - R, ctr.y() - R, 2 * R, 2 * R));
    const QRect tl = textRectLocal();
    m_edit->setGeometry(QRect(tl.topLeft(), QSize(qMax(1, tl.width()), qMax(1, tl.height()))));
    updateMask();
    update();
}

void TextBoxEditor::setTextRect(const QRect& innerParent)
{
    // Inverse of textRect(): grow the inner text rect by the editor's padding to get
    // the chrome content box, so a re-opened editor lands exactly where it committed.
    setContentBox(innerParent.adjusted(-kEditPad, -kEditPad, kEditPad, kEditPad));
}

void TextBoxEditor::setRotationDeg(qreal deg)
{
    m_rotation = deg;
    updateRotationVisuals();                 // toggle self-render + refit the mask
}

void TextBoxEditor::updateMask()
{
    const QTransform t = rotationXform();
    // Slack around the content rect so the (possibly wide) dashed border pen is fully
    // visible on every side — without it the pen straddles the mask edge and the
    // top/left half gets clipped, making those edges look thin.
    const int pad = m_boxLineWidth + 1;
    QRegion reg(t.map(QPolygonF(QRectF(contentRectLocal().adjusted(-pad, -pad, pad, pad)))).toPolygon());
    for (Grip g : {Grip::TL, Grip::TR, Grip::BL, Grip::BR}) {
        const QPoint c = t.map(QPointF(cornerLocal(g))).toPoint();
        reg += QRect(c.x() - kGripR - 2, c.y() - kGripR - 2, 2 * (kGripR + 2), 2 * (kGripR + 2));
    }
    const QPoint rc = t.map(QPointF(rotateCenterLocal())).toPoint();
    const QPoint cc = t.map(QPointF(closeCenterLocal())).toPoint();
    // Generous reserve around the rotate handle so the degree-readout pill (drawn just
    // outside it, in any rotation) is never clipped by the mask.
    reg += QRect(rc.x() - 48, rc.y() - 48, 96, 96);
    reg += QRect(cc.x() - kHandleR - 2, cc.y() - kHandleR - 2, 2 * (kHandleR + 2), 2 * (kHandleR + 2));
    const QRect cr = contentRectLocal();
    const QPoint tc = t.map(QPointF(cr.center().x(), cr.top())).toPoint();
    reg += QRect(tc, rc).normalized().adjusted(-kHandleR, -kHandleR, kHandleR, kHandleR);
    setMask(reg);
}

// ---------------------------------------------------------------------------- text I/O

QString TextBoxEditor::text() const           { return m_edit->toPlainText(); }
void TextBoxEditor::setText(const QString& t)  { m_edit->setPlainText(t); }
void TextBoxEditor::focusEditor()             { m_edit->setFocus(); }

// Box size that fits the text with NO wrapping: width = longest line, height = the
// number of explicit ('\n') lines. So typing grows the box rightward and Enter grows
// it downward.
QSize TextBoxEditor::autoContentSize() const
{
    const QFontMetrics fm(previewFont());
    const QStringList lines = m_edit->toPlainText().split(QLatin1Char('\n'));
    int w = 0;
    for (const QString& l : lines)
        w = qMax(w, fm.horizontalAdvance(l));
    const int innerW = qMax(fm.averageCharWidth() * 2 + 4, w + 4);   // slack for the caret
    const int innerH = qMax(1, int(lines.size())) * fm.lineSpacing();
    return QSize(innerW + 2 * kEditPad, innerH + 2 * kEditPad);
}

// Resize to `ns`, keeping the parent point `fixedParent` pinned — it corresponds to the
// box corner/centre whose offset-from-centre signs are `sign` (0 = centre).
void TextBoxEditor::resizePinned(const QSize& ns, const QPointF& fixedParent, const QPoint& sign)
{
    const QTransform R = QTransform().rotate(m_rotation);
    const QPointF off(sign.x() * ns.width() / 2.0, sign.y() * ns.height() / 2.0);
    const QPointF center = fixedParent - R.map(off);
    setContentBox(QRect(QPoint(qRound(center.x() - ns.width() / 2.0),
                               qRound(center.y() - ns.height() / 2.0)), ns));
}

void TextBoxEditor::autoFitOnText()
{
    if (m_box.isEmpty())
        return;
    const QSize ns = autoContentSize();
    if (ns == m_box.size())
        return;
    // Keep the (rotated) top-left corner pinned → grow right / down.
    const QTransform R = QTransform().rotate(m_rotation);
    const QPointF tl = QPointF(m_box.center())
                     + R.map(QPointF(-m_box.width() / 2.0, -m_box.height() / 2.0));
    resizePinned(ns, tl, QPoint(-1, -1));
}

void TextBoxEditor::scaleAboutCenter(qreal factor)
{
    m_fontPx = qBound(6.0, qreal(qRound(m_fontPx * factor)), 2000.0);   // integer sizes
    applyFont();
    resizePinned(autoContentSize(), QPointF(m_box.center()), QPoint(0, 0));
    emit fontSizeChanged(m_fontPx);
}

// ---------------------------------------------------------------------------- painting

void TextBoxEditor::renderText(QPainter& p, bool haloOnly) const
{
    const QString s = m_edit->toPlainText();
    if (s.isEmpty())
        return;
    const QFont f = previewFont();
    const QFontMetrics fm(f);
    const QRect cr = textRectLocal();
    const QStringList lines = wrapLines(s, fm, cr.width());

    p.save();
    p.setTransform(rotationXform(), true);
    p.setRenderHint(QPainter::Antialiasing, true);
    qreal y = cr.top() + fm.ascent();
    if (m_hollow) {
        // White-surrounded: stroke a white halo, then fill the coloured glyphs on top.
        // In haloOnly mode (upright preview) we draw just the halo and let the upright
        // QTextEdit supply the coloured glyphs on top — so editing/caret keep working.
        QPainterPath path;
        for (const QString& l : lines) { path.addText(qreal(cr.left()), y, f, l); y += fm.lineSpacing(); }
        p.setPen(QPen(Qt::white, qMax(2.0, m_fontPx / 8.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        if (!haloOnly) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_color);
            p.drawPath(path);
        }
    } else {
        p.setFont(f);
        p.setPen(m_color);
        for (const QString& l : lines) { p.drawText(QPointF(cr.left(), y), l); y += fm.lineSpacing(); }
    }
    p.restore();
}

void TextBoxEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRect cr = contentRectLocal();

    // Text painting (behind the chrome). While rotated the QTextEdit is hidden, so we
    // render the full text ourselves; while upright we only add the white halo behind
    // the QTextEdit's own glyphs so hollow text previews live without losing the caret.
    if (m_selfRender)
        renderText(p, /*haloOnly=*/false);     // rotated → full text (incl. white-surround)
    else if (m_hollow)
        renderText(p, /*haloOnly=*/true);      // upright hollow → halo behind live glyphs

    // The chrome rotates with the box.
    p.save();
    p.setTransform(rotationXform(), true);

    p.setPen(QPen(m_accent, m_boxLineWidth, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(cr.adjusted(-1, -1, 0, 0));

    const QPoint rotC = rotateCenterLocal();
    p.setPen(QPen(m_accent, 1));
    p.drawLine(QPoint(cr.center().x(), cr.top()), rotC);

    auto handle = [&](const QPoint& c) {
        p.setPen(QPen(m_accent, 1));
        p.setBrush(Qt::white);
        p.drawEllipse(c, kHandleR, kHandleR);
    };

    handle(rotC);
    {
        const int rr = 4;
        const QRectF arcBox(rotC.x() - rr, rotC.y() - rr, 2 * rr, 2 * rr);
        QPainterPath arc;
        arc.arcMoveTo(arcBox, 60);
        arc.arcTo(arcBox, 60, 270);
        p.setPen(QPen(m_accent.darker(150), 1.4));   // rotate glyph: a deeper accent shade
        p.setBrush(Qt::NoBrush);
        p.drawPath(arc);
    }

    const QPoint clC = closeCenterLocal();
    handle(clC);
    {
        const int d = 3;
        p.setPen(QPen(QColor(0xc0, 0x39, 0x2b), 1.6));
        p.drawLine(clC.x() - d, clC.y() - d, clC.x() + d, clC.y() + d);
        p.drawLine(clC.x() - d, clC.y() + d, clC.x() + d, clC.y() - d);
    }

    p.setPen(QPen(m_accent, 1));
    p.setBrush(Qt::white);
    for (Grip g : {Grip::TL, Grip::TR, Grip::BL, Grip::BR}) {
        const QPoint c = cornerLocal(g);
        p.drawRect(QRect(c.x() - 4, c.y() - 4, 8, 8));
    }
    p.restore();

    // Degree readout (screen-aligned) while actively rotating. Placed OUTWARD from the
    // box centre, past the rotate handle, with an opaque pill — so the input box never
    // occludes it.
    if (m_drag == Grip::Rotate) {
        const QPoint rc = rotationXform().map(rotC);
        QPointF out = QPointF(rc) - QRectF(cr).center();
        const double L = std::hypot(out.x(), out.y());
        out = (L > 0.1) ? out / L : QPointF(0, -1);
        const QPoint pos = (QPointF(rc) + out * (kHandleR + 10)).toPoint();
        const QString txt = QString::number(qRound(m_rotation)) + QStringLiteral("°");
        QFont vf = font(); vf.setBold(true);
        p.setFont(vf);
        const QFontMetrics fm(vf);
        QRect pill(0, 0, fm.horizontalAdvance(txt) + 12, fm.height() + 4);
        pill.moveCenter(pos);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 190));
        p.drawRoundedRect(pill, 4, 4);
        p.setPen(Qt::white);
        p.drawText(pill, Qt::AlignCenter, txt);
    }
}

// ----------------------------------------------------------------------------- input

void TextBoxEditor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) {     // right-click confirms (bakes) the box
        m_drag = Grip::None;
        emit committed();
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    const QPoint local = e->position().toPoint();
    m_drag = gripAt(toUnrotated(local));
    const QPoint parentPos = mapToParent(local);

    switch (m_drag) {
        case Grip::None:
            return;
        case Grip::Close:                 // close the box, discarding what was typed
            m_drag = Grip::None;
            emit canceled();
            return;
        case Grip::Move:
            m_moveStartMouse = parentPos;
            m_moveStartTopLeft = m_box.topLeft();
            break;
        case Grip::Rotate:
            m_rotCenter = m_box.center();
            m_rotStart = m_rotation;
            m_rotMouseStart = angleDeg(QPointF(parentPos - m_rotCenter));
            break;
        case Grip::TL: case Grip::TR: case Grip::BL: case Grip::BR: {
            m_boxStart = m_box;
            m_fontStart = m_fontPx;
            const QPointF c0 = m_box.center();
            const qreal hw = m_box.width() / 2.0, hh = m_box.height() / 2.0;
            auto off = [&](Grip g) -> QPointF {
                switch (g) {
                    case Grip::TL: return QPointF(-hw, -hh);
                    case Grip::TR: return QPointF(+hw, -hh);
                    case Grip::BL: return QPointF(-hw, +hh);
                    default:       return QPointF(+hw, +hh);
                }
            };
            const Grip opp = m_drag == Grip::TL ? Grip::BR : m_drag == Grip::TR ? Grip::BL
                           : m_drag == Grip::BL ? Grip::TR : Grip::TL;
            const QTransform R = QTransform().rotate(m_rotation);
            m_resizeFixed = c0 + R.map(off(opp));               // pinned (rotated) opposite corner
            m_resizeDiag  = (c0 + R.map(off(m_drag))) - m_resizeFixed;
            m_resizeSign  = QPoint(off(opp).x() < 0 ? -1 : 1, off(opp).y() < 0 ? -1 : 1);
            break;
        }
        default: break;
    }
    update();
}

void TextBoxEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag == Grip::None) {              // hover: reflect each control's function
        static const QCursor kRotate = rotateCursor();
        switch (gripAt(toUnrotated(e->position().toPoint()))) {
            case Grip::TL: case Grip::BR: setCursor(Qt::SizeFDiagCursor); break;
            case Grip::TR: case Grip::BL: setCursor(Qt::SizeBDiagCursor); break;
            case Grip::Rotate:            setCursor(kRotate);            break;
            case Grip::Close:             setCursor(Qt::PointingHandCursor); break;
            case Grip::Move:              setCursor(Qt::SizeAllCursor);  break;
            default:                      unsetCursor();                 break;
        }
        return;
    }
    const QPoint parentPos = mapToParent(e->position().toPoint());

    if (m_drag == Grip::Move) {
        const QPoint delta = parentPos - m_moveStartMouse;
        setContentBox(QRect(m_moveStartTopLeft + delta, m_box.size()));
        return;
    }
    if (m_drag == Grip::Rotate) {
        const double a = angleDeg(QPointF(parentPos - m_rotCenter));
        m_rotation = m_rotStart + (a - m_rotMouseStart);
        emit rotationChanged(m_rotation);
        updateRotationVisuals();             // live WYSIWYG rotation of the whole box
        return;
    }
    // Corner: scale font + box by the cursor's projection along the diagonal, keeping
    // the opposite (rotated) corner pinned — the classic top-left-anchored resize.
    const QPointF d0 = m_resizeDiag;
    const double denom = d0.x() * d0.x() + d0.y() * d0.y();
    if (denom < 1.0)
        return;
    const QPointF dm = QPointF(parentPos) - m_resizeFixed;
    double factor = (dm.x() * d0.x() + dm.y() * d0.y()) / denom;     // scalar projection
    factor = qBound(0.15, factor, 40.0);

    // Corner-drag scales the font (integer); the box then auto-fits the text, pinned at
    // the opposite (rotated) corner.
    m_fontPx = qBound(6.0, qreal(qRound(m_fontStart * factor)), 2000.0);
    applyFont();
    resizePinned(autoContentSize(), m_resizeFixed, m_resizeSign);
    emit fontSizeChanged(m_fontPx);
}

void TextBoxEditor::wheelScale(int steps)
{
    scaleAboutCenter(steps > 0 ? 1.1 : 1.0 / 1.1);
}

void TextBoxEditor::mouseReleaseEvent(QMouseEvent*)
{
    if (m_drag != Grip::None) {
        m_drag = Grip::None;
        update();
    }
}

void TextBoxEditor::wheelEvent(QWheelEvent* e)
{
    scaleAboutCenter(e->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1);   // scroll = scale about centre
    e->accept();
}

bool TextBoxEditor::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_edit && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Escape) {     // Esc commits the box (Enter = newline)
            emit committed();
            return true;
        }
    }
    if (o == m_edit && e->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() == Qt::RightButton) { emit committed(); return true; }
    }
    if (o == m_edit && e->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(e);
        scaleAboutCenter(we->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1);
        return true;
    }
    return QWidget::eventFilter(o, e);
}
