#include "ui/ColorDialog.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QImage>
#include <QPixmap>
#include <QLinearGradient>
#include <QPolygon>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QFont>

#include "core/ColorPicker.h"
#include "core/Settings.h"
#include "core/i18n/I18n.h"
#include "ui/PrefsStyle.h"
#include "ui/WheelGuard.h"

// ===========================================================================
// File-local helper widgets (HSV square, value bar, alpha-aware preview).
// ===========================================================================
namespace {

QString swatchStyle(const QColor& c, bool selected, const QColor& accent)
{
    if (!c.isValid()) {
        // An EMPTY custom slot. It has to read as "nothing here", which a white box
        // stops doing on a dark dialog — there it looks like a slot holding white.
        // Light keeps its original values exactly; only dark gets the substitutes.
        return (PrefsStyle::effectiveScheme() == PrefsStyle::Scheme::Dark)
                   ? QStringLiteral("QPushButton{background:#1e2128;border:1px solid #3a4150;}")
                   : QStringLiteral("QPushButton{background:#fff;border:1px solid #b0b0b0;}");
    }
    return QStringLiteral("QPushButton{background:%1;border:%2;}")
        .arg(c.name(QColor::HexRgb),
             selected ? QStringLiteral("2px solid %1").arg(accent.name())
                      : QStringLiteral("1px solid #888"));
}

// A large crosshair cursor for the screen eyedropper (white outline + black core
// so it reads on any background; a center gap keeps the sampled pixel visible).
QCursor makeCrossCursor()
{
    const int S = 63, C = S / 2, gap = 4;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const auto cross = [&](const QColor& col, int w) {
        QPen pen(col); pen.setWidth(w); p.setPen(pen);
        p.drawLine(0, C, C - gap, C);   p.drawLine(C + gap, C, S, C);
        p.drawLine(C, 0, C, C - gap);   p.drawLine(C, C + gap, C, S);
    };
    cross(Qt::white, 3);
    cross(Qt::black, 1);
    p.end();
    return QCursor(pm, C, C);
}

} // namespace

// Hue (x) x saturation (y) square, shaded by the current value.
class ColorField : public QWidget {
    Q_OBJECT
public:
    explicit ColorField(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(256, 200);
        setCursor(Qt::CrossCursor);
    }
    void setHsv(int h, int s, int v) {
        if (m_h == h && m_s == s && m_v == v) return;
        if (m_v != v) m_cache = QImage();   // value affects the whole gradient
        m_h = h; m_s = s; m_v = v;
        update();
    }
signals:
    void picked(int hue, int sat);
protected:
    void paintEvent(QPaintEvent*) override {
        const int w = width(), h = height();
        if (m_cache.size() != size()) m_cache = QImage();
        if (m_cache.isNull()) {
            m_cache = QImage(w, h, QImage::Format_RGB32);
            for (int y = 0; y < h; ++y) {
                const int s = h > 1 ? 255 - (y * 255) / (h - 1) : 255;
                QRgb* line = reinterpret_cast<QRgb*>(m_cache.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const int hue = w > 1 ? (x * 359) / (w - 1) : 0;
                    line[x] = QColor::fromHsv(hue, s, m_v).rgb();
                }
            }
        }
        QPainter p(this);
        p.drawImage(0, 0, m_cache);
        const int mx = w > 1 ? (m_h * (w - 1)) / 359 : 0;
        const int my = h > 1 ? ((255 - m_s) * (h - 1)) / 255 : 0;
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::black, 1)); p.drawEllipse(QPoint(mx, my), 5, 5);
        p.setPen(QPen(Qt::white, 1)); p.drawEllipse(QPoint(mx, my), 4, 4);
    }
    void mousePressEvent(QMouseEvent* e) override { emitAt(e->pos()); }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) emitAt(e->pos());
    }
private:
    void emitAt(QPoint pos) {
        const int w = width(), h = height();
        const int x = qBound(0, pos.x(), w - 1), y = qBound(0, pos.y(), h - 1);
        emit picked(w > 1 ? (x * 359) / (w - 1) : 0,
                    h > 1 ? 255 - (y * 255) / (h - 1) : 255);
    }
    int m_h = 0, m_s = 0, m_v = 255;
    QImage m_cache;
};

// Vertical value/brightness bar for the current hue + saturation.
class ColorValueBar : public QWidget {
    Q_OBJECT
public:
    explicit ColorValueBar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedWidth(26);
        setMinimumHeight(200);
        setCursor(Qt::SizeVerCursor);
    }
    void setHsv(int h, int s, int v) {
        if (m_h == h && m_s == s && m_v == v) return;
        m_h = h; m_s = s; m_v = v; update();
    }
signals:
    void picked(int val);
protected:
    void paintEvent(QPaintEvent*) override {
        const int w = width(), h = height();
        const int barW = w - 7;          // leave room for the indicator triangle
        QPainter p(this);
        QLinearGradient g(0, 0, 0, h);
        g.setColorAt(0.0, QColor::fromHsv(m_h, m_s, 255));
        g.setColorAt(1.0, QColor::fromHsv(m_h, m_s, 0));
        p.fillRect(0, 0, barW, h, g);
        p.setPen(QColor(120, 120, 120));
        p.drawRect(0, 0, barW - 1, h - 1);
        const int y = h > 1 ? ((255 - m_v) * (h - 1)) / 255 : 0;
        p.setRenderHint(QPainter::Antialiasing);
        QPolygon tri; tri << QPoint(w, y - 5) << QPoint(w, y + 5) << QPoint(barW, y);
        p.setBrush(Qt::black); p.setPen(Qt::black); p.drawPolygon(tri);
    }
    void mousePressEvent(QMouseEvent* e) override { emitAt(e->pos()); }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) emitAt(e->pos());
    }
private:
    void emitAt(QPoint pos) {
        const int h = height();
        const int y = qBound(0, pos.y(), h - 1);
        emit picked(h > 1 ? 255 - (y * 255) / (h - 1) : 255);
    }
    int m_h = 0, m_s = 0, m_v = 255;
};

// Current-color swatch; draws a checkerboard behind the color so alpha shows.
class ColorPreview : public QWidget {
public:
    explicit ColorPreview(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(72, 80);
    }
    void setColor(const QColor& c) { m_c = c; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const int t = 8;
        for (int y = 0; y < height(); y += t)
            for (int x = 0; x < width(); x += t) {
                const bool alt = ((x / t) + (y / t)) & 1;
                p.fillRect(x, y, t, t, alt ? QColor(0xcc, 0xcc, 0xcc) : QColor(0xff, 0xff, 0xff));
            }
        p.fillRect(rect(), m_c);
        p.setPen(QColor(120, 120, 120));
        p.drawRect(0, 0, width() - 1, height() - 1);
    }
private:
    QColor m_c = Qt::black;
};

// Full-virtual-desktop eyedropper surface. It freezes every screen at start (so
// sampling is exact) but is itself transparent, so the color dialog stays visible
// underneath and updates live. Being a top-most window spanning the whole virtual
// desktop, it captures the cross cursor and mouse over *every* application — which
// a plain QWidget::grabMouse can't do on Windows (cross-window moves are only
// delivered while a button is held). Moving reports the color + cursor position;
// left-click commits, right-click/Esc cancels.
class ScreenPickOverlay : public QWidget {
    Q_OBJECT
public:
    ScreenPickOverlay(QWidget* parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                       Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint);
        setWindowModality(Qt::ApplicationModal);   // take input from the modal dialog
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setCursor(makeCrossCursor());

        QRect vd;
        for (QScreen* s : QGuiApplication::screens()) {
            Shot sh;
            sh.geo = s->geometry();
            const QPixmap pm = s->grabWindow(0);
            sh.img = pm.toImage();
            sh.dpr = pm.devicePixelRatio() > 0 ? pm.devicePixelRatio() : 1.0;
            m_shots.append(sh);
            vd = vd.united(sh.geo);
        }
        setGeometry(vd);
    }

signals:
    void moved(const QColor& c, QPoint global);
    void committed(const QColor& c);
    void cancelled();

protected:
    void showEvent(QShowEvent*) override {
        raise(); activateWindow(); setFocus(); grabKeyboard();
        emit moved(colorAt(QCursor::pos()), QCursor::pos());   // seed the readout
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        const QPoint g = e->globalPosition().toPoint();
        emit moved(colorAt(g), g);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) emit committed(colorAt(e->globalPosition().toPoint()));
        else                               emit cancelled();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) emit cancelled();
        else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
            emit committed(colorAt(QCursor::pos()));
        else QWidget::keyPressEvent(e);
    }
    void paintEvent(QPaintEvent*) override {
        // A near-invisible fill keeps the whole surface hit-testable while staying
        // effectively transparent; sampling uses the frozen snapshot, never the
        // live screen, so this fill can't affect the picked color.
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 1));
    }

private:
    struct Shot { QRect geo; QImage img; qreal dpr = 1.0; };

    QColor colorAt(QPoint g) const {
        for (const Shot& s : m_shots)
            if (s.geo.contains(g)) {
                const int x = qBound(0, int((g.x() - s.geo.x()) * s.dpr), s.img.width() - 1);
                const int y = qBound(0, int((g.y() - s.geo.y()) * s.dpr), s.img.height() - 1);
                return s.img.pixelColor(x, y);
            }
        return QColor(Qt::black);
    }

    QVector<Shot> m_shots;
};

// ===========================================================================
// ColorDialog
// ===========================================================================

ColorDialog::ColorDialog(const QColor& initial, Settings* settings, QWidget* parent)
    : QDialog(parent), m_settings(settings), m_accent(PrefsStyle::accentColor())
{
    setObjectName(QStringLiteral("ColorDialog"));
    buildUi();
    WheelGuard::install(this);   // wheel edits a HSV/RGB box only once it is clicked
    loadCustomColors();
    connect(&I18n::instance(), &I18n::localeChanged, this, &ColorDialog::retranslate);
    setFromColor(initial.isValid() ? initial : QColor(Qt::black), Src::External);
    retranslate();
    resize(560, 380);
}

// A press that reaches the dialog itself landed on blank space — spin boxes,
// swatches, the HSV square and the buttons all accept their own presses. Drop
// focus there, so "click elsewhere to unfocus" works here as it does on a
// Preferences page (which gets it for free from its QScrollArea). This also
// disarms the wheel guard on whatever was focused.
void ColorDialog::mousePressEvent(QMouseEvent* e)
{
    if (QWidget* fw = focusWidget())
        fw->clearFocus();
    QDialog::mousePressEvent(e);
}

QColor ColorDialog::color() const
{
    QColor c = QColor::fromHsv(m_h, m_s, m_v);
    c.setAlpha(m_a);
    return c;
}

QColor ColorDialog::selectedColor() const { return color(); }

QColor ColorDialog::getColor(const QColor& initial, Settings* settings, QWidget* parent)
{
    // If the opener lives under a style sheet (the Preferences dialog applies
    // res/preferences.qss), that sheet would cascade into the picker and strip its
    // spin boxes' native up/down chevrons. Only in that case do we build it
    // parentless (kept application-modal via exec(), centred on the opener) so no
    // sheet leaks in. Otherwise we keep the real parent so the picker stacks correctly
    // above always-on-top overlays (e.g. the annotation paint bar) and stays visible.
    const bool styledAncestor = parent && !parent->window()->styleSheet().isEmpty();
    ColorDialog d(initial, settings, styledAncestor ? nullptr : parent);
    if (styledAncestor && parent) {
        d.adjustSize();
        const QRect pg = parent->window()->frameGeometry();
        d.move(pg.center().x() - d.width() / 2, pg.center().y() - d.height() / 2);
    }
    return d.exec() == QDialog::Accepted ? d.selectedColor() : QColor();
}

// --- UI construction --------------------------------------------------------

void ColorDialog::buildUi()
{
    auto* root = new QHBoxLayout(this);

    // Left column: basic grid, eyedropper, custom grid.
    auto* left = new QVBoxLayout;
    m_basicLabel = new QLabel; left->addWidget(m_basicLabel);
    left->addWidget(buildBasicGrid());
    m_pickBtn = new QPushButton;
    connect(m_pickBtn, &QPushButton::clicked, this, &ColorDialog::startPick);
    left->addWidget(m_pickBtn);
    m_cursorLabel = new QLabel; m_cursorLabel->setVisible(false); left->addWidget(m_cursorLabel);
    m_hintLabel   = new QLabel; m_hintLabel->setVisible(false);   left->addWidget(m_hintLabel);
    left->addStretch(1);
    m_customLabel = new QLabel; left->addWidget(m_customLabel);
    left->addWidget(buildCustomGrid());
    m_addBtn = new QPushButton;
    connect(m_addBtn, &QPushButton::clicked, this, &ColorDialog::addCurrentToCustom);
    left->addWidget(m_addBtn);

    // Right column: HSV square + value bar, then editors.
    auto* right = new QVBoxLayout;
    auto* topRow = new QHBoxLayout;
    m_field = new ColorField;
    m_valueBar = new ColorValueBar;
    topRow->addWidget(m_field, 1);
    topRow->addWidget(m_valueBar);
    right->addLayout(topRow, 1);
    right->addWidget(buildEditors());

    root->addLayout(left);
    root->addLayout(right, 1);

    connect(m_field, &ColorField::picked, this, [this](int h, int s) {
        m_h = h; m_s = s; syncControls(Src::Field);
    });
    connect(m_valueBar, &ColorValueBar::picked, this, [this](int v) {
        m_v = v; syncControls(Src::ValueBar);
    });
}

QWidget* ColorDialog::buildBasicGrid()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);
    g->setSpacing(3);
    g->setContentsMargins(0, 0, 0, 0);
    const QVector<QColor> palette = ColorPicker::basicPalette();
    for (int i = 0; i < palette.size(); ++i) {
        const QColor c = palette[i];
        auto* b = new QPushButton;
        b->setFixedSize(20, 20);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(swatchStyle(c, false, m_accent));
        connect(b, &QPushButton::clicked, this, [this, c] {
            QColor q = c; q.setAlpha(m_a);
            setFromColor(q, Src::Swatch);
        });
        m_swatches.append({b, c});
        g->addWidget(b, i / 8, i % 8);
    }
    return w;
}

QWidget* ColorDialog::buildCustomGrid()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);
    g->setSpacing(3);
    g->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 16; ++i) {
        auto* b = new QPushButton;
        b->setFixedSize(20, 20);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(swatchStyle(QColor(), false, m_accent));
        connect(b, &QPushButton::clicked, this, [this, i] {
            const QColor c = m_customColors.value(i);
            if (c.isValid()) { QColor q = c; q.setAlpha(m_a); setFromColor(q, Src::Swatch); }
        });
        m_customSlots.append(b);
        g->addWidget(b, i / 8, i % 8);
    }
    return w;
}

QWidget* ColorDialog::buildEditors()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    auto* row = new QHBoxLayout;
    m_preview = new ColorPreview;
    row->addWidget(m_preview);

    auto* grid = new QGridLayout;
    const auto mkSpin = [](int max) { auto* s = new QSpinBox; s->setRange(0, max); return s; };
    m_hue = mkSpin(359); m_sat = mkSpin(255); m_valv = mkSpin(255);
    m_red = mkSpin(255); m_green = mkSpin(255); m_blue = mkSpin(255); m_alpha = mkSpin(255);

    const auto addField = [&](int r, int c, const QString& key, QSpinBox* sp) {
        auto* lbl = new QLabel;
        grid->addWidget(lbl, r, c);
        grid->addWidget(sp, r, c + 1);
        m_labels.append({lbl, key});
    };
    addField(0, 0, QStringLiteral("color_picker.hue"),   m_hue);
    addField(1, 0, QStringLiteral("color_picker.sat"),   m_sat);
    addField(2, 0, QStringLiteral("color_picker.val"),   m_valv);
    addField(0, 2, QStringLiteral("color_picker.red"),   m_red);
    addField(1, 2, QStringLiteral("color_picker.green"), m_green);
    addField(2, 2, QStringLiteral("color_picker.blue"),  m_blue);
    addField(3, 2, QStringLiteral("color_picker.alpha"), m_alpha);

    auto* htmlLbl = new QLabel;
    m_html = new QLineEdit;
    grid->addWidget(htmlLbl, 4, 0);
    grid->addWidget(m_html, 4, 1, 1, 3);
    m_labels.append({htmlLbl, QStringLiteral("color_picker.html")});

    row->addLayout(grid, 1);
    v->addLayout(row, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    m_okBtn = new QPushButton; m_okBtn->setDefault(true);
    m_cancelBtn = new QPushButton;
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(m_okBtn);
    btnRow->addWidget(m_cancelBtn);
    v->addLayout(btnRow);

    // Editors -> state.
    const auto hsvChanged = [this] {
        m_h = m_hue->value(); m_s = m_sat->value(); m_v = m_valv->value();
        syncControls(Src::Hsv);
    };
    connect(m_hue,  &QSpinBox::valueChanged, this, hsvChanged);
    connect(m_sat,  &QSpinBox::valueChanged, this, hsvChanged);
    connect(m_valv, &QSpinBox::valueChanged, this, hsvChanged);
    const auto rgbChanged = [this] {
        setFromColor(QColor(m_red->value(), m_green->value(), m_blue->value(), m_a), Src::Rgb);
    };
    connect(m_red,   &QSpinBox::valueChanged, this, rgbChanged);
    connect(m_green, &QSpinBox::valueChanged, this, rgbChanged);
    connect(m_blue,  &QSpinBox::valueChanged, this, rgbChanged);
    connect(m_alpha, &QSpinBox::valueChanged, this, [this] {
        m_a = m_alpha->value(); syncControls(Src::Alpha);
    });
    connect(m_html, &QLineEdit::editingFinished, this, [this] {
        QColor c = ColorPicker::parseHtml(m_html->text());
        if (c.isValid()) { c.setAlpha(m_a); setFromColor(c, Src::Html); }
        else syncControls(Src::External);     // revert invalid text
    });
    return w;
}

// --- state sync -------------------------------------------------------------

void ColorDialog::setFromColor(const QColor& c, Src src)
{
    const QColor q = c.isValid() ? c : QColor(Qt::black);
    int h = q.hsvHue();
    if (h < 0) h = m_h;                 // grays have no hue — keep the current one
    m_h = h;
    m_s = q.hsvSaturation();
    m_v = q.value();
    m_a = q.alpha();
    syncControls(src);
}

void ColorDialog::syncControls(Src src)
{
    const QColor c = color();
    const QSignalBlocker b1(m_hue),  b2(m_sat),   b3(m_valv);
    const QSignalBlocker b4(m_red),  b5(m_green), b6(m_blue), b7(m_alpha);
    const QSignalBlocker b8(m_html);

    m_field->setHsv(m_h, m_s, m_v);
    m_valueBar->setHsv(m_h, m_s, m_v);
    m_preview->setColor(c);

    m_hue->setValue(m_h); m_sat->setValue(m_s); m_valv->setValue(m_v);
    m_red->setValue(c.red()); m_green->setValue(c.green()); m_blue->setValue(c.blue());
    m_alpha->setValue(m_a);
    if (src != Src::Html)
        m_html->setText(ColorPicker::toHtml(c));

    refreshSwatchSelection();
}

void ColorDialog::refreshSwatchSelection()
{
    const QRgb cur = color().rgb();
    for (const auto& [btn, col] : m_swatches)
        btn->setStyleSheet(swatchStyle(col, col.isValid() && col.rgb() == cur, m_accent));
    for (int i = 0; i < m_customSlots.size(); ++i) {
        const QColor col = m_customColors.value(i);
        m_customSlots[i]->setStyleSheet(swatchStyle(col, col.isValid() && col.rgb() == cur, m_accent));
    }
}

// --- custom colors ----------------------------------------------------------

void ColorDialog::loadCustomColors()
{
    m_customColors = QVector<QColor>(16);
    if (m_settings) {
        const QStringList list = m_settings->value(QStringLiteral("color/custom")).toStringList();
        for (int i = 0; i < list.size() && i < 16; ++i) {
            const QColor c = ColorPicker::parseHtml(list[i]);
            if (c.isValid()) m_customColors[i] = c;
        }
    }
    refreshSwatchSelection();
}

void ColorDialog::saveCustomColors()
{
    if (!m_settings) return;
    QStringList list;
    for (const QColor& c : m_customColors)
        list << (c.isValid() ? ColorPicker::toHtml(c) : QString());
    m_settings->setValue(QStringLiteral("color/custom"), list);
}

void ColorDialog::addCurrentToCustom()
{
    int idx = -1;
    for (int i = 0; i < m_customColors.size(); ++i)
        if (!m_customColors[i].isValid()) { idx = i; break; }
    if (idx < 0) { idx = m_customWrap; m_customWrap = (m_customWrap + 1) % 16; }
    m_customColors[idx] = color().toRgb();   // alpha dropped on save (HTML field)
    saveCustomColors();
    refreshSwatchSelection();
}

// --- eyedropper -------------------------------------------------------------

void ColorDialog::startPick()
{
    if (m_picking) return;
    m_picking = true;
    m_prePick = color();
    m_cursorLabel->setVisible(true);
    m_hintLabel->setVisible(true);

    m_overlay = new ScreenPickOverlay(this);
    connect(m_overlay, &ScreenPickOverlay::moved, this, [this](const QColor& c, QPoint g) {
        QColor q = c; q.setAlpha(m_a);
        setFromColor(q, Src::Pick);                       // live preview, dialog stays visible
        m_cursorLabel->setText(I18n::instance().t(
            QStringLiteral("color_picker.cursor_pos"),
            {{QStringLiteral("x"), g.x()}, {QStringLiteral("y"), g.y()}}));
    });
    connect(m_overlay, &ScreenPickOverlay::committed, this,
            [this](const QColor& c) { finishPick(c, true); });
    connect(m_overlay, &ScreenPickOverlay::cancelled, this,
            [this] { finishPick(QColor(), false); });
    m_overlay->show();
}

void ColorDialog::finishPick(const QColor& c, bool commit)
{
    if (!m_picking) return;
    m_picking = false;
    m_cursorLabel->setVisible(false);
    m_hintLabel->setVisible(false);
    if (m_overlay) {
        m_overlay->releaseKeyboard();
        m_overlay->close();
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }
    if (commit && c.isValid()) {
        QColor q = c; q.setAlpha(m_a);
        setFromColor(q, Src::Pick);
    } else {
        setFromColor(m_prePick, Src::External);
    }
}

// --- i18n -------------------------------------------------------------------

void ColorDialog::retranslate()
{
    auto& tr = I18n::instance();
    setWindowTitle(tr.t(QStringLiteral("color_picker.title")));
    m_basicLabel->setText(tr.t(QStringLiteral("color_picker.basic")));
    m_customLabel->setText(tr.t(QStringLiteral("color_picker.custom")));
    m_pickBtn->setText(tr.t(QStringLiteral("color_picker.pick_screen")));
    m_addBtn->setText(tr.t(QStringLiteral("color_picker.add_custom")));
    m_okBtn->setText(tr.t(QStringLiteral("color_picker.ok")));
    m_cancelBtn->setText(tr.t(QStringLiteral("color_picker.cancel")));
    m_hintLabel->setText(tr.t(QStringLiteral("color_picker.pick_hint")));
    for (const auto& [lbl, key] : m_labels)
        lbl->setText(tr.t(key));
}

#include "ColorDialog.moc"
