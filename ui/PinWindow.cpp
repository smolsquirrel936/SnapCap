#include "ui/PinWindow.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QEvent>
#include <QTimer>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QScreen>
#include <QWindow>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QConicalGradient>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>

#include "core/PinModel.h"
#include "core/PinManager.h"
#include "core/GroupRegistry.h"
#include "core/ClipboardContent.h"
#include "core/ImageExport.h"
#include "core/codec/AnimatedImage.h"
#include "core/codec/PdfEncoder.h"
#include "core/ManualSave.h"
#include "core/Settings.h"
#include "core/i18n/I18n.h"
#include "ui/PinEditController.h"
#include "ui/SaveFeedback.h"
#include "platform/platform.h"

QList<PinWindow*> PinWindow::s_windows;
platform::IClipboard* PinWindow::s_clipboard = nullptr;
platform::IOcr* PinWindow::s_ocr = nullptr;
clip::PasteOptions PinWindow::s_pasteOptions;
PinWindow* PinWindow::s_editing = nullptr;

namespace {
constexpr qreal kScaleStep = 1.1;
constexpr qreal kOpacityStep = 0.08;

// Neon outline / drop-shadow geometry. The image is inset by kMargin on every side so
// the focused glow can be stroked — and an unfocused drop shadow blurred — in the
// transparent margin without covering any image pixel. kMargin must exceed the shadow
// blur + drop so the softest layer still fits inside the window.
constexpr int kMargin = 16;        // logical px reserved around the image
constexpr int kRainbowMs = 100;    // 0.1s per colour during the creation sweep
constexpr qreal kFlashOpacity = 0.55;   // window opacity while flashing (translucentWhenFlashing)

// Unfocused drop-shadow tuning (Paste → Display → Window shadow).
constexpr int   kShadowBlur = 5;   // logical px the shadow spreads past the image edge (unfocused)
constexpr qreal kShadowDrop = 3.0; // downward offset
constexpr qreal kShadowAlpha = 0.24;

// Fallback fast-thumbnail box when no Settings are available (px).
constexpr int kThumbFallback = 75;

// Neon-style ROYGBIV palette for the just-created sweep (red → violet, rainbow order).
const QColor kRainbow[] = {
    QColor(255,  0,  64),   // neon red
    QColor(255,106,  0),    // neon orange
    QColor(255,230,  0),    // neon yellow
    QColor( 57,255, 20),    // neon green
    QColor(  0,179,255),    // neon blue
    QColor( 77,  0,255),    // neon indigo
    QColor(180,  0,255),    // neon violet
};
constexpr int kRainbowCount = int(sizeof(kRainbow) / sizeof(kRainbow[0]));

// How far (logical px) from an image edge a press still grabs that resize grip.
// The band straddles the edge so it reaches a little into the neon-border margin.
constexpr qreal kGripBand = 9.0;

// Cursor travel (manhattan px) that turns a press into a drag rather than a click. Below
// this the pin doesn't move and the release counts as a clean click (deselects all pins).
constexpr int kClickSlop = 4;
}

PinWindow::PinWindow(PinModel* model, PinManager* manager, Settings* settings,
                     GroupRegistry* groups, QWidget* parent)
    : QWidget(parent), m_model(model), m_manager(manager), m_settings(settings), m_groups(groups)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
    s_windows.append(this);

    if (m_manager)
        connect(m_manager, &PinManager::hiddenChanged, this, &PinWindow::applyHidden);

    if (m_settings) {
        // Cap the pin size up front (showAt → refreshFromModel picks up the clamped scale).
        m_model->setMaxLongSide(
            m_settings->value(QStringLiteral("paste/display/maxWindowSize"), 12000).toInt());
        // The global default seeds the pin — but never overrules a pin that set its own
        // Zoom ▸ Smoothing (a restored pin carries that override in from fromJson()).
        if (!m_model->smoothScalingOverridden())
            m_model->setSmoothScaling(
                m_settings->value(QStringLiteral("paste/behavior/smoothScaling"), true).toBool());
        connect(m_settings, &Settings::changed, this, [this](const QString& key) {
            if (key == QStringLiteral("paste/display/maxWindowSize"))
                applyDisplaySettings();                        // re-clamp + resize
            else if (key == QStringLiteral("paste/display/windowShadow")
                  || key == QStringLiteral("paste/display/shadowColor"))
                update();                                      // repaint the shadow only
            else if (key == QStringLiteral("paste/behavior/smoothScaling")
                     && !m_model->smoothScalingOverridden()) {
                m_model->setSmoothScaling(m_settings->value(key, true).toBool());
                refreshFromModel();                            // recompose with the new filter
            }
        });
    }

    m_rainbowTimer = new QTimer(this);
    m_rainbowTimer->setInterval(kRainbowMs);
    connect(m_rainbowTimer, &QTimer::timeout, this, [this] {
        if (++m_rainbowStep >= kRainbowCount) {   // sweep done → settle to the state colour
            m_rainbowStep = -1;
            m_rainbowTimer->stop();
            setWindowOpacity(m_model->opacity());  // undo the translucent-while-flashing dip
            update();
            emit flashFinished();                  // a listener may tear this pin down
            return;
        }
        update();
    });

    // The animation clock. Single-shot and re-armed per tick, because every frame carries
    // its own delay; updatePlayback() decides whether it runs at all.
    m_frameTimer = new QTimer(this);
    m_frameTimer->setSingleShot(true);
    connect(m_frameTimer, &QTimer::timeout, this, [this] {
        m_model->advanceFrame();
        refreshFromModel();          // recomposes the new frame and re-arms the timer
    });
}

int PinWindow::borderMargin() { return kMargin; }

// TODO(theme): see header — neon-cyan placeholder until the theme manager exists.
QColor PinWindow::themeColor() { return QColor(0, 240, 255); }

void PinWindow::detach()
{
    // Both clocks dereference m_model on every tick (advanceFrame / opacity), and a hidden
    // widget keeps its timers running — there is no hideEvent that would stop them.
    m_frameTimer->stop();
    m_rainbowTimer->stop();
    // The two inbound signals that reach a hidden pin: hide-all and a live settings change.
    if (m_manager)
        disconnect(m_manager, nullptr, this, nullptr);
    if (m_settings)
        disconnect(m_settings, nullptr, this, nullptr);
    // Deleted, not closed: closeToolbar() bakes an open text box into the model.
    delete m_edit;
    m_edit = nullptr;
    if (s_editing == this)
        s_editing = nullptr;
    // Off the list before the model dies, so no PinWindow::windows() caller can reach a pin
    // whose model is gone (the destructor is too late — it only runs on the next turn).
    s_windows.removeAll(this);
    m_model = nullptr;
}

PinWindow::~PinWindow()
{
    // Tear the floating bars down with the pin. Deleted directly rather than via
    // closeToolbar(): this pin is being destroyed, so an open text box is discarded
    // instead of baked (committing would touch a half-dead widget).
    if (s_editing == this)
        s_editing = nullptr;
    delete m_edit;
    m_edit = nullptr;
    s_windows.removeAll(this);
}

QTransform PinWindow::sourceToWidget() const
{
    // source px --sourceToComposed--> composed px --/dpr--> logical --+kMargin--> widget.
    // (QTransform's own calls apply inner-first, so the two outer steps are built here
    // and the model's transform is multiplied in ahead of them.)
    qreal dpr = m_composed.devicePixelRatio();
    if (dpr <= 0.0)
        dpr = 1.0;
    QTransform t;
    t.translate(kMargin, kMargin);
    t.scale(1.0 / dpr, 1.0 / dpr);
    return m_model->sourceToComposed() * t;
}

void PinWindow::toggleToolbar()
{
    if (m_edit) {
        closeToolbar();
        return;
    }
    if (s_editing && s_editing != this)
        s_editing->closeToolbar();       // only one toolbar at a time — it follows the pin
    m_edit = new PinEditController(this, m_settings);
    s_editing = this;
    connect(m_edit, &PinEditController::closeRequested, this, &PinWindow::closeToolbar);
    updatePlayback();                    // freeze an animation: you can't draw on a moving image
    update();                            // the pin keeps its focused look while editing
}

void PinWindow::closeToolbar()
{
    if (!m_edit)
        return;
    PinEditController* e = m_edit;
    e->finishEditing();                  // bake an open text box rather than eat it
    m_edit = nullptr;                    // clear before deleting: paintEvent reads it
    if (s_editing == this)
        s_editing = nullptr;
    delete e;
    unsetCursor();
    setCursor(Qt::OpenHandCursor);
    updatePlayback();                    // done drawing → an animation resumes
    update();
}

void PinWindow::showAt(const QPoint& globalTopLeft, bool animate)
{
    // Latch the dpr from the monitor this pin will land on. refreshFromModel() runs before
    // move(), so devicePixelRatioF() would still report the old/primary screen here — a pin
    // created on a differently-scaled monitor would latch the wrong scale for its whole life.
    if (m_model->sourceDpr() <= 0.0)
        if (const QScreen* s = QGuiApplication::screenAt(globalTopLeft))
            m_model->setSourceDpr(s->devicePixelRatio());
    refreshFromModel();
    move(globalTopLeft);
    show();
    createWinId();
    // Recompose whenever the pin lands on a different monitor. QEvent::DevicePixelRatioChange
    // alone proved unreliable (it did not cover every crossing direction), so hook the window's
    // own screenChanged too — whichever arrives first, the deferred refresh runs once.
    if (QWindow* h = windowHandle(); h && !m_screenHooked) {
        m_screenHooked = true;
        connect(h, &QWindow::screenChanged, this, [this] { refreshAfterScreenChange(); });
    }
    if (!m_surface)
        m_surface = platform::createPinSurface(winId());
    if (m_surface)
        m_surface->setAlwaysOnTop(true);
    raise();
    // Paste → Behavior → "Activate": a freshly created pin (animate=true) only grabs focus
    // when the setting is on; a restored pin (animate=false) keeps its prior activation.
    bool activate = true;
    if (animate && m_settings)
        activate = m_settings->value(QStringLiteral("paste/behavior/pasteActivate"), true).toBool();
    if (activate)
        activateWindow();
    // Again now that the window is actually visible: the refreshFromModel() above ran before
    // show(), so a pin that arrived already animated (a session restore) saw isVisible() false.
    updatePlayback();
    if (animate)
        startCreationAnimation();
}

void PinWindow::showWithContentAt(const QPoint& contentTopLeftGlobal)
{
    // Offset by the border margin so the image content — not the bordered window —
    // sits at the requested global top-left (i.e. exactly where it was captured).
    showAt(contentTopLeftGlobal - QPoint(kMargin, kMargin));
}

void PinWindow::startCreationAnimation()
{
    // Paste → Behavior → "Flash a newly pasted pin": when off, a new pin appears with no
    // attention sweep (it still gets the static focus outline if activated). Only the
    // *creation* flash is gated here — flash() itself serves other triggers with their
    // own settings.
    if (m_settings
        && !m_settings->value(QStringLiteral("paste/behavior/pasteFlash"), true).toBool())
        return;
    flash();
}

void PinWindow::flash()
{
    // Paste → Behavior → "Translucent when flashing": dim the window for the duration of the
    // sweep, restored to the model's opacity when the rainbow timer settles.
    if (m_settings
        && m_settings->value(QStringLiteral("paste/behavior/translucentWhenFlashing"), true).toBool())
        setWindowOpacity(qMin(m_model->opacity(), kFlashOpacity));
    m_rainbowStep = 0;           // sweep red → violet once, then settle
    m_rainbowTimer->start();
    update();
}

void PinWindow::refreshAfterScreenChange()
{
    // Deferred to the next event-loop turn on purpose: Windows applies its own suggested
    // window rect for the new DPI *around* the notification, so recomposing inline can be
    // overwritten by that resize and leave the image clipped again. Running afterwards makes
    // our size the final word. Coalesced by the timer, so both hooks firing costs one refresh.
    QTimer::singleShot(0, this, [this] {
        if (isVisible())
            refreshFromModel();
    });
}

void PinWindow::refreshFromModel()
{
    const QPoint topLeft = isVisible() ? frameGeometry().topLeft() : QPoint();
    m_composed = m_model->compose();
    // Prefer the model's intrinsic source dpr (capture DPR for a snip) so the pin keeps
    // its true logical size on every monitor; fall back to this window's current screen
    // dpr for clipboard images that carry no intrinsic dpr — then latch that, so the pin
    // sizes the same way a snip-sourced one does instead of re-scaling per monitor.
    qreal dpr = m_model->sourceDpr();
    if (dpr <= 0.0)
        dpr = devicePixelRatioF();
    if (dpr <= 0.0)
        dpr = 1.0;
    m_model->setSourceDpr(dpr);
    m_composed.setDevicePixelRatio(dpr);

    const QSize logical = m_composed.deviceIndependentSize().toSize().expandedTo(QSize(1, 1));
    resize(logical + QSize(2 * kMargin, 2 * kMargin));   // room for the neon border
    if (isVisible())
        move(topLeft);                                   // zoom keeps the top-left corner fixed
    setWindowOpacity(m_model->opacity());
    updatePlayback();
    update();
}

void PinWindow::updatePlayback()
{
    // A hidden pin has nothing to show, and an open toolbar means the user is drawing on
    // this image — neither should keep recomposing frames underneath them.
    const bool play = m_model->isAnimated() && !m_paused && !m_edit && isVisible();
    if (!play) {
        m_frameTimer->stop();
        return;
    }
    m_frameTimer->start(m_model->currentDelayMs());
}

bool PinWindow::attachAnimation(PinModel* m, const QString& path, QWidget* warnParent)
{
    if (!m)
        return false;
    const anim::Animation a = anim::load(path);
    if (a.status == anim::Animation::Status::TooLarge && warnParent) {
        QMessageBox::warning(warnParent, TR("paste.anim.too_large.title"),
                             TR("paste.anim.too_large.text"));
    }
    if (!a.isValid())
        return false;
    m->setAnimation(a.frames, a.delaysMs);
    return true;
}

void PinWindow::applyHidden(bool hidden)
{
    setVisible(!hidden);
    updatePlayback();                  // a hidden pin's clock stops (and restarts on show)
    if (m_edit)
        m_edit->setBarsVisible(!hidden);   // the bars are separate windows — hide them too
    if (!hidden) {
        raise();
        if (m_surface)
            m_surface->setAlwaysOnTop(true);
    }
}

void PinWindow::moveEvent(QMoveEvent*)
{
    if (m_edit && !m_edit->drawing())      // mid-stroke the bars are deliberately hidden
        m_edit->reposition();
}

void PinWindow::resizeEvent(QResizeEvent*)
{
    if (m_edit && !m_edit->drawing())
        m_edit->reposition();
}

void PinWindow::setClickThrough(bool on)
{
    m_clickThrough = on;
    if (m_surface)
        m_surface->setClickThrough(on);
}

QVector<QRect> PinWindow::siblingGeometries() const
{
    QVector<QRect> geos;
    for (PinWindow* w : s_windows)
        if (w != this && w->isVisible())
            geos.append(w->frameGeometry());
    return geos;
}

void PinWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_composed.isNull())
        return;
    const QPointF imgTopLeft(kMargin, kMargin);
    const QRectF imageRect(imgTopLeft, m_composed.deviceIndependentSize());

    // Focused / dragging / just-created → glow outline. Unfocused → drop shadow (when the
    // Paste → Display setting is on), otherwise a flat, chrome-less image. An open
    // toolbar counts as focused: its bars are separate windows, so clicking one would
    // otherwise make the pin it belongs to look unfocused.
    const bool focused = (m_rainbowStep >= 0) || isActiveWindow() || m_dragging || m_edit;
    const bool outlined = focused || m_selected;   // multi-selected pins outline while unfocused too
    if (!outlined && !m_thumbnail && shadowEnabled())
        drawDropShadow(p, imageRect);           // painted behind the image

    p.drawImage(imgTopLeft, m_composed);

    // Committed annotations are already inside m_composed (PinModel::compose bakes them);
    // only the in-progress draft is drawn on top.
    if (m_edit)
        m_edit->paintDraft(p);

    if (m_thumbnail)
        drawSpectrumBorder(p, imageRect);       // rainbow border flags a fast thumbnail
    else if (outlined)
        drawNeonBorder(p, imageRect,
                       m_selected ? selectionColor() : outlineColor());
}

QColor PinWindow::outlineColor() const
{
    if (m_rainbowStep >= 0)                     // just created: sweeping the rainbow
        return kRainbow[qMin(m_rainbowStep, kRainbowCount - 1)];
    return themeColor();                         // focused / dragging
}

QColor PinWindow::selectionColor()
{
    return QColor(255, 176, 32);                 // amber — distinct from the neon-cyan focus glow
}

QList<PinWindow*> PinWindow::selectedWindows()
{
    QList<PinWindow*> out;
    for (PinWindow* w : s_windows)
        if (w->m_selected && w->isVisible())
            out.append(w);
    return out;
}

void PinWindow::clearSelection()
{
    for (PinWindow* w : s_windows)
        if (w->m_selected) {
            w->m_selected = false;
            w->update();
        }
}

void PinWindow::selectAllVisible()
{
    for (PinWindow* w : s_windows)
        if (w->isVisible() && !w->m_selected) {
            w->m_selected = true;
            w->update();
        }
}

QList<PinWindow*> PinWindow::opTargets()
{
    if (m_selected) {
        const QList<PinWindow*> sel = selectedWindows();
        if (!sel.isEmpty())
            return sel;
    }
    return { this };
}

void PinWindow::closeTargets(bool destroy)
{
    // close()/destroy() delete windows and mutate s_windows, so snapshot the models (and the
    // manager, since `this` may be one of the closed windows) before touching any of them.
    PinManager* mgr = m_manager;
    QList<PinModel*> models;
    for (PinWindow* w : opTargets())
        models.append(w->m_model);
    for (PinModel* mdl : models) {
        if (destroy) mgr->destroy(mdl);
        else         mgr->close(mdl);
    }
}

bool PinWindow::shadowEnabled() const
{
    return m_settings
        && m_settings->value(QStringLiteral("paste/display/windowShadow"), true).toBool();
}

QColor PinWindow::shadowColor() const
{
    if (m_settings) {
        const QString n =
            m_settings->value(QStringLiteral("paste/display/shadowColor")).toString();
        if (!n.isEmpty() && QColor::isValidColorName(n))
            return QColor(n);
    }
    return Qt::black;
}

void PinWindow::drawDropShadow(QPainter& p, const QRectF& imageRect) const
{
    // Fake a soft blur cheaply with the same concentric-layer trick as the neon border:
    // stack rounded rects from the faintest/outermost to the darkest/innermost, offset
    // slightly downward so the pin appears lifted off the desktop.
    p.setPen(Qt::NoPen);
    const QColor base = shadowColor();
    const QRectF r = imageRect.translated(0.0, kShadowDrop);
    for (int i = kShadowBlur; i >= 1; --i) {
        QColor c = base;
        c.setAlphaF(kShadowAlpha * (1.0 - qreal(i - 1) / kShadowBlur));
        p.setBrush(c);
        p.drawRoundedRect(r.adjusted(-i, -i, i, i), 3, 3);
    }
}

void PinWindow::drawNeonBorder(QPainter& p, const QRectF& imageRect, const QColor& c) const
{
    // Stroke the image-edge rect in concentric layers (faint + wide → bright + thin) so
    // it reads as a neon glow, drawn entirely outside the image so no pixel is covered.
    p.setBrush(Qt::NoBrush);
    struct Layer { qreal width; int alpha; };
    static const Layer layers[] = { {6.0, 55}, {4.0, 110}, {2.0, 255} };
    for (const Layer& l : layers) {
        QColor col = c;
        col.setAlpha(l.alpha);
        QPen pen(col, l.width);
        pen.setJoinStyle(Qt::MiterJoin);
        p.setPen(pen);
        const qreal off = l.width / 2.0;        // push the stroke fully outside the image
        p.drawRect(imageRect.adjusted(-off, -off, off, off));
    }
}

void PinWindow::drawSpectrumBorder(QPainter& p, const QRectF& imageRect) const
{
    // A rainbow spectrum ring (same ROYGBIV palette as the creation sweep) that stays lit
    // for as long as the pin is in fast-thumbnail mode, so a shrunk pin is unmistakable.
    QConicalGradient grad(imageRect.center(), 90.0);
    for (int i = 0; i < kRainbowCount; ++i)
        grad.setColorAt(qreal(i) / kRainbowCount, kRainbow[i]);
    grad.setColorAt(1.0, kRainbow[0]);          // close the loop seamlessly

    p.setBrush(Qt::NoBrush);
    struct Layer { qreal width; qreal opacity; };
    static const Layer layers[] = { {6.0, 0.30}, {4.0, 0.55}, {2.0, 1.0} };
    for (const Layer& l : layers) {
        QPen pen(QBrush(grad), l.width);        // gradient-stroked pen
        pen.setJoinStyle(Qt::MiterJoin);
        p.setOpacity(l.opacity);                // fake the faint→bright glow layering
        p.setPen(pen);
        const qreal off = l.width / 2.0;
        p.drawRect(imageRect.adjusted(-off, -off, off, off));
    }
    p.setOpacity(1.0);
}

void PinWindow::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::ActivationChange)
        update();                               // focused→theme / unfocused→grey
    else if (e->type() == QEvent::DevicePixelRatioChange)
        refreshAfterScreenChange();             // dragged onto a differently-scaled monitor:
                                                // Windows resizes the window under us, so
                                                // re-derive the image extent to match or the
                                                // widget clips the image
    QWidget::changeEvent(e);
}

PinWindow::Grip PinWindow::gripAt(const QPoint& p) const
{
    if (m_composed.isNull() || m_thumbnail)     // a fast thumbnail is a fixed-size preview:
        return Grip::None;                      // no grips → press = move, Shift-2×click reverts
    const qreal W = width() - 2.0 * kMargin;
    const qreal H = height() - 2.0 * kMargin;
    if (W <= 0 || H <= 0)
        return Grip::None;
    // The image rect inside the window (the neon border lives in the kMargin frame).
    const QRectF cr(kMargin, kMargin, W, H);
    // Shrink the band on tiny pins so a central move-zone always remains.
    const qreal band = qMin(kGripBand, qMin(W, H) / 3.0);

    const bool left   = qAbs(p.x() - cr.left())   <= band;
    const bool right  = qAbs(p.x() - cr.right())  <= band;
    const bool top    = qAbs(p.y() - cr.top())    <= band;
    const bool bottom = qAbs(p.y() - cr.bottom()) <= band;

    if (top && left)     return Grip::TopLeft;
    if (top && right)    return Grip::TopRight;
    if (bottom && left)  return Grip::BottomLeft;
    if (bottom && right) return Grip::BottomRight;
    if (left)   return Grip::Left;
    if (right)  return Grip::Right;
    if (top)    return Grip::Top;
    if (bottom) return Grip::Bottom;
    return Grip::None;
}

Qt::CursorShape PinWindow::cursorForGrip(Grip g)
{
    switch (g) {
        case Grip::Left:  case Grip::Right:       return Qt::SizeHorCursor;
        case Grip::Top:   case Grip::Bottom:      return Qt::SizeVerCursor;
        case Grip::TopLeft: case Grip::BottomRight:return Qt::SizeFDiagCursor;
        case Grip::TopRight: case Grip::BottomLeft:return Qt::SizeBDiagCursor;
        default:                                  return Qt::OpenHandCursor;
    }
}

void PinWindow::beginResize(Grip g, const QPoint& /*localPos*/)
{
    // Pick the anchor handle (stays fixed) and the grabbed handle (follows the cursor)
    // as fractions of the content rect, then resolve both to logical px at press time.
    QPointF anchorFrac, movingFrac;
    switch (g) {
        case Grip::Left:        anchorFrac = {1.0, 0.5}; movingFrac = {0.0, 0.5}; break;
        case Grip::Right:       anchorFrac = {0.0, 0.5}; movingFrac = {1.0, 0.5}; break;
        case Grip::Top:         anchorFrac = {0.5, 1.0}; movingFrac = {0.5, 0.0}; break;
        case Grip::Bottom:      anchorFrac = {0.5, 0.0}; movingFrac = {0.5, 1.0}; break;
        case Grip::TopLeft:     anchorFrac = {1.0, 1.0}; movingFrac = {0.0, 0.0}; break;
        case Grip::TopRight:    anchorFrac = {0.0, 1.0}; movingFrac = {1.0, 0.0}; break;
        case Grip::BottomLeft:  anchorFrac = {1.0, 0.0}; movingFrac = {0.0, 1.0}; break;
        case Grip::BottomRight: anchorFrac = {0.0, 0.0}; movingFrac = {1.0, 1.0}; break;
        default: return;
    }
    const qreal W = width() - 2.0 * kMargin;
    const qreal H = height() - 2.0 * kMargin;
    const QPointF contentTL = frameGeometry().topLeft() + QPoint(kMargin, kMargin);

    m_resizeGrip = g;
    m_resizeAnchorFrac = anchorFrac;
    m_resizeAnchorGlobal = contentTL + QPointF(anchorFrac.x() * W, anchorFrac.y() * H);
    m_resizeOrigVec = QPointF((movingFrac.x() - anchorFrac.x()) * W,
                              (movingFrac.y() - anchorFrac.y()) * H);
    m_resizeStartScale = m_model->scale();
    m_thumbnail = false;                 // a manual resize leaves thumbnail mode
    setCursor(cursorForGrip(g));
}

void PinWindow::updateResize(const QPoint& globalPos)
{
    const qreal denom = QPointF::dotProduct(m_resizeOrigVec, m_resizeOrigVec);
    if (denom <= 0.0)
        return;
    // Uniform scale ratio = projection of the cursor (relative to the fixed anchor)
    // onto the original anchor→handle vector. Works for edges and corners alike.
    const QPointF d = QPointF(globalPos) - m_resizeAnchorGlobal;
    qreal ratio = QPointF::dotProduct(d, m_resizeOrigVec) / denom;
    if (ratio < 0.001)
        ratio = 0.001;                   // never flip past the anchor

    m_model->setScale(m_resizeStartScale * ratio);
    refreshFromModel();                  // resizes the window (keeps its old top-left)

    // Re-place the window so the anchor handle stays pinned under its original point.
    const qreal W = width() - 2.0 * kMargin;
    const qreal H = height() - 2.0 * kMargin;
    const QPointF contentTL = m_resizeAnchorGlobal
        - QPointF(m_resizeAnchorFrac.x() * W, m_resizeAnchorFrac.y() * H);
    move((contentTL - QPointF(kMargin, kMargin)).toPoint());
}

void PinWindow::mousePressEvent(QMouseEvent* e)
{
    // With a tool selected the toolbar draws; with none it declines and the pin behaves
    // exactly as it always has (drag/resize/menu) — the snip overlay's read-only model.
    if (m_edit && m_edit->onPress(e->position().toPoint(), e->button(), e->modifiers())) {
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        if (e->modifiers() & Qt::ControlModifier) {  // Ctrl+click: toggle multi-select
            m_selected = !m_selected;
            update();
            return;
        }
        const Grip g = gripAt(e->position().toPoint());
        if (g != Grip::None) {                       // grab a border grip → resize
            beginResize(g, e->position().toPoint());
            return;
        }
        // A plain press starts a *potential* drag. A clean click (release without crossing
        // kClickSlop) collapses the whole selection to just this pin — deselect all, keep
        // focus here. A real drag cancels that: on a selected pin it group-moves the rest, so
        // record every selected pin's origin up front for followers to track this pin's delta.
        m_collapseOnClick = true;
        m_pressGlobal = e->globalPosition().toPoint();
        m_dragging = true;
        m_dragOffset = m_pressGlobal - frameGeometry().topLeft();
        if (m_selected)
            for (PinWindow* w : selectedWindows())
                w->m_dragStart = w->frameGeometry().topLeft();
        setCursor(Qt::ClosedHandCursor);
    } else if (e->button() == Qt::MiddleButton) {
        for (PinWindow* w : opTargets()) {           // reset size & opacity
            w->m_model->reset();
            w->m_thumbnail = false;
            w->refreshFromModel();
        }
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent* e)
{
    if (m_edit && m_edit->onMove(e->position().toPoint(), e->modifiers())) {
        e->accept();
        return;
    }
    if (m_resizeGrip != Grip::None) {
        updateResize(e->globalPosition().toPoint());
        return;
    }
    if (!m_dragging) {
        // A selected tool owns the cursor over the image; otherwise the grips do.
        const Qt::CursorShape tool =
            m_edit ? m_edit->cursorFor(e->position().toPoint()) : Qt::ArrowCursor;
        setCursor(tool != Qt::ArrowCursor ? tool
                                          : cursorForGrip(gripAt(e->position().toPoint())));
        return;
    }
    const QPoint cur = e->globalPosition().toPoint();
    if ((cur - m_pressGlobal).manhattanLength() < kClickSlop)
        return;                                             // still within click slop — don't move yet
    m_collapseOnClick = false;                              // crossed the slop → it's a drag, not a click
    QPoint tl = cur - m_dragOffset;
    if ((e->modifiers() & Qt::ShiftModifier) && m_manager)   // magnetic snap (lead pin)
        tl = m_manager->snapPosition(tl, frameGeometry().size(), siblingGeometries());
    if (m_selected) {                                        // group drag: carry the rest along
        const QPoint delta = tl - m_dragStart;
        for (PinWindow* w : selectedWindows())
            if (w != this)
                w->move(w->m_dragStart + delta);
    }
    move(tl);
}

void PinWindow::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_edit && m_edit->onRelease(e->position().toPoint())) {
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        if (m_resizeGrip != Grip::None) {
            m_resizeGrip = Grip::None;
            setCursor(cursorForGrip(gripAt(e->position().toPoint())));
            return;
        }
        m_dragging = false;
        if (m_collapseOnClick) {              // a clean click (no drag) → deselect all pins
            clearSelection();
            m_collapseOnClick = false;
        }
        setCursor(cursorForGrip(gripAt(e->position().toPoint())));
    }
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (e->modifiers() & Qt::ShiftModifier) {
        for (PinWindow* w : opTargets())
            w->toggleThumbnail();
        return;
    }
    // An active tool may own the click (number label: a fast second tap on +/− must not
    // close the pin), as it does for the wheel.
    if (m_edit && m_edit->onDoubleClick(e->position().toPoint()))
        return;
    closeTargets(/*destroy=*/false);      // double-click closes (recoverable)
}

void PinWindow::wheelEvent(QWheelEvent* e)
{
    const int dy = e->angleDelta().y();
    if (dy == 0)
        return;
    // With a tool active the wheel sizes the pen/eraser, as it does on the snip canvas;
    // with the toolbar closed (or no tool picked) it still zooms the pin.
    if (m_edit && m_edit->onWheel(dy > 0 ? 1 : -1)) {
        e->accept();
        return;
    }
    if (e->modifiers() & Qt::ControlModifier) {   // Ctrl+wheel = opacity, no reposition
        for (PinWindow* w : opTargets()) {
            w->m_model->adjustOpacity(dy > 0 ? kOpacityStep : -kOpacityStep);
            w->refreshFromModel();
        }
        e->accept();
        return;
    }

    // Paste → Behavior → "Zoom at mouse position": keep the point under the cursor fixed
    // while zooming; otherwise keep the window centre fixed. (refreshFromModel resizes
    // around the old top-left, so we re-anchor afterwards.)
    const bool atMouse = m_settings
        && m_settings->value(QStringLiteral("paste/behavior/zoomAtMouse"), false).toBool();
    const QSize  before = size();
    const QPoint tlBefore = frameGeometry().topLeft();
    const QPointF anchor = atMouse ? e->globalPosition()
                                   : QPointF(frameGeometry().center());

    m_model->scaleBy(dy > 0 ? kScaleStep : 1.0 / kScaleStep);
    refreshFromModel();

    const QSize after = size();
    if (after != before && before.width() > 0 && before.height() > 0) {
        const qreal rx = qreal(after.width())  / before.width();
        const qreal ry = qreal(after.height()) / before.height();
        move(QPoint(qRound(anchor.x() - (anchor.x() - tlBefore.x()) * rx),
                    qRound(anchor.y() - (anchor.y() - tlBefore.y()) * ry)));
    }
    // Group zoom: scale the rest of the selection by the same step, each about its own centre
    // (the zoom-at-mouse anchor is meaningful only for the pin under the cursor).
    if (m_selected) {
        for (PinWindow* w : selectedWindows()) {
            if (w == this)
                continue;
            const QPoint c = w->frameGeometry().center();
            w->m_model->scaleBy(dy > 0 ? kScaleStep : 1.0 / kScaleStep);
            w->refreshFromModel();
            w->move(c - QPoint(w->width() / 2, w->height() / 2));
        }
    }
    e->accept();
}

void PinWindow::keyPressEvent(QKeyEvent* e)
{
    const Qt::KeyboardModifiers m = e->modifiers();
    const bool ctrl = m & Qt::ControlModifier;
    const bool shift = m & Qt::ShiftModifier;
    const bool alt = m & Qt::AltModifier;

    // Space shows/hides the paint bar (Features.md §4, Common).
    if (e->key() == Qt::Key_Space && !m) {
        toggleToolbar();
        return;
    }
    // The open toolbar gets first refusal — notably Esc, which closes it before the pin
    // (Features.md §3: "Esc — hide paint bar, or close the window"), and Ctrl+Z/Ctrl+Y.
    if (m_edit && m_edit->onKey(e->key(), m))
        return;

    // Pin operations fan out over opTargets() — the whole multi-selection when this pin is in
    // it, else just this pin. Clipboard/preferences keys (V, C, P) stay single-pin on purpose.
    switch (e->key()) {
        case Qt::Key_1: for (PinWindow* w : opTargets()) { w->m_model->rotateCCW();      w->refreshFromModel(); } return;   // 1 = rotate left
        case Qt::Key_2: for (PinWindow* w : opTargets()) { w->m_model->rotateCW();       w->refreshFromModel(); } return;   // 2 = rotate right
        case Qt::Key_3: for (PinWindow* w : opTargets()) { w->m_model->flipHorizontal(); w->refreshFromModel(); } return;
        case Qt::Key_4: for (PinWindow* w : opTargets()) { w->m_model->flipVertical();   w->refreshFromModel(); } return;
        case Qt::Key_5: for (PinWindow* w : opTargets()) { w->m_model->toggleGrayscale();w->refreshFromModel(); } return;
        case Qt::Key_6: for (PinWindow* w : opTargets()) { w->m_model->toggleInvert();   w->refreshFromModel(); } return;

        case Qt::Key_A:
            if (ctrl) { selectAllVisible(); return; }   // Ctrl+A: select all visible pins
            break;

        case Qt::Key_Plus:
        case Qt::Key_Equal:
            for (PinWindow* w : opTargets()) {
                if (ctrl) w->m_model->adjustOpacity(kOpacityStep);
                else      w->m_model->scaleBy(kScaleStep);
                w->refreshFromModel();
            }
            return;
        case Qt::Key_Minus:
            for (PinWindow* w : opTargets()) {
                if (ctrl) w->m_model->adjustOpacity(-kOpacityStep);
                else      w->m_model->scaleBy(1.0 / kScaleStep);
                w->refreshFromModel();
            }
            return;

        case Qt::Key_Escape:
            closeTargets(/*destroy=*/shift);          // Shift = permanent, else recoverable
            return;
        case Qt::Key_W:
            if (ctrl) { closeTargets(/*destroy=*/false); return; }
            break;
        case Qt::Key_V:
            if (ctrl) { replaceFromClipboard(); return; }
            break;
        case Qt::Key_C:
            if (ctrl && alt)   { copyImageToClipboard(/*asFile=*/true); return; }   // Ctrl+Alt+C: copy as file
            if (ctrl && shift) { copyPlainText(); return; }
            break;
        case Qt::Key_P:
            if (ctrl && shift) { emit preferencesRequested(); return; }
            break;

        case Qt::Key_Left:  for (PinWindow* w : opTargets()) w->move(w->pos() + QPoint(-1, 0)); return;
        case Qt::Key_Right: for (PinWindow* w : opTargets()) w->move(w->pos() + QPoint(1, 0));  return;
        case Qt::Key_Up:    for (PinWindow* w : opTargets()) w->move(w->pos() + QPoint(0, -1)); return;
        case Qt::Key_Down:  for (PinWindow* w : opTargets()) w->move(w->pos() + QPoint(0, 1));  return;

        default: break;
    }
    QWidget::keyPressEvent(e);
}

void PinWindow::replaceFromClipboard()
{
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    if (!md)
        return;
    const clip::Content c = clip::classify(*md, s_pasteOptions);
    if (c.isValid()) {
        m_model->setSource(c.image, c.text);
        m_thumbnail = false;
        refreshFromModel();
    }
}

void PinWindow::copyPlainText()
{
    if (m_model->hasText())
        QGuiApplication::clipboard()->setText(m_model->sourceText());
}

void PinWindow::copyImageToClipboard(bool asFile)
{
    const QImage img = m_model->compose();
    if (img.isNull())
        return;
    if (!s_clipboard) {                 // no backend → plain Qt copy
        QGuiApplication::clipboard()->setImage(img);
        return;
    }
    platform::ImageClipboardOptions opt;
    if (m_settings) {
        opt.addFlatDib = m_settings->value(QStringLiteral("output/clipboard/dibImageWindows"),
                                           false).toBool();
        const QString mode = m_settings->value(QStringLiteral("output/clipboard/copyAsFile"),
                                               QStringLiteral("disable")).toString();
        opt.addFile = (mode == QLatin1String("always"))
            || (mode == QLatin1String("hotkey") && asFile);   // hotkey mode: only via Ctrl+Alt+C
        opt.quality = m_settings->value(QStringLiteral("output/imageQuality"), -1).toInt();
    }
    s_clipboard->setImage(img, opt);
}

void PinWindow::applyDisplaySettings()
{
    if (m_settings)
        m_model->setMaxLongSide(
            m_settings->value(QStringLiteral("paste/display/maxWindowSize"), 12000).toInt());
    refreshFromModel();                   // re-clamps the scale and resizes the window
}

void PinWindow::toggleThumbnail()
{
    // Paste → Behavior → "Reset on thumbnail": when on, entering thumbnail mode drops the
    // rotation, flips and opacity (leaving only the fit-scale) and restores the full prior
    // state on exit; when off, only the scale is swapped.
    const bool resetAll = !m_settings
        || m_settings->value(QStringLiteral("paste/behavior/resetOnThumbnail"), true).toBool();
    if (!m_thumbnail) {
        m_thumbSnapshot = m_model->toJson();   // full state to restore on exit
        m_preThumbScale = m_model->scale();
        if (resetAll)
            m_model->reset();                  // 0°, no flips, scale 1, opacity 1
        const QSize b = m_model->baseSize();
        int tw = kThumbFallback, th = kThumbFallback;
        if (m_settings) {
            tw = m_settings->value(QStringLiteral("paste/display/thumbWidth"), kThumbFallback).toInt();
            th = m_settings->value(QStringLiteral("paste/display/thumbHeight"), kThumbFallback).toInt();
        }
        if (b.width() > 0 && b.height() > 0 && tw > 0 && th > 0)   // fit inside the W×H box
            m_model->setScale(qMin(qreal(tw) / b.width(), qreal(th) / b.height()));
        m_thumbnail = true;
    } else {
        if (resetAll)
            m_model->fromJson(m_thumbSnapshot);   // restore rotation/flip/scale/opacity
        else
            m_model->setScale(m_preThumbScale);
        m_thumbnail = false;
    }
    refreshFromModel();
}

void PinWindow::saveImageAs()
{
    // Save what the pin shows (rotation/flips/scale baked in) — the same image its
    // "Copy image" puts on the clipboard. The name/format come from the user's
    // Output → Manual Save preferences, exactly as the snip overlay's Save does.
    const QImage img = m_model->compose();
    if (img.isNull())
        return;

    const QString fileName = ManualSave::suggestedName(m_settings);
    const QString suggested =
        QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(fileName);
    const QString path = QFileDialog::getSaveFileName(this, TR("paste.save_title"), suggested,
                                                      ManualSave::filterFor(fileName));
    if (path.isEmpty() || !SaveFeedback::confirmIcoDownscale(this, img, path))
        return;

    const int quality = m_settings
        ? m_settings->value(QStringLiteral("output/imageQuality"), -1).toInt()
        : -1;
    const bool dropAlpha = m_settings
        && m_settings->value(QStringLiteral("output/auto_alpha_channel_removal"), true).toBool();
    const PdfEncoder::PageMode pdfPage = PdfEncoder::pageModeFromString(
        m_settings ? m_settings->value(QStringLiteral("output/pdf/pageSize"), QString()).toString()
                   : QString());
    QString err;
    if (ImageExport::toFile(img, path, quality, dropAlpha, &err, pdfPage)) {
        ManualSave::rememberExtension(m_settings, path);
        m_model->setSourcePath(path);        // "View in folder" now has a file to reveal
    } else {
        SaveFeedback::warnSaveFailed(this, path, err);
    }
}

void PinWindow::replaceByFile()
{
    // Start in the pin's own folder when it has one, else where saves go by default.
    const QString dir = m_model->sourcePath().isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
        : QFileInfo(m_model->sourcePath()).path();
    const QString filter =
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp);;All files (*)");
    const QString path =
        QFileDialog::getOpenFileName(this, TR("paste.replace_title"), dir, filter);
    if (path.isEmpty())
        return;

    const QImage img(path);
    if (img.isNull())
        return;                          // unreadable / not an image → leave the pin as it was
    m_model->setSource(img);             // resets the transforms and clears the old source path
    m_model->setSourcePath(path);
    attachAnimation(m_model, path, this);   // an animated file plays instead of showing frame 0
    m_thumbnail = false;
    m_paused = false;
    refreshFromModel();
}

bool PinWindow::canViewInFolder() const
{
    const QString path = m_model->sourcePath();
    return !path.isEmpty() && QFileInfo::exists(path);
}

void PinWindow::viewInFolder()
{
    if (!canViewInFolder())
        return;
    const QString path = m_model->sourcePath();
#ifdef Q_OS_WIN
    // "explorer /select, <path>" highlights the file itself; QDesktopServices can only
    // open the containing folder. The two arguments must stay separate so a path with
    // spaces is still quoted correctly.
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).path()));
#endif
}

void PinWindow::contextMenuEvent(QContextMenuEvent* e)
{
    // OCR mode owns the pin as a text surface — swallow the context menu so a stray
    // right-click doesn't pop the pin menu over the selectable text.
    if (m_edit && m_edit->ocrActive()) {
        e->accept();
        return;
    }
    // The right-column single-key hints (C, S, W, Z…) are QAction shortcuts, which QMenu
    // right-aligns and triggers while the menu is open. The "Mnemonics on the context
    // menu" setting toggles whether those hints are shown at all.
    const bool mnemonics =
        !m_settings
        || m_settings->value(QStringLiteral("paste/display/mnemonics"), false).toBool();
    auto hint = [mnemonics](QAction* a, const QKeySequence& ks) {
        if (mnemonics)
            a->setShortcut(ks);
    };

    QMenu menu(this);

    QAction* copyImg = menu.addAction(TR("paste.menu.copy_image"));
    hint(copyImg, QKeySequence(Qt::Key_C));
    connect(copyImg, &QAction::triggered, this, [this] { copyImageToClipboard(); });

    QAction* saveAs = menu.addAction(TR("paste.menu.save_image_as"));
    hint(saveAs, QKeySequence(Qt::Key_S));
    connect(saveAs, &QAction::triggered, this, &PinWindow::saveImageAs);

    menu.addSeparator();

    QAction* toolbar = menu.addAction(TR("paste.menu.show_toolbar"));
    hint(toolbar, QKeySequence(Qt::Key_Space));
    toolbar->setCheckable(true);
    toolbar->setChecked(toolbarOpen());
    connect(toolbar, &QAction::triggered, this, &PinWindow::toggleToolbar);

    if (m_model->isAnimated()) {
        QAction* pause = menu.addAction(TR("paste.menu.pause"));
        pause->setCheckable(true);
        pause->setChecked(m_paused);
        connect(pause, &QAction::triggered, this, [this](bool on) {
            m_paused = on;
            updatePlayback();
        });
    }

    QAction* shadow = menu.addAction(TR("paste.menu.shadow"));
    hint(shadow, QKeySequence(Qt::Key_W));
    shadow->setCheckable(true);
    shadow->setChecked(shadowEnabled());
    connect(shadow, &QAction::triggered, this, [this](bool on) {
        if (m_settings)
            m_settings->setValue(QStringLiteral("paste/display/windowShadow"), on);
    });

    menu.addSeparator();

    // --- Zoom submenu ---
    QMenu* zoom = menu.addMenu(TR("paste.menu.zoom"));
    struct ZoomPreset { const char* key; qreal scale; };
    static const ZoomPreset kZoom[] = {
        {"paste.menu.zoom_33", 1.0 / 3.0}, {"paste.menu.zoom_50", 0.5},
        {"paste.menu.zoom_66", 2.0 / 3.0}, {"paste.menu.zoom_100", 1.0},
        {"paste.menu.zoom_200", 2.0},
    };
    for (const ZoomPreset& z : kZoom) {
        QAction* a = zoom->addAction(I18n::instance().t(QString::fromLatin1(z.key)));
        const qreal s = z.scale;
        a->setCheckable(true);
        a->setChecked(qAbs(m_model->scale() - s) < 1e-6);   // tick the pin's current zoom level
        connect(a, &QAction::triggered, this, [this, s] {
            m_thumbnail = false;
            m_model->setScale(s);
            refreshFromModel();
        });
    }
    zoom->addSeparator();
    QAction* smoothing = zoom->addAction(TR("paste.menu.zoom_smoothing"));
    smoothing->setCheckable(true);
    smoothing->setChecked(m_model->smoothScaling());
    connect(smoothing, &QAction::triggered, this, [this](bool on) {
        m_model->overrideSmoothScaling(on);   // per-pin: the global default no longer applies
        refreshFromModel();                   // recompose with the new filter
    });

    // --- Image processing submenu ---
    QMenu* proc = menu.addMenu(TR("paste.menu.image_processing"));
    struct ProcItem { const char* key; void (PinModel::*op)(); };
    static const ProcItem kProc[] = {
        {"paste.menu.rotate_left",  &PinModel::rotateCCW},
        {"paste.menu.rotate_right", &PinModel::rotateCW},
        {"paste.menu.flip_h",       &PinModel::flipHorizontal},
        {"paste.menu.flip_v",       &PinModel::flipVertical},
    };
    for (const ProcItem& it : kProc) {
        QAction* a = proc->addAction(I18n::instance().t(QString::fromLatin1(it.key)));
        const auto op = it.op;
        connect(a, &QAction::triggered, this, [this, op] {
            (m_model->*op)();
            refreshFromModel();
        });
    }

    // Color ops: toggles, unlike the geometry items above.
    proc->addSeparator();
    QAction* gray = proc->addAction(TR("paste.menu.grayscale"));
    gray->setCheckable(true);
    gray->setChecked(m_model->grayscale());
    connect(gray, &QAction::triggered, this, [this] {
        m_model->toggleGrayscale();
        refreshFromModel();
    });

    QAction* invert = proc->addAction(TR("paste.menu.invert"));
    invert->setCheckable(true);
    invert->setChecked(m_model->inverted());
    connect(invert, &QAction::triggered, this, [this] {
        m_model->toggleInvert();
        refreshFromModel();
    });

    menu.addSeparator();

    QAction* paste = menu.addAction(TR("paste.menu.paste"));
    hint(paste, QKeySequence(Qt::Key_P));
    connect(paste, &QAction::triggered, this, &PinWindow::replaceFromClipboard);

    QAction* replaceFile = menu.addAction(TR("paste.menu.replace_by_file"));
    hint(replaceFile, QKeySequence(Qt::Key_R));
    connect(replaceFile, &QAction::triggered, this, &PinWindow::replaceByFile);

    QMenu* group = menu.addMenu(TR("paste.menu.move_to_group"));
    hint(group->menuAction(), QKeySequence(Qt::Key_M));
    if (m_groups) {
        const int cur = m_model->group();
        QAction* ung = group->addAction(TR("paste.menu.group_ungrouped"));
        ung->setCheckable(true);
        ung->setChecked(cur == 0);
        connect(ung, &QAction::triggered, this,
                [this] { emit assignToGroupRequested(m_model, 0); });
        const auto groups = m_groups->groups();
        if (!groups.isEmpty())
            group->addSeparator();
        for (const GroupRegistry::Group& g : groups) {
            QAction* a = group->addAction(g.name);
            a->setCheckable(true);
            a->setChecked(cur == g.id);
            const int id = g.id;
            connect(a, &QAction::triggered, this,
                    [this, id] { emit assignToGroupRequested(m_model, id); });
        }
    } else {
        group->setEnabled(false);
    }

    // Enabled only once the pin has a file behind it (Save image as… / Replace by file…)
    // and that file is still there — a moved or deleted one greys the item out again.
    QAction* viewFolder = menu.addAction(TR("paste.menu.view_in_folder"));
    hint(viewFolder, QKeySequence(Qt::Key_V));
    viewFolder->setEnabled(canViewInFolder());
    connect(viewFolder, &QAction::triggered, this, &PinWindow::viewInFolder);

    menu.addSeparator();

    QAction* close = menu.addAction(TR("paste.menu.close"));
    hint(close, QKeySequence(Qt::Key_X));
    connect(close, &QAction::triggered, this, [this] { m_manager->close(m_model); });

    QAction* destroy = menu.addAction(TR("paste.menu.destroy"));
    hint(destroy, QKeySequence(Qt::Key_D));
    connect(destroy, &QAction::triggered, this, [this] { m_manager->destroy(m_model); });

    menu.addSeparator();

    // --- Info submenu (read-only): "W × H" of the composed image ---
    const QSize cs = m_model->composedSize();
    QMenu* info = menu.addMenu(QStringLiteral("%1 × %2").arg(cs.width()).arg(cs.height()));
    auto infoRow = [info](const QString& label, const QString& value) {
        QAction* a = info->addAction(label + QStringLiteral(":  ") + value);
        a->setEnabled(false);
    };
    const QString no = TR("paste.menu.info_no");
    const int gid = m_model->group();
    const QString gname = (gid != 0 && m_groups && !m_groups->name(gid).isEmpty())
                              ? m_groups->name(gid)
                              : TR("paste.menu.group_ungrouped");
    infoRow(TR("paste.menu.info_group"), gname);
    infoRow(TR("paste.menu.info_zoom"),  QStringLiteral("%1%").arg(qRound(m_model->scale() * 100)));
    infoRow(TR("paste.menu.info_rotation"), QStringLiteral("%1°").arg(m_model->rotation()));
    infoRow(TR("paste.menu.info_opacity"),  QStringLiteral("%1%").arg(qRound(m_model->opacity() * 100)));
    infoRow(TR("paste.menu.info_xmirror"), m_model->flippedH() ? TR("paste.menu.info_yes") : no);
    infoRow(TR("paste.menu.info_ymirror"), m_model->flippedV() ? TR("paste.menu.info_yes") : no);
    infoRow(TR("paste.menu.info_grayscale"), m_model->grayscale() ? TR("paste.menu.info_yes") : no);
    infoRow(TR("paste.menu.info_inverted"), m_model->inverted() ? TR("paste.menu.info_yes") : no);

    menu.exec(e->globalPos());
}
