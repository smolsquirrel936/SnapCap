#include "ui/ScrollCaptureBar.h"

#include "core/i18n/I18n.h"
#include "ui/PrefsStyle.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineF>
#include <QPainter>
#include <QPixmap>
#include <QPolygon>
#include <QPushButton>
#include <QStyle>
#include <QToolTip>
#include <QtMath>

#include <cmath>

namespace {

// The bar's buttons are icon-only, so each glyph is drawn here in the same neutral ink,
// 40 px canvas and 3 px round pen as PaintBar's code-drawn icons — there is no icons/*.svg
// for any of them.
template <class Draw>
QIcon makeGlyph(Draw draw)
{
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(0x33, 0x33, 0x33));
    pen.setWidthF(3.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    draw(p);
    p.end();
    return QIcon(pm);
}

// Capture axis: a double-headed arrow pointing the way the capture grows. The glyph is
// the state, so these two are what the direction button switches between.
QIcon axisVerticalIcon()
{
    return makeGlyph([](QPainter& p) {
        p.drawLine(20, 6, 20, 34);
        p.drawPolyline(QPolygon() << QPoint(13, 15) << QPoint(20, 6)  << QPoint(27, 15));
        p.drawPolyline(QPolygon() << QPoint(13, 25) << QPoint(20, 34) << QPoint(27, 25));
    });
}

QIcon axisHorizontalIcon()
{
    return makeGlyph([](QPainter& p) {
        p.drawLine(6, 20, 34, 20);
        p.drawPolyline(QPolygon() << QPoint(15, 13) << QPoint(6, 20)  << QPoint(15, 27));
        p.drawPolyline(QPolygon() << QPoint(25, 13) << QPoint(34, 20) << QPoint(25, 27));
    });
}

// The two hands. Unlike every other glyph here these are FILLED silhouettes, not strokes:
// at 24 px an outlined hand collapses into noise, while a solid shape still reads. The
// finger separations are punched out with CompositionMode_Clear rather than drawn in a
// colour, so they show the button's own background and stay right on hover/checked.
QPixmap handCanvas()
{
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    return pm;
}

void fillCapsule(QPainter& p, QPointF a, QPointF b, qreal width)
{
    QPen pen(QColor(0x33, 0x33, 0x33));
    pen.setWidthF(width);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QLineF(a, b));
}

void clearGroove(QPainter& p, QPointF a, QPointF b)
{
    p.save();
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    QPen pen(Qt::black);          // colour is irrelevant in Clear mode; only the shape is
    pen.setWidthF(1.6);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QLineF(a, b));
    p.restore();
}

// The four finger roots, shared by both hands so the knuckles line up with the fingers.
constexpr qreal kFingerX[4] = {14.2, 19.0, 23.8, 28.4};
constexpr qreal kGrooveX[3] = {16.6, 21.4, 26.1};

// Move, off: an open palm — "you may grab the capture area".
QIcon palmOpenIcon()
{
    QPixmap pm = handCanvas();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x33, 0x33, 0x33));
    p.drawRoundedRect(QRectF(11.5, 19, 19, 15), 5.5, 5.5);        // palm
    const qreal tops[4] = {11.5, 8.5, 10.0, 14.0};                 // middle finger tallest
    for (int i = 0; i < 4; ++i)
        fillCapsule(p, QPointF(kFingerX[i], 21), QPointF(kFingerX[i], tops[i]), 4.4);
    fillCapsule(p, QPointF(13, 27), QPointF(6.5, 20.5), 5.0);      // thumb, out to the left
    for (int i = 0; i < 3; ++i)
        clearGroove(p, QPointF(kGrooveX[i], tops[i + 1] - 1), QPointF(kGrooveX[i], 20));
    p.end();
    return QIcon(pm);
}

// Move, on: the same hand closed — "you are holding it".
QIcon fistIcon()
{
    QPixmap pm = handCanvas();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x33, 0x33, 0x33));
    p.drawRoundedRect(QRectF(11.5, 16.5, 19, 15), 6.0, 6.0);       // fist
    for (qreal x : kFingerX)                                       // knuckles, bulging up
        p.drawEllipse(QPointF(x, 17.5), 2.8, 2.8);
    fillCapsule(p, QPointF(13.5, 26), QPointF(23, 26), 5.2);       // thumb across the front
    for (qreal x : kGrooveX)
        clearGroove(p, QPointF(x, 13.8), QPointF(x, 21));
    clearGroove(p, QPointF(12, 23.0), QPointF(24, 23.0));          // thumb ↔ palm
    p.end();
    return QIcon(pm);
}

// Auto: a capital A inside a circular arrow — an arc left open at the top right, with an
// arrowhead riding tangent to the end that stops there.
QIcon autoIcon()
{
    return makeGlyph([](QPainter& p) {
        // kR is capped by the arrowhead, not the ring: the head's outward leg reaches about
        // 5 px past the tip, and must still clear the 40 px canvas edge.
        constexpr qreal kR = 13.0, kC = 20.0, kEnd = 20.0;   // radius, centre, arc end angle
        p.drawArc(QRectF(kC - kR, kC - kR, 2 * kR, 2 * kR),
                  80 * 16, 300 * 16);                        // open between 20° and 80°
        const qreal a = qDegreesToRadians(kEnd);
        const QPointF tip(kC + kR * std::cos(a), kC - kR * std::sin(a));
        // Travel at the end is counter-clockwise, so the head opens back at -70° ± 30°.
        for (qreal deg : {-40.0, -100.0}) {
            const qreal r = qDegreesToRadians(deg);
            p.drawLine(QLineF(tip, tip + QPointF(6.5 * std::cos(r), -6.5 * std::sin(r))));
        }
        QFont f = p.font();
        f.setPixelSize(18);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 0, 40, 40), Qt::AlignCenter, QStringLiteral("A"));
    });
}

// Trim: the piece that gets torn AWAY — a block with tear teeth along the edge the cut
// runs through, so the button shows what it removes rather than what survives. Only the
// "start piece of a vertical capture" case is drawn; the other three are that same
// drawing rotated and mirrored, which is what keeps the pair consistent when the axis
// flips (and impossible to get half-right).
QIcon tearIcon(bool start, bool horizontal)
{
    return makeGlyph([start, horizontal](QPainter& p) {
        if (horizontal) {                 // ↕ → ↔ : "down the canvas" becomes "to the right"
            p.translate(0, 40);
            p.rotate(-90);
        }
        if (!start) {                     // mirror: the block moves to the far side and its
            p.translate(0, 40);           // teeth to the near edge, facing the live region
            p.scale(1, -1);
        }
        // Sized to fill the canvas the way the tick and the cross do — the pair was drawn
        // small at first and read as a lesser control than its neighbours.
        constexpr qreal kL = 6, kR = 34, kTop = 4, kTear = 23, kTooth = 6.5;
        constexpr int kTeeth = 4;
        QPolygonF poly;
        poly << QPointF(kL, kTop) << QPointF(kR, kTop) << QPointF(kR, kTear);
        for (int i = 0; i < kTeeth; ++i)  // zigzag back along the torn edge
            poly << QPointF(kR - (kR - kL) * (i + 0.5) / kTeeth, kTear + kTooth)
                 << QPointF(kR - (kR - kL) * (i + 1.0) / kTeeth, kTear);
        // A MITRE join for this outline, unlike every other glyph on the bar: the round
        // join rolls the points off into a wave once the 40 px canvas is drawn at 24, and
        // being torn is the whole message of this edge. The teeth are deep for the same
        // reason — shallow ones survive the downscale as a ripple.
        QPen tear = p.pen();
        tear.setJoinStyle(Qt::MiterJoin);
        p.setPen(tear);
        p.drawPolygon(poly);              // closes up the far edge

        // ...and an ✕ through the block, so the piece drawn is unmistakably the one being
        // thrown away rather than the one being kept. Thinner than the outline so the
        // shape of the torn piece still leads. Symmetric, so it survives the mirror above.
        QPen x = p.pen();
        x.setWidthF(2.4);
        p.setPen(x);
        constexpr qreal kCx = (kL + kR) / 2, kCy = (kTop + kTear) / 2, kArm = 6;
        p.drawLine(QLineF(kCx - kArm, kCy - kArm, kCx + kArm, kCy + kArm));
        p.drawLine(QLineF(kCx + kArm, kCy - kArm, kCx - kArm, kCy + kArm));
    });
}

// Done: a tick.
QIcon doneIcon()
{
    return makeGlyph([](QPainter& p) {
        p.drawPolyline(QPolygon() << QPoint(8, 21) << QPoint(17, 30) << QPoint(32, 10));
    });
}

// Cancel: a cross.
QIcon cancelIcon()
{
    return makeGlyph([](QPainter& p) {
        p.drawLine(10, 10, 30, 30);
        p.drawLine(30, 10, 10, 30);
    });
}

}  // namespace

ScrollCaptureBar::ScrollCaptureBar(QWidget* parent) : QWidget(parent)
{
    m_accent = PrefsStyle::accentColor();
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("scrollBar"));

    // Margins + button size mirror PaintBar's row 1 exactly, so the two bars are the same
    // height when stacked.
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 6, 6, 6);
    row->setSpacing(3);

    m_size    = new QLabel(this);
    m_message = new QLabel(this);
    m_message->hide();

    m_dir       = new QPushButton(this);
    m_move      = new QPushButton(this);
    m_auto      = new QPushButton(this);
    m_trimStart = new QPushButton(this);
    m_trimEnd   = new QPushButton(this);
    m_done      = new QPushButton(this);
    m_cancel    = new QPushButton(this);
    m_move->setCheckable(true);
    m_auto->setCheckable(true);
    // m_dir is deliberately NOT checkable: neither axis is "the on state", so a checked
    // fill would be a lie. The icon carries it.
    applyMoveIcon();
    setDirection(false);                  // also draws the two trim glyphs for the axis
    m_auto->setIcon(autoIcon());
    m_done->setIcon(doneIcon());
    m_cancel->setIcon(cancelIcon());
    for (QPushButton* b : {m_dir, m_move, m_auto, m_trimStart, m_trimEnd, m_done, m_cancel}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setIconSize(QSize(24, 24));    // icon-only: the glyph carries the meaning, so it
        b->setFixedSize(30, 30);          // runs larger than row 1's 20 px in the same 30 px box
        b->installEventFilter(this);      // hover hint
    }
    m_done->setEnabled(false);            // nothing captured yet
    setTrimEnabled(false, false);         // ...and one frame has no side to cut

    // Dividers group the row the way PaintBar's row 1 does: readout | modes | trim | finish.
    auto makeSep = [this] {
        auto* f = new QFrame(this);
        f->setFrameShape(QFrame::VLine);
        f->setObjectName(QStringLiteral("sep"));
        return f;
    };

    row->addWidget(m_size);
    row->addStretch(0);
    row->addSpacing(8);          // breathing room between the readout and the divider
    row->addWidget(makeSep());
    row->addWidget(m_dir);
    row->addWidget(m_move);
    row->addWidget(m_auto);
    // The trim pair is destructive, unlike everything left of it, and it is not the way
    // out of the session either — so it is fenced off from both neighbours.
    row->addWidget(makeSep());
    row->addWidget(m_trimStart);
    row->addWidget(m_trimEnd);
    row->addWidget(makeSep());
    row->addWidget(m_done);
    row->addWidget(m_cancel);
    // The prompt trails the buttons rather than sitting between the readout and them, so a
    // message appearing never shifts the controls sideways under the pointer.
    row->addSpacing(8);
    row->addWidget(m_message);

    connect(m_dir, &QPushButton::clicked, this, [this] {
        setDirection(!m_horizontal);
        emit directionToggled(m_horizontal);
    });
    connect(m_move, &QPushButton::toggled, this, [this](bool on) {
        applyMoveIcon();                   // open palm -> fist
        emit moveModeToggled(on);
    });
    connect(m_auto,   &QPushButton::toggled, this, &ScrollCaptureBar::autoScrollToggled);
    connect(m_trimStart, &QPushButton::clicked, this, &ScrollCaptureBar::trimStartRequested);
    connect(m_trimEnd,   &QPushButton::clicked, this, &ScrollCaptureBar::trimEndRequested);
    connect(m_done,   &QPushButton::clicked, this, &ScrollCaptureBar::doneRequested);
    connect(m_cancel, &QPushButton::clicked, this, &ScrollCaptureBar::cancelRequested);

    setStyleSheet(barQss());
    retranslate();
}

QString ScrollCaptureBar::barQss() const
{
    QString q = QStringLiteral(
        "QWidget#scrollBar{background:#f2f2f2;border:1px solid #c4c4c4;border-radius:6px;}"
        "QPushButton{background:transparent;border:1px solid transparent;border-radius:4px;"
        "padding:0;color:#222;}"
        "QPushButton:hover{background:#e3e3e3;}"
        "QPushButton:pressed{background:#d4d4d4;border:1px solid #2d7dff;}"
        "QPushButton:disabled{color:#aaa;}"
        "QPushButton:checked{background:#d4d4d4;border:1px solid #2d7dff;}"
        "QLabel{color:#333;font-size:16px;}"
        "QLabel[warn=\"true\"]{color:#c0392b;}"
        "QFrame#sep{color:#cfcfcf;}");
    q.replace(QStringLiteral("#2d7dff"),
              (m_accent.isValid() ? m_accent : QColor(0x2d, 0x7d, 0xff)).name(),
              Qt::CaseInsensitive);
    return q;
}

void ScrollCaptureBar::setAccent(const QColor& c)
{
    m_accent = c;
    setStyleSheet(barQss());
}

void ScrollCaptureBar::retranslate()
{
    // Icon-only buttons: the tooltip is the only place the action is named, so the Move and
    // Auto tips lead with the name.
    m_move->setToolTip(I18n::instance().t(QStringLiteral("scroll.move.tip")));
    m_auto->setToolTip(I18n::instance().t(QStringLiteral("scroll.auto.tip")));
    m_done->setToolTip(I18n::instance().t(QStringLiteral("scroll.done")));
    m_cancel->setToolTip(I18n::instance().t(QStringLiteral("scroll.cancel")));
    setDirection(m_horizontal);           // its tooltip is one of two, by state
    applyTrimHints();                     // ...and each trim button's is one of three
    setCapturedSize(m_w, m_h);            // re-render the size with the new locale
}

void ScrollCaptureBar::applyMoveIcon()
{
    m_move->setIcon(m_move->isChecked() ? fistIcon() : palmOpenIcon());
}

// The same QToolTip box row 1 shows, popped by hand. Qt's automatic path is not equivalent
// here for two reasons: it waits out the hover delay, and this bar spends the whole session
// floating over a LIVE target app that keeps the foreground — the case Qt's tooltip machinery
// is least willing to fire in. Showing it on Enter sidesteps both.
void ScrollCaptureBar::showHint(QWidget* w) const
{
    if (!w || w->toolTip().isEmpty())
        return;
    // Below the button. When the bar sits under the selection — its usual place — that is
    // the direction pointing AWAY from the capture region, which matters: the tooltip is a
    // top-level window of Qt's own and does not carry the bar's WDA_EXCLUDEFROMCAPTURE, so
    // one overlapping the region during a grab would be stitched into the image.
    QToolTip::showText(w->mapToGlobal(QPoint(0, w->height() + 4)), w->toolTip(), w);
}

bool ScrollCaptureBar::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Enter)
        showHint(qobject_cast<QWidget*>(watched));
    else if (event->type() == QEvent::Leave)
        QToolTip::hideText();
    return QWidget::eventFilter(watched, event);
}

void ScrollCaptureBar::setDirection(bool horizontal)
{
    m_horizontal = horizontal;
    m_dir->setIcon(horizontal ? axisHorizontalIcon() : axisVerticalIcon());
    applyDirectionHint();
    // The trim glyphs are statements about the axis too — top/bottom or left/right — so
    // they and their hints follow it.
    m_trimStart->setIcon(tearIcon(/*start*/ true, horizontal));
    m_trimEnd->setIcon(tearIcon(/*start*/ false, horizontal));
    applyTrimHints();
}

void ScrollCaptureBar::setTrimEnabled(bool start, bool end)
{
    m_trimStart->setEnabled(start);
    m_trimEnd->setEnabled(end);
    applyTrimHints();          // a side with nothing on it says so instead of offering
}

// Same reasoning as applyDirectionHint(): Qt shows tooltips for disabled widgets, so a
// greyed trim button must explain why rather than go on naming an action it will not do.
void ScrollCaptureBar::applyTrimHints()
{
    auto hint = [](QPushButton* b, const QString& key) {
        b->setToolTip(I18n::instance().t(
            b->isEnabled() ? key : QStringLiteral("scroll.trim.none")));
    };
    hint(m_trimStart, m_horizontal ? QStringLiteral("scroll.trim.left")
                                   : QStringLiteral("scroll.trim.top"));
    hint(m_trimEnd,   m_horizontal ? QStringLiteral("scroll.trim.right")
                                   : QStringLiteral("scroll.trim.bottom"));
}

void ScrollCaptureBar::setDirectionEnabled(bool on)
{
    m_dir->setEnabled(on);
    applyDirectionHint();          // a locked button must stop offering the click
}

// Four texts, not two: the enabled pair invites the click, the locked pair only states the
// axis. Qt shows tooltips for DISABLED widgets too, so without this the greyed-out button
// goes on promising "click for horizontal" and does nothing when you do.
void ScrollCaptureBar::applyDirectionHint()
{
    const QString was = m_dir->toolTip();
    const QString key = m_dir->isEnabled()
                            ? (m_horizontal ? QStringLiteral("scroll.direction.horizontal")
                                            : QStringLiteral("scroll.direction.vertical"))
                            : (m_horizontal ? QStringLiteral("scroll.direction.horizontal_locked")
                                            : QStringLiteral("scroll.direction.vertical_locked"));
    m_dir->setToolTip(I18n::instance().t(key));
    // The hint can be on screen as this runs — the click flipped the axis under the pointer,
    // or a frame locked the button while it was hovered. Replace it rather than leave it
    // contradicting the button. Matching the old text keeps us off another button's hint.
    if (QToolTip::isVisible() && QToolTip::text() == was)
        showHint(m_dir);
}

void ScrollCaptureBar::setCapturedSize(int w, int h)
{
    m_w = w;
    m_h = h;
    // Keyed rather than hardcoded because the separator and the digit grouping are a
    // locale's business, even though there is no prose here.
    m_size->setText(I18n::instance().t(QStringLiteral("scroll.size"))
                        .arg(QString::number(w), QString::number(h)));
}

void ScrollCaptureBar::setMessage(const QString& text, bool warning)
{
    m_message->setText(text);
    m_message->setProperty("warn", warning);
    // A dynamic property only changes the painted colour after the style re-evaluates.
    m_message->style()->unpolish(m_message);
    m_message->style()->polish(m_message);
    m_message->setVisible(!text.isEmpty());
}

void ScrollCaptureBar::setDoneEnabled(bool on)
{
    m_done->setEnabled(on);
}

void ScrollCaptureBar::setAutoChecked(bool on)
{
    if (m_auto->isChecked() == on)
        return;
    m_auto->blockSignals(true);            // silent: the caller already owns the state
    m_auto->setChecked(on);
    m_auto->blockSignals(false);
}

void ScrollCaptureBar::setMoveChecked(bool on)
{
    if (m_move->isChecked() == on)
        return;
    m_move->blockSignals(true);
    m_move->setChecked(on);
    m_move->blockSignals(false);
    applyMoveIcon();          // blockSignals also muted the connection that swaps the glyph
}
