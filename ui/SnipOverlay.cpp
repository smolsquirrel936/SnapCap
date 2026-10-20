#include "ui/SnipOverlay.h"
#include "ui/SaveFeedback.h"
#include "ui/SnipScreenView.h"

#include <QGuiApplication>
#include <QDebug>
#include <QScreen>
#include <QStyleHints>
#include <QTimer>
#include <QScopeGuard>
#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTransform>
#include <QtMath>
#include <QFileDialog>
#include <QCheckBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <cmath>

#include "core/SelectionModel.h"
#include "core/ClickSlop.h"
#include "core/Settings.h"
#include "core/action/ActionRegistry.h"
#include "core/ImageExport.h"
#include "core/FilenameTemplate.h"
#include "core/ManualSave.h"
#include "core/HistoryStrip.h"
#include "core/ColorPicker.h"
#include "core/RoundedCorners.h"
#include "core/barcode/BarcodeHint.h"
#include <algorithm>
#include "core/i18n/I18n.h"
#include "core/annotation/AnnotationDocument.h"
#include "core/annotation/ShapeConstraints.h"
#include "ui/PaintBar.h"
#include "ui/OcrBar.h"
#include "ui/RecordBar.h"
#include "ui/RecordPreviewDialog.h"
#include "ui/ScrollCaptureBar.h"
#include "ui/ScrollStripWindow.h"
#include "core/scroll/ScrollStitcher.h"
#include "platform/platform.h"
#include <QThread>
#include <QPointer>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>

// File-local helpers for on-canvas non-text shape editing (see the ShapeEdit region).
namespace {
QPointF rotateAbout(const QPointF& pivot, qreal deg, const QPointF& pt)
{
    QTransform t;
    t.translate(pivot.x(), pivot.y());
    t.rotate(deg);
    t.translate(-pivot.x(), -pivot.y());
    return t.map(pt);
}
double distF(const QPoint& a, const QPoint& b)
{
    return std::hypot(double(a.x() - b.x()), double(a.y() - b.y()));
}
// Grip local offsets from the box centre (half-extents hw,hh). Index order:
// 0 TL, 1 Top, 2 TR, 3 Right, 4 BR, 5 Bottom, 6 BL, 7 Left. Corners are even.
void gripLocals(qreal hw, qreal hh, QPointF out[8])
{
    out[0] = {-hw, -hh}; out[1] = {0, -hh}; out[2] = {hw, -hh}; out[3] = {hw, 0};
    out[4] = {hw, hh};   out[5] = {0, hh};  out[6] = {-hw, hh}; out[7] = {-hw, 0};
}
} // namespace

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <dwmapi.h>
#endif
#include "ui/TextBoxEditor.h"
#include "platform/platform.h"

namespace {
// Refresh screenshot: how long a press must be held before it turns into a continuous
// refresh, and how often the desktop is re-grabbed while it lasts.
constexpr int kRefreshHoldMs = 400;
constexpr int kRefreshIntervalMs = 33;      // ~30 fps
constexpr int kSpinIntervalMs = 16;         // ~60 fps, icon animation only
}

SnipOverlay::SnipOverlay(platform::IElementDetect* detect,
                         platform::IScreenCapture* capture, QObject* parent)
    : QObject(parent), m_detect(detect), m_capture(capture)
{
    grabScreens();

    // Snapshot the cursor at the freeze moment, before we show the overlay (which
    // swaps the OS cursor to our crosshair). Cheap; composited into exports only if
    // enabled — see applySnipSettings()/compositeBase().
    if (capture) {
        const platform::CursorImage c = capture->grabCursor();
        m_cursorVisible = c.visible;
        m_cursorImage   = c.image;
        m_cursorHotspot = c.hotspot;
    }
    m_cursorPos = QCursor::pos();
    if (QScreen* cs = QGuiApplication::screenAt(m_cursorPos))
        m_cursorDpr = cs->devicePixelRatio() > 0.0 ? cs->devicePixelRatio() : 1.0;

    m_sel = new SelectionModel(this);
    m_sel->setBounds(m_layout.captureBounds());       // selection lives in capture px
    connect(m_sel, &SelectionModel::changed, this, [this] { updateAllViews(); });

    m_doc = new AnnotationDocument(this);
    // One hook for all 23 mutation sites: every add/undo/redo/move/restyle re-renders
    // the layer the recorder burns into its frames. No-op unless a recording is running.
    connect(m_doc, &AnnotationDocument::changed, this, [this] { pushRecordingOverlay(); });
    connect(m_doc, &AnnotationDocument::changed, this, [this] {
        syncPaintBarState();
        updateAllViews();
    });

    for (const ScreenShot& sh : m_shots)
        m_views.append(new SnipScreenView(this, sh));

    // Refresh screenshot (F5 / the readout-row button). A tap grabs once; the hold timer
    // turns a sustained press into a repeating grab until the press ends.
    m_refreshHold = new QTimer(this);
    m_refreshHold->setSingleShot(true);
    m_refreshHold->setInterval(kRefreshHoldMs);
    connect(m_refreshHold, &QTimer::timeout, this, [this] {
        if (m_refreshHeld) {
            m_refreshClock.restart();      // the icon spins from zero each time the loop starts
            m_refreshRepeat->start();
            m_refreshSpin->start();
        }
    });
    m_refreshRepeat = new QTimer(this);
    m_refreshRepeat->setInterval(kRefreshIntervalMs);
    connect(m_refreshRepeat, &QTimer::timeout, this, [this] { refreshFreeze(); });

    // The icon's spin gets its own, faster ticker: the grab loop's repaint cadence is
    // hostage to how long a full-desktop re-freeze takes, which makes the animation
    // stutter. This one repaints nothing but the button's own plate.
    m_refreshSpin = new QTimer(this);
    m_refreshSpin->setInterval(kSpinIntervalMs);
    connect(m_refreshSpin, &QTimer::timeout, this, [this] {
        for (SnipScreenView* v : m_views)
            v->updateRefreshButton();
    });
}

// Freeze every monitor. Each grab is DPI-correct per-screen; we store raw device
// pixels + the explicit dpr (image dpr reset to 1 so our compositor and the views
// control scaling). The native physical-pixel rect per monitor (captureGeo) is the
// canonical space that packs mixed-DPI monitors gap-free.
void SnipOverlay::grabScreens()
{
    const QList<platform::MonitorInfo> mons = platform::monitorLayout();
    QList<ScreenLayoutEntry> layoutEntries;
    QList<ScreenShot> shots;
    int idx = 0;
    for (QScreen* s : QGuiApplication::screens()) {
        ScreenShot sh;
        sh.screen = s;
        sh.geo = s->geometry();
        sh.dpr = s->devicePixelRatio();
        if (sh.dpr <= 0.0)
            sh.dpr = 1.0;
        sh.shot = s->grabWindow(0).toImage();
        sh.shot.setDevicePixelRatio(1.0);

        // Native physical rect (monitorLayout() is in screens() order). Fall back to
        // logical*dpr when no native layout is available (e.g. off-Windows).
        QRect capGeo = (idx < mons.size()) ? mons.at(idx).captureGeometry : QRect();
        if (!capGeo.isValid())
            capGeo = QRect(QPoint(int(std::lround(sh.geo.left() * sh.dpr)),
                                  int(std::lround(sh.geo.top()  * sh.dpr))),
                           sh.shot.size());
        sh.captureGeo = capGeo;

        shots.append(sh);
        layoutEntries.append(ScreenLayoutEntry{sh.geo, capGeo, sh.shot.size(), 1.0, 1.0});
        ++idx;
    }
    m_shots = shots;
    m_layout = ScreenLayout(layoutEntries);
}

void SnipOverlay::refreshFreeze()
{
    if (m_views.isEmpty())
        return;
    // processEvents() below can let the repeat timer fire again mid-grab.
    static bool busy = false;
    if (busy)
        return;
    busy = true;
    const auto done = qScopeGuard([] { busy = false; });

    // Our own overlays must not land in the new grab. Normally they are already excluded
    // from every screen capture (setExcludedFromCapture(), applied when they are shown),
    // so the grab can happen with the mask, selection and toolbar left untouched on
    // screen — that is what makes a continuous refresh steady instead of strobing.
    //
    // Only when the OS is too old for capture exclusion do we fall back to hiding the
    // overlays for the grab (which visibly blinks). Zero opacity rather than hide() keeps
    // focus and z-order intact: a hide/show cycle deactivates the windows, fighting the
    // quitOnFocusLoss watchdog and dropping the keyboard grab mid-hold.
    if (!m_captureExcluded) {
        for (SnipScreenView* v : m_views)
            v->setWindowOpacity(0.0);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
#ifdef Q_OS_WIN
        DwmFlush();      // wait until the compositor has presented a frame without us
#endif
    }

    grabScreens();

    if (!m_captureExcluded) {
        for (SnipScreenView* v : m_views)
            v->setWindowOpacity(1.0);
    }
    for (int i = 0; i < m_views.size() && i < m_shots.size(); ++i)
        m_views[i]->setBackground(m_shots[i].shot, m_shots[i].dpr);

    // A refresh always shows the *live* desktop, so it ends a `,`/`.` history replay:
    // the stashed live freeze is now stale and the restored past screen is gone.
    m_historyActive = false;
    m_liveStashed = false;
    m_liveShots.clear();

    // The freeze-time cursor snapshot is deliberately NOT re-grabbed: while the overlay
    // is up the OS cursor is our own crosshair, so a re-grab would bake that into the
    // export instead of the user's cursor.

    // Everything derived from the background is now stale.
    if (m_sel->bounds() != m_layout.captureBounds())
        m_sel->setBounds(m_layout.captureBounds());
    rescanBarcode();
    if (!m_sel->isValid())
        refreshDetection(true);
    updateAllViews();
}

void SnipOverlay::beginRefresh(RefreshSource s)
{
    const int bit = 1 << int(s);
    if (m_refreshHeld & bit)
        return;                      // already held (key auto-repeat)
    const bool idle = m_refreshHeld == 0;
    m_refreshHeld |= bit;
    if (!idle)
        return;                      // the other source is already driving the refresh
    refreshFreeze();                 // the tap itself always refreshes once
    m_refreshHold->start();
}

void SnipOverlay::endRefresh(RefreshSource s)
{
    m_refreshHeld &= ~(1 << int(s));
    if (m_refreshHeld == 0)
        stopRefreshLoop();           // the view locks on the last grabbed frame
}

void SnipOverlay::stopRefreshLoop()
{
    m_refreshHeld = 0;
    if (m_refreshHold)   m_refreshHold->stop();
    if (m_refreshRepeat) m_refreshRepeat->stop();
    if (m_refreshSpin)   m_refreshSpin->stop();
    updateAllViews();                // drop the button's active look
}

bool SnipOverlay::refreshing() const
{
    return m_refreshHeld != 0;
}

qreal SnipOverlay::refreshSpin() const
{
    if (!m_refreshRepeat || !m_refreshRepeat->isActive() || !m_refreshClock.isValid())
        return 0.0;
    constexpr qreal kSpinMsPerTurn = 1500.0;
    return std::fmod(m_refreshClock.elapsed() / kSpinMsPerTurn, 1.0) * 360.0;
}

void SnipOverlay::start()
{
    applyMagnifierSettings();
    applySnipSettings();

    // Colour-pick-only: the snip-specific corner tips panel doesn't apply (there's no
    // region to drag), so hide it — the loupe's own inline Shift hint stays.
    if (m_colorPickOnly)
        m_keyTips = false;

    // Snip → Keys policies that need a low-level keyboard hook (see IKeyboardFilter):
    // ignore an injected Ctrl+C and/or suppress the lone Left/Right Win Start menu.
    if (m_keyFilter)
        m_keyFilter->install({m_ignoreSimCopy, m_ignoreWinLeft, m_ignoreWinRight});

    // Losing activation also swallows the F5 key-up, so a continuous refresh would run on
    // with nothing left to stop it.
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState st) {
                if (st != Qt::ApplicationActive && m_refreshHeld)
                    stopRefreshLoop();
            });

    // Quit snipping when another program is activated (snip/behavior/quitOnFocusLoss). Our
    // own modal dialogs (Save-As, confirm-Esc) also deactivate the app, so m_suspendFocusQuit
    // gates them out — and so does a recording, which hands the desktop back to the app being
    // filmed on purpose: deactivation is the normal state there, not a reason to abort (it
    // would end the session and stop the recording on the first click into that app).
    if (m_quitOnFocusLoss) {
        connect(qApp, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState st) {
                    if (st != Qt::ApplicationActive && !m_suspendFocusQuit && !recordingActive())
                        finish();
                });
    }

    for (SnipScreenView* v : m_views)
        v->showOnScreen();

    // Keep the overlays out of every screen capture for the whole snip, so "refresh
    // screenshot" can re-grab the desktop without hiding anything (see refreshFreeze).
    m_captureExcluded = true;
    for (SnipScreenView* v : m_views)
        m_captureExcluded &= v->setExcludedFromCapture(true);
    if (!m_captureExcluded)
        qInfo() << "[snip] capture exclusion unavailable — refresh will blink";

    // Give focus to the monitor under the cursor.
    QScreen* cur = QGuiApplication::screenAt(QCursor::pos());
    SnipScreenView* focusView = nullptr;
    for (SnipScreenView* v : m_views) {
        if (v->targetScreen() == cur) { focusView = v; break; }
    }
    if (!focusView && !m_views.isEmpty())
        focusView = m_views.first();
    if (focusView) {
        const QPoint gc = QCursor::pos();
        focusView->seedCursor(gc);       // loupe visible on F1 without a mouse move
        onMove(m_layout.globalLogicalToCapture(gc));   // seed hover + detection (capture px)
        focusView->forceForeground();    // activate + repaint: mask, loupe, live keys
    }

    // Custom snip: adopt the pre-set area and open the annotate phase on it, exactly as
    // if the user had dragged it out. Already capture px — the units SelectionModel and
    // the size readout both use — so it needs no conversion.
    if (m_presetArea.isValid()) {
        m_sel->setRect(m_presetArea);
        m_sel->commit();
        if (m_sel->isValid()) {
            beginEditing();
            updateEditChrome();
        }
    }

    updateAllViews();                    // paint every monitor right away
}

void SnipOverlay::startQuickDrag(const QPoint& startCapturePx)
{
    // Hold-modifier + drag gesture: the WH_MOUSE_LL hook already saw the press and
    // the drag start, so we open the overlay already mid-drag anchored at the press
    // point. The overlay maps topmost under the still-held cursor, so its views pick
    // up the ongoing physical moves (onMove -> dragTo) and the release (onRelease ->
    // the m_quickMode branch runs Copy/Pin and closes). No annotate phase.
    m_quickSeeded = true;
    start();
    m_sel->beginAt(startCapturePx);
    updateAllViews();
}

void SnipOverlay::cancelQuickDrag()
{
    // Built on the press but never revealed (the gesture turned out to be a plain click).
    // Tear down without running any action; finish() is safe on an unshown overlay.
    finish();
}

void SnipOverlay::finishQuickDrag(const QPoint& releaseCapturePx)
{
    // The gesture hook reports the button-up because Qt may not deliver a native
    // release for a drag it never saw begin. Extend to the exact release point, then
    // run the normal release path (commit + the m_quickMode Copy/Pin branch + close).
    if (!m_quickSeeded || m_quickReleased)
        return;
    if (m_sel->isDragging())
        m_sel->dragTo(releaseCapturePx);
    onRelease(releaseCapturePx);
}

void SnipOverlay::applyMagnifierSettings()
{
    if (!m_appSettings)
        return;

    m_enabledFormats = ColorPicker::parseFormats(
        m_appSettings->value(QStringLiteral("interface/magnifier/formats"),
                             QStringLiteral("rgb,hex")).toString());
    if (m_enabledFormats.isEmpty())                // never leave the readout without a format
        m_enabledFormats = {ColorPicker::Format::Rgb, ColorPicker::Format::Hex};

    // Restore the row Shift last landed on. Stored as the format *code*, not an
    // index, so re-ticking the Preferences boxes can't silently point this at a
    // different format; an unticked one just falls back to the first row.
    const QString last = m_appSettings->value(QStringLiteral("interface/magnifier/selectedFormat"))
                             .toString();
    m_formatIdx = 0;
    for (int i = 0; i < m_enabledFormats.size(); ++i) {
        if (ColorPicker::codeFor(m_enabledFormats.at(i)) == last) {
            m_formatIdx = i;
            break;
        }
    }

    m_uppercaseHex = m_appSettings->value(QStringLiteral("interface/magnifier/hexUppercase"),
                                          false).toBool();
    m_noHash = m_appSettings->value(QStringLiteral("interface/magnifier/noHash"), false).toBool();
    m_usageTips = m_appSettings->value(QStringLiteral("interface/magnifier/usageTips"),
                                       true).toBool();
    m_gridLines = m_appSettings->value(QStringLiteral("interface/magnifier/gridLines"),
                                       true).toBool();
    m_showCoords = m_appSettings->value(QStringLiteral("interface/magnifier/coordinates"),
                                        true).toBool();
    // Clamped to the Preferences range so a hand-edited config.ini can't produce a
    // loupe that swallows the screen (or collapses to nothing).
    m_magPercent = qBound(50, m_appSettings->value(
                                  QStringLiteral("interface/magnifier/sizePercent"), 100).toInt(),
                          200);
}

void SnipOverlay::applySnipSettings()
{
    if (!m_appSettings)
        return;
    const auto& s = *m_appSettings;

    // Display.
    m_borderWidth = s.value(QStringLiteral("snip/display/borderWidth"), 3).toInt();
    m_maskColor   = QColor(s.value(QStringLiteral("snip/display/maskColor"),
                                   QStringLiteral("#464646")).toString());
    m_anchorColor = QColor(s.value(QStringLiteral("snip/display/anchorColor"),
                                   QStringLiteral("#ffffff")).toString());
    m_showAnchors = s.value(QStringLiteral("snip/display/showAnchors"), true).toBool();
    m_crossLines  = s.value(QStringLiteral("snip/display/crossLines"), false).toBool();
    m_keyTips     = s.value(QStringLiteral("snip/display/keyTips"), true).toBool();
    m_noAuxLines  = s.value(QStringLiteral("snip/display/noAuxLines"), false).toBool();
    m_cornerOption  = s.value(QStringLiteral("snip/display/roundedCorners"), false).toBool();
    m_cornerRounded = s.value(QStringLiteral("snip/display/cornerRounded"), false).toBool();
    m_cornerRadius  = qBound(0, s.value(QStringLiteral("snip/display/cornerRadius"), 12).toInt(),
                             kMaxCornerRadius);
    m_auxLinesOn  = s.value(QStringLiteral("snip/display/auxLinesOn"), false).toBool();
    m_accentColor  = QColor(s.value(QStringLiteral("interface/accentColor"),
                                   QStringLiteral("#2d7dff")).toString());
    if (!m_accentColor.isValid()) m_accentColor = QColor(0x2d, 0x7d, 0xff);
    // Anchor border defaults to the accent color; anchor fill (m_anchorColor) is white.
    m_anchorBorderColor = QColor(s.value(QStringLiteral("snip/display/anchorBorderColor"),
                                         m_accentColor.name()).toString());
    if (!m_anchorBorderColor.isValid()) m_anchorBorderColor = m_accentColor;

    // Behavior.
    m_hide1x1         = s.value(QStringLiteral("snip/behavior/hide1x1"), true).toBool();
    // Anti-misclick gate (core/ClickSlop.h). INI-only — no Preferences control by design;
    // documented in docs/Usage.md §4a. 0 disables it.
    m_clickSlopPx     = qMax(0, s.value(QStringLiteral("snip/behavior/clickSlopPx"), 30).toInt());
    m_clickWindowMs   = qMax(0, s.value(QStringLiteral("snip/behavior/clickWindowMs"), 300).toInt());
    m_quitOnFocusLoss = s.value(QStringLiteral("snip/behavior/quitOnFocusLoss"), false).toBool();
    m_captureCursor        = s.value(QStringLiteral("snip/captureCursor"), false).toBool();
    m_disableCursorCapture = s.value(QStringLiteral("snip/behavior/disableCursorCapture"), false).toBool();
    m_cursorCaptureOn      = m_captureCursor && !m_disableCursorCapture;   // ` / ! toggles this live

    // Keys.
    m_disableScroll = s.value(QStringLiteral("snip/keys/disableScroll"), false).toBool();
    // Default ON: losing a finished capture to a stray Esc costs more than one keypress.
    // The default is repeated in ui/PreferencesDialog's addSnipCheck for this key — they
    // must agree, or the checkbox shows one thing while the overlay does another.
    m_confirmEsc    = s.value(QStringLiteral("snip/keys/confirmEsc"), true).toBool();
    m_ignoreSimCopy  = s.value(QStringLiteral("snip/keys/ignoreSimCopy"), true).toBool();
    m_ignoreWinLeft  = s.value(QStringLiteral("snip/keys/ignoreWinLeft"), true).toBool();
    m_ignoreWinRight = s.value(QStringLiteral("snip/keys/ignoreWinRight"), false).toBool();

    // Actions.
    m_doubleClickAction = s.value(QStringLiteral("snip/keys/doubleClickAction"),
                                  QStringLiteral("copy")).toString();
    m_doubleClickQuit   = s.value(QStringLiteral("snip/keys/doubleClickQuit"), true).toBool();
    m_middleClickAction = s.value(QStringLiteral("snip/keys/middleClickAction"),
                                  QStringLiteral("pin")).toString();
    m_middleClickQuit   = s.value(QStringLiteral("snip/keys/middleClickQuit"), true).toBool();
    m_enterAction       = s.value(QStringLiteral("snip/keys/enterAction"),
                                  QStringLiteral("copy")).toString();
    m_enterQuit         = s.value(QStringLiteral("snip/keys/enterQuit"), true).toBool();
}

QRect SnipOverlay::selection() const { return m_sel->rect(); }
bool SnipOverlay::selectionValid() const { return m_sel->isValid(); }

void SnipOverlay::toggleAuxLines()
{
    m_auxLinesOn = !m_auxLinesOn;
    if (m_appSettings)                                 // persist so the toggle survives across snips
        m_appSettings->setValue(QStringLiteral("snip/display/auxLinesOn"), m_auxLinesOn);
    updateAllViews();
}

void SnipOverlay::onAltReleased()
{
    // Windows swallows a lone Alt release before Qt can raise a key event, so the raw
    // WM_SYSKEYUP caught in SnipScreenView::nativeEvent is the only reliable signal that
    // the quick-mark modifier is up.
    clearQuickMark();
}

int SnipOverlay::effectiveCornerRadius() const
{
    if (!m_cornerOption || !m_cornerRounded || m_cornerRadius <= 0 || !m_sel->isValid())
        return 0;
    // Logical -> capture px via the monitor the region starts on, exactly as
    // captureStyle() scales pen/font sizes (mixed-DPI model).
    const QRect sel = m_sel->rect();
    const qreal s = m_layout.scaleAt(sel.topLeft());
    return RoundedCorners::clamp(int(std::lround(m_cornerRadius * s)), sel.size());
}

void SnipOverlay::toggleCornerMode()
{
    if (!m_cornerOption)
        return;
    m_cornerRounded = !m_cornerRounded;
    if (m_appSettings)                     // persist so the choice survives across snips
        m_appSettings->setValue(QStringLiteral("snip/display/cornerRounded"), m_cornerRounded);
    updateAllViews();
}

void SnipOverlay::stepCornerRadius(int notches)
{
    if (!m_cornerOption)
        return;
    const int next = qBound(0, m_cornerRadius + notches, kMaxCornerRadius);
    // Scrolling is the "make it rounder" gesture, so it also switches the mode on —
    // otherwise the wheel would silently change a number with nothing to show for it.
    const bool wantRounded = next > 0;
    if (next == m_cornerRadius && m_cornerRounded == wantRounded)
        return;
    m_cornerRadius = next;
    m_cornerRounded = wantRounded;
    if (m_appSettings) {
        m_appSettings->setValue(QStringLiteral("snip/display/cornerRadius"), m_cornerRadius);
        m_appSettings->setValue(QStringLiteral("snip/display/cornerRounded"), m_cornerRounded);
    }
    updateAllViews();
}

QVector<SnipOverlay::TipRow> SnipOverlay::tipRows() const
{
    // A single-action key from the keymap, split into chips ("Ctrl+A" -> {Ctrl, A}).
    // Falls back to `def` when the registry is absent or the action is unbound so a
    // row never renders blank. Multi-key clusters (WASD, arrows) can't be one keymap
    // sequence and are passed as static chips.
    auto keymap = [this](ActionId id, const QString& def) -> QStringList {
        const QString seq = m_actions ? m_actions->keyFor(id) : QString();
        return (seq.isEmpty() ? def : seq).split(QLatin1Char('+'), Qt::SkipEmptyParts);
    };
    // The same key, kept as ONE chip ("Ctrl + C"). Used on rows that also carry a
    // separator, where split chips would read as two independent keys.
    auto combo = [this](ActionId id, const QString& def) -> QString {
        const QString seq = m_actions ? m_actions->keyFor(id) : QString();
        return (seq.isEmpty() ? def : seq).split(QLatin1Char('+'), Qt::SkipEmptyParts)
                   .join(QStringLiteral(" + "));
    };
    auto tr = [](const char* k) { return I18n::instance().t(QString::fromLatin1(k)); };

    QVector<TipRow> rows;
    if (!m_editing) {
        // Choosing the capture region (matches the reference key panel).
        rows.push_back({{QStringLiteral("W"), QStringLiteral("A"),
                         QStringLiteral("S"), QStringLiteral("D")}, tr("snip.tips.move_cursor"),
                        QStringLiteral("/")});
        rows.push_back({keymap(ActionId::Snip_ToggleDetect, QStringLiteral("Tab")),
                        tr("snip.tips.toggle_detect")});
        if (m_detectMode == DetectMode::Element)     // only mode where the walk applies
            rows.push_back({{QStringLiteral("Scroll")}, tr("snip.tips.element_level")});
        rows.push_back({keymap(ActionId::Snip_Fullscreen, QStringLiteral("Ctrl+A")),
                        tr("snip.tips.fullscreen")});
        rows.push_back({keymap(ActionId::Snip_RepeatLast, QStringLiteral("R"))
                            << QStringLiteral("Shift + R"), tr("snip.tips.reuse_area"),
                        QStringLiteral("/")});
        rows.push_back({keymap(ActionId::Snip_HistPrev, QStringLiteral(","))
                            << keymap(ActionId::Snip_HistNext, QStringLiteral(".")),
                        tr("snip.tips.history"), QStringLiteral("/")});
        rows.push_back({keymap(ActionId::Snip_ToggleCursor, QStringLiteral("`"))
                            << QStringLiteral("!"), tr("snip.tips.toggle_cursor"),
                        tr("snip.tips.sep_or")});
        if (!m_noAuxLines)      // pref off = G is a no-op, so don't advertise it
            rows.push_back({{QStringLiteral("G")}, tr("snip.tips.aux_lines")});
        rows.push_back({{QStringLiteral("Ctrl"), QStringLiteral("Drag")},
                        tr("snip.tips.square_drag")});
        rows.push_back({keymap(ActionId::Snip_Refresh, QStringLiteral("F5")),
                        tr("snip.tips.refresh")});
    } else if (!toolSelected()) {
        // Read-only: region committed, no annotation tool active.
        rows.push_back({QStringList{QStringLiteral("Enter")}
                            << combo(ActionId::Snip_Copy, QStringLiteral("Ctrl+C")),
                        tr("snip.tips.copy"), QStringLiteral("/")});
        rows.push_back({keymap(ActionId::Snip_Save, QStringLiteral("Ctrl+S")), tr("snip.tips.save")});
        rows.push_back({keymap(ActionId::Snip_Pin, QStringLiteral("Ctrl+T")), tr("snip.tips.pin")});
        rows.push_back({{QStringLiteral("↑"), QStringLiteral("↓"),
                         QStringLiteral("←"), QStringLiteral("→")}, tr("snip.tips.move_area"),
                        QStringLiteral("/")});
        rows.push_back({{QStringLiteral("Ctrl / Shift"), QStringLiteral("Arrows")},
                        tr("snip.tips.resize")});
        rows.push_back({{QStringLiteral("Shift"), QStringLiteral("Drag corner")},
                        tr("snip.tips.lock_ratio")});
        rows.push_back({{QStringLiteral("Ctrl"), QStringLiteral("Drag corner")},
                        tr("snip.tips.lock_square")});
        if (!m_noAuxLines)      // still a region-geometry phase, so the guides still help
            rows.push_back({{QStringLiteral("G")}, tr("snip.tips.aux_lines")});
        rows.push_back({keymap(ActionId::Snip_Refresh, QStringLiteral("F5")),
                        tr("snip.tips.refresh")});
        rows.push_back({{QStringLiteral("Esc")}, tr("snip.tips.cancel")});
    } else {
        // Annotating: an annotation tool is active.
        rows.push_back({keymap(ActionId::Edit_Undo, QStringLiteral("Ctrl+Z")), tr("snip.tips.undo")});
        rows.push_back({keymap(ActionId::Edit_Redo, QStringLiteral("Ctrl+Y")), tr("snip.tips.redo")});
        rows.push_back({keymap(ActionId::Edit_ClearAll, QStringLiteral("Ctrl+Shift+Z")),
                        tr("snip.tips.clear_all")});
        rows.push_back({{QStringLiteral("Space")}, tr("snip.tips.toggle_bar")});
        if (quickMarkTool())      // only the rect family can be placed from an element rect
            rows.push_back({{QStringLiteral("Alt"), QStringLiteral("Right-click")},
                            tr("snip.tips.quick_mark")});
        // Ctrl constrains every rectangle drag — the same set ShapeConstraints::
        // constrainedEnd() squares, where the ellipse's square is a circle.
        if (m_tool == annotation::ToolId::Rect || m_tool == annotation::ToolId::Mosaic
            || m_tool == annotation::ToolId::Blur || m_tool == annotation::ToolId::EraserRect
            || m_tool == annotation::ToolId::MarkerRect)
            rows.push_back({{QStringLiteral("Ctrl"), QStringLiteral("Drag")},
                            tr("snip.tips.draw_square")});
        else if (m_tool == annotation::ToolId::Ellipse)
            rows.push_back({{QStringLiteral("Ctrl"), QStringLiteral("Drag")},
                            tr("snip.tips.draw_circle")});
        if (m_tool == annotation::ToolId::Pencil     // the two freehand straighteners
            || m_tool == annotation::ToolId::Marker) {
            rows.push_back({{QStringLiteral("Shift"), QStringLiteral("Drag")},
                            tr("snip.tips.straight_stroke")});
            rows.push_back({{QStringLiteral("Ctrl"), QStringLiteral("Drag")},
                            tr("snip.tips.straight_stroke_45")});
        }
        // The line tool's right-click means something different per phase — and in Idle
        // it means nothing at all (a right-click there closes the capture), so the row
        // only appears once a line is actually under construction.
        if (isLineTool() && lineEditing())
            rows.push_back({{QStringLiteral("Right-click")},
                            tr(m_linePhase == LinePhase::Building ? "snip.tips.line_confirm"
                                                                  : "snip.tips.line_finalize")});
        rows.push_back({{QStringLiteral("Esc")}, tr("snip.tips.cancel")});
        // FUTURE: append per-active-tool key rows here (keys TBD by the user) — this
        // annotating branch is the intended home for tool-specific tips.
    }
    return rows;
}

bool SnipOverlay::hasSuggestion() const
{
    return !m_sel->isValid() && !m_sel->isDragging()
           && m_suggested.width() > 0 && m_suggested.height() > 0;
}

bool SnipOverlay::elementHierarchyActive() const
{
    return m_detectMode == DetectMode::Element && !m_editing && m_haveCursor
           && !m_sel->isValid() && !m_sel->isDragging() && hasSuggestion();
}

void SnipOverlay::onElementLevelWheel(int steps)
{
    if (m_disableScroll || !m_detect)               // snip/keys/disableScroll
        return;
    // Resolve the ancestors on the FIRST step, not while hovering: the walk costs a
    // cross-process round trip per level (measured up to 762ms on a deep browser
    // chain), which would stall every mouse-move. Paying it once on a deliberate
    // scroll is fine, and every later tick is free.
    if (m_chain.isEmpty()) {
        m_chain = m_detect->elementChainAt(m_layout.captureToGlobalLogical(m_lastCursor));
        m_level = 0;
        if (m_chain.isEmpty())
            return;
    }
    // Scroll up (steps > 0) widens out to the enclosing element.
    const int next = qBound(0, m_level + (steps > 0 ? 1 : -1), int(m_chain.size()) - 1);
    if (next == m_level)
        return;
    m_level = next;
    m_suggested = m_layout.globalLogicalRectToCapture(m_chain.at(m_level))
                      .intersected(m_sel->bounds());
    updateAllViews();
}

void SnipOverlay::refreshDetection(bool force)
{
    if (!m_detect || m_detectMode == DetectMode::Off || !m_haveCursor) {
        m_suggested = QRect();
        m_leaf = QRect();
        m_chain.clear();
        m_level = 0;
        return;
    }
    // Throttle: detection (esp. UI Automation) is too costly to run on every
    // mouse-move event. Keep the last candidate between queries. `force` bypasses
    // the throttle so a click adopts an up-to-date region.
    if (!force && m_detectClock.isValid() && m_detectClock.elapsed() < 30)
        return;
    m_detectClock.restart();

    // Detection speaks Qt global-logical coords; the pipeline is in capture px. Query
    // with the cursor mapped to logical, then map the result rect back to capture.
    const QPoint gl = m_layout.captureToGlobalLogical(m_lastCursor);
    QRect r;
    if (m_detectMode == DetectMode::Element) {
        const QRect leaf = m_detect->elementAt(gl);     // cheap: one provider hit test
        // A different innermost element means the cursor moved onto something else,
        // so drop any held level and the (now stale) chain. Comparing the leaf rect
        // is what makes this tolerant of the hand drift scrolling inevitably causes.
        if (leaf != m_leaf) {
            m_leaf = leaf;
            m_chain.clear();
            m_level = 0;
        }
        r = m_chain.isEmpty() ? leaf : m_chain.value(m_level);
    } else {
        m_leaf = QRect();
        m_chain.clear();
        m_level = 0;
        r = m_detect->windowAt(gl);
    }
    m_suggested = m_layout.globalLogicalRectToCapture(r).intersected(m_sel->bounds());
    if (m_hide1x1 && m_suggested.width() <= 1 && m_suggested.height() <= 1)
        m_suggested = QRect();                      // never auto-select a 1x1 region
}

bool SnipOverlay::captureCursorActive() const
{
    return m_cursorCaptureOn && m_cursorVisible && !m_cursorImage.isNull();
}

QRectF SnipOverlay::capturedCursorRectCapture() const
{
    // The cursor image is device (== capture) px; place it at the freeze-moment cursor
    // position mapped into capture space, offset by its hotspot (also image/device px).
    const QPointF tl = QPointF(m_layout.globalLogicalToCapture(m_cursorPos)) - QPointF(m_cursorHotspot);
    return QRectF(tl, QSizeF(m_cursorImage.size()));
}

void SnipOverlay::rescanBarcode()
{
    const QString before = m_barcodePayload;
    m_barcodePayload.clear();

    if (m_barcode && m_sel->isValid()) {
        // Decode the region exactly as it would be exported (cursor included), so what
        // the user sees in the selection is what gets scanned.
        const QImage base = compositeBase(m_sel->rect());
        if (!base.isNull())
            m_barcodePayload = BarcodeHint::singleLine(m_barcode->decodeFirst(base));
    }

    // The "Copied" acknowledgement belongs to the *payload*, not to the region: a
    // rescan that lands on the same link must not cancel it. Only a genuinely
    // different (or vanished) code retires the toast early.
    if (m_barcodePayload != before) {
        m_barcodeCopied = false;
        if (m_barcodeCopiedTimer)
            m_barcodeCopiedTimer->stop();
        updateAllViews();
    }
}

void SnipOverlay::copyBarcodePayload()
{
    if (m_barcodePayload.isEmpty())
        return;
    ColorPicker::copyToClipboard(m_barcodePayload);

    // Acknowledge the copy for 2s, then fall back to showing the link. The clock
    // starts here, on the press that performs the copy, and nothing in the release
    // path cancels it (see rescanBarcode). The snip stays active — copying the URL is
    // not a terminal action.
    m_barcodeCopied = true;
    if (!m_barcodeCopiedTimer) {
        m_barcodeCopiedTimer = new QTimer(this);
        m_barcodeCopiedTimer->setSingleShot(true);
        connect(m_barcodeCopiedTimer, &QTimer::timeout, this, [this] {
            m_barcodeCopied = false;
            updateAllViews();
        });
    }
    m_barcodeCopiedTimer->start(2000);
    updateAllViews();
}

void SnipOverlay::openBarcodePayload()
{
    // Only web links are openable; everything else is copy-only (see BarcodeHint).
    if (!BarcodeHint::isOpenableUrl(m_barcodePayload))
        return;
    QDesktopServices::openUrl(QUrl(m_barcodePayload));
    finish();      // the browser takes focus — leaving the overlay up would strand it
}

QImage SnipOverlay::compositeBase(const QRect& selCapture) const
{
    QImage base = ScreenComposite::compositeSelection(m_shots, selCapture);   // capture px, 1:1
    if (!captureCursorActive() || base.isNull() || selCapture.width() <= 0)
        return base;

    const QRectF r = capturedCursorRectCapture();
    const QRectF dst(r.topLeft() - QPointF(selCapture.topLeft()), r.size());   // 1:1 into the base
    QPainter p(&base);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(dst, m_cursorImage);   // painter clips if outside sel
    return base;
}

QImage SnipOverlay::finalImage()
{
    // A finished scrolling session replaces the region entirely: the image is taller
    // than the selection, its annotation document is empty (tools were off), and the
    // frozen pixels the normal path composites are stale. Rounded corners still apply —
    // it is the same "this is the snip" rectangle, just a long one.
    if (hasScrollResult())
        return RoundedCorners::apply(m_scrollResult, effectiveCornerRadius());
    commitTextEditor();             // bake any open text box before flattening
    const QImage base = compositeBase(m_sel->rect());
    // Corners are cut AFTER flatten so annotations running into one are trimmed with
    // the pixels underneath.
    return RoundedCorners::apply(m_doc->flatten(base, m_sel->rect()),
                                 effectiveCornerRadius());
}

QSize SnipOverlay::outputPixelSize() const
{
    return m_sel->rect().size();       // selection is already in physical (capture) px
}

void SnipOverlay::setSelectionField(int field, int value)
{
    if (!m_sel->isValid())
        return;
    const QRect r = m_sel->rect();
    switch (field) {
        case 0: m_sel->moveTopLeftTo(QPoint(value, r.top()));  break;   // keeps the size
        case 1: m_sel->moveTopLeftTo(QPoint(r.left(), value)); break;
        // setRect intersects with the desktop bounds, so an oversized w/h is truncated
        // instead of shifted — either way the top-left corner stays put.
        case 2: m_sel->setRect(QRect(r.topLeft(), QSize(std::max(1, value), r.height()))); break;
        case 3: m_sel->setRect(QRect(r.topLeft(), QSize(r.width(), std::max(1, value)))); break;
        default: return;
    }
    m_sel->commit();            // same as finishing a grip drag: remember it as "last"
    updateEditChrome();         // re-anchor the toolbars to the new region
    rescanBarcode();                 // the region changed — its code content may have too
    updateAllViews();
}

void SnipOverlay::updateAllViews()
{
    for (SnipScreenView* v : m_views)
        v->update();
}

void SnipOverlay::hideAllToolbars()
{
    if (m_paintBar) {
        m_paintBar->hide();
        if (QWidget* sub = m_paintBar->subBar())
            sub->hide();
    }
}

SnipOverlay::Handle SnipOverlay::handleAt(const QPoint& g) const
{
    if (!isReadOnly() || !m_sel->isValid())
        return Handle::None;
    const QRect r = m_sel->rect();
    const int hs = int(std::lround(8 * m_layout.scaleAt(g)));   // grip hit half-size (capture px)
    const int xl = r.left(), xr = r.right(), xc = r.center().x();
    const int yt = r.top(),  yb = r.bottom(), yc = r.center().y();
    struct HP { Handle h; int x, y; };
    const HP pts[] = {                              // corners first (priority over edges)
        {Handle::TopLeft, xl, yt}, {Handle::TopRight, xr, yt},
        {Handle::BottomLeft, xl, yb}, {Handle::BottomRight, xr, yb},
        {Handle::Top, xc, yt}, {Handle::Bottom, xc, yb},
        {Handle::Left, xl, yc}, {Handle::Right, xr, yc},
    };
    for (const HP& p : pts)
        if (qAbs(g.x() - p.x) <= hs && qAbs(g.y() - p.y) <= hs)
            return p.h;
    if (r.contains(g))
        return Handle::Move;
    return Handle::None;
}

Qt::CursorShape SnipOverlay::cursorAt(const QPoint& g) const
{
    // OCR mode: the region is a text surface, not a movable/resizable selection —
    // an I-beam over it, a plain arrow outside.
    if (m_ocrActive)
        return selection().contains(g) ? Qt::IBeamCursor : Qt::ArrowCursor;
    // Circle eraser: hide the system cursor inside the region so only the drawn
    // two-ring brush cursor shows. Rectangle eraser uses a (hollow) crosshair.
    // Pen + circle eraser draw their own cursor (a sized dot/crosshair, two rings) over
    // the region, so hide the system cursor there.
    if ((m_tool == annotation::ToolId::Eraser || m_tool == annotation::ToolId::Pencil
         || m_tool == annotation::ToolId::MosaicBrush
         || m_tool == annotation::ToolId::BlurBrush)
        && selection().contains(g))
        return Qt::BlankCursor;
    if (m_tool == annotation::ToolId::EraserRect)
        return Qt::CrossCursor;
    // Text tool: a big I-beam over the capture region previews where a click drops a
    // text box (the editor box itself carries its own move/resize cursors).
    if (m_tool == annotation::ToolId::Text && selection().contains(g))
        return Qt::IBeamCursor;
    // The renumber +/− buttons are chrome, not canvas — a normal arrow, and checked before
    // the region test below since they sit just outside the badge (and can fall outside
    // the selection entirely).
    if (m_tool == annotation::ToolId::NumberLabel) {
        for (const QRect& r : labelStepButtons())
            if (r.contains(g))
                return Qt::ArrowCursor;
        if (labelDeleteButton().contains(g))
            return Qt::ArrowCursor;
    }
    // Number label: the badge preview under the pointer IS the cursor (drawn by the
    // view), so blank the system one — but only while a click would actually stamp.
    // While renumbering, or while hovering a committed badge (where a click edits
    // instead), no preview is drawn, so the system cursor must come back or the pointer
    // would vanish entirely. I-beam matches the text-entry meaning of that click.
    if (m_tool == annotation::ToolId::NumberLabel && selection().contains(g)) {
        if (labelEditing() || labelAt(g))
            return Qt::IBeamCursor;
        return Qt::BlankCursor;
    }
    // On-canvas shape editing: control-point cursors on the selected shape, and a pointing
    // hand over a hoverable committed shape.
    if (annotation::Annotation* sel = selectedShape()) {
        const ShapeHandles h = shapeHandles();
        if (h.active) {
            const qreal grab = shapeGrab(sel->bounds().center());
            if (distF(g, h.deleteHandle) <= grab)
                return Qt::PointingHandCursor;                // × button
            if (distF(g, h.rotateHandle) <= grab)
                return Qt::PointingHandCursor;                // spin handle
            for (int i = 0; i < 8; ++i)
                if (distF(g, h.grips[i]) <= grab) {
                    if (i == 0 || i == 4) return Qt::SizeFDiagCursor;   // TL / BR
                    if (i == 2 || i == 6) return Qt::SizeBDiagCursor;   // TR / BL
                    if (i == 1 || i == 5) return Qt::SizeVerCursor;     // Top / Bottom
                    return Qt::SizeHorCursor;                            // Left / Right
                }
            if (sel->hitTest(g))
                return Qt::SizeAllCursor;                     // body → move
        }
    }
    if (m_shapeHover)
        return Qt::PointingHandCursor;
    switch (handleAt(g)) {
        case Handle::TopLeft:
        case Handle::BottomRight: return Qt::SizeFDiagCursor;
        case Handle::TopRight:
        case Handle::BottomLeft:  return Qt::SizeBDiagCursor;
        case Handle::Top:
        case Handle::Bottom:      return Qt::SizeVerCursor;
        case Handle::Left:
        case Handle::Right:       return Qt::SizeHorCursor;
        case Handle::Move:        return Qt::SizeAllCursor;
        default:                  return Qt::CrossCursor;
    }
}

QPoint SnipOverlay::constrainCorner(const QPoint& g, Qt::KeyboardModifiers mods) const
{
    // The fixed corner is the one diagonally opposite the grip being dragged.
    QPoint anchor;
    switch (m_activeHandle) {
        case Handle::TopLeft:     anchor = m_resizeOrig.bottomRight(); break;
        case Handle::TopRight:    anchor = m_resizeOrig.bottomLeft();  break;
        case Handle::BottomLeft:  anchor = m_resizeOrig.topRight();    break;
        case Handle::BottomRight: anchor = m_resizeOrig.topLeft();     break;
        default: return g;                  // edge grips: no fixed corner, no lock
    }
    // Ctrl wins if both are held: a square is the more specific request.
    if (mods & Qt::ControlModifier)
        return ShapeConstraints::ratioCorner(anchor, g, QSize(1, 1), m_sel->bounds());
    if (mods & Qt::ShiftModifier)
        return ShapeConstraints::ratioCorner(anchor, g, m_resizeOrig.size(), m_sel->bounds());
    return g;
}

void SnipOverlay::applyResize(const QPoint& g, Qt::KeyboardModifiers mods)
{
    QRect r = m_resizeOrig;
    const QPoint c = constrainCorner(g, mods);
    switch (m_activeHandle) {
        case Handle::Left:        r.setLeft(g.x());   break;
        case Handle::Right:       r.setRight(g.x());  break;
        case Handle::Top:         r.setTop(g.y());    break;
        case Handle::Bottom:      r.setBottom(g.y()); break;
        case Handle::TopLeft:     r.setTopLeft(c);     break;
        case Handle::TopRight:    r.setTopRight(c);    break;
        case Handle::BottomLeft:  r.setBottomLeft(c);  break;
        case Handle::BottomRight: r.setBottomRight(c); break;
        default: return;
    }
    m_sel->setRect(r.normalized());                // normalize handles edge crossings
}

// --- On-canvas non-text shape editing ---------------------------------------
bool SnipOverlay::isSelectableShape(annotation::ToolId t)
{
    using annotation::ToolId;
    return t == ToolId::Rect || t == ToolId::Ellipse || t == ToolId::Mosaic
        || t == ToolId::Blur || t == ToolId::MarkerRect;
}

annotation::Annotation* SnipOverlay::shapeById(int id) const
{
    if (!m_doc || id <= 0)
        return nullptr;
    for (const auto& it : m_doc->items())
        if (it->id == id)
            return it.get();
    return nullptr;
}

annotation::Annotation* SnipOverlay::selectedShape() const
{
    annotation::Annotation* a = shapeById(m_shapeSel);
    return (a && isSelectableShape(a->tool())) ? a : nullptr;
}

bool SnipOverlay::shapeHoverHit(const annotation::Annotation* a, const QPoint& g) const
{
    using annotation::ToolId;
    // A hollow (unfilled) rectangle/ellipse is grabbed only by its outline — its interior
    // shows the capture through, so it isn't part of the shape. Everything else (filled
    // rect/ellipse, mosaic, blur, marker-rect) covers its whole area.
    const bool hollow = (a->tool() == ToolId::Rect || a->tool() == ToolId::Ellipse)
                        && !a->style.filled;
    if (!hollow)
        return a->hitTest(g);
    QPainterPathStroker stroker;
    const double tol = a->style.penWidth * 0.5 + 6.0 * m_layout.scaleAt(a->bounds().center());
    stroker.setWidth(qMax(4.0, tol * 2.0));          // a grab band centred on the stroke
    return stroker.createStroke(a->outlinePath()).contains(QPointF(g));
}

annotation::Annotation* SnipOverlay::selectableShapeAt(const QPoint& g) const
{
    if (!m_doc)
        return nullptr;
    // Topmost item under the cursor wins: return it only if it's a selectable shape (a
    // non-selectable topmost item — text/label/line — blocks so its own tool owns the click).
    // Hollow rect/ellipse use the outline-only test, so hovering their empty interior falls
    // through to whatever is beneath (or nothing).
    const auto& items = m_doc->items();
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        annotation::Annotation* a = it->get();
        const bool selectable = isSelectableShape(a->tool());
        const bool hit = selectable ? shapeHoverHit(a, g) : a->hitTest(g);
        if (hit)
            return selectable ? a : nullptr;
    }
    return nullptr;
}

qreal SnipOverlay::shapeGrab(const QPoint& center) const
{
    return 9.0 * m_layout.scaleAt(center);            // ~9 logical px, in capture px
}

void SnipOverlay::shapeControlPoints(const annotation::Annotation* a, QVector<QPoint>& grips,
                                     QPoint& rotateHandle, QPoint& rotateAnchor,
                                     QPoint& deleteHandle, QPoint& deleteAnchor) const
{
    grips.clear();
    const QVector<QPoint> geom = a->geometry();
    if (geom.size() < 2)
        return;
    const QRectF cr(QRect(geom.first(), geom.last()).normalized());
    const QPointF c = cr.center();
    const qreal rot = a->rotation();
    QPointF local[8];
    gripLocals(cr.width() / 2.0, cr.height() / 2.0, local);
    grips.resize(8);
    for (int i = 0; i < 8; ++i)
        grips[i] = rotateAbout(c, rot, c + local[i]).toPoint();
    // The handles grow "up" out of the box on a stalk (matching the Text tool's kStalk=28):
    // the spin handle above the top-edge midpoint, the delete (×) button above the top-right
    // corner. up = (0,-1) rotated by rot; stalk length in capture px = 28 logical * scale.
    const qreal rad = qDegreesToRadians(rot);
    const QPointF up(std::sin(rad), -std::cos(rad));
    const qreal stalk = 28.0 * m_layout.scaleAt(c.toPoint());
    rotateAnchor = grips[1];                          // top-edge midpoint
    rotateHandle = (QPointF(rotateAnchor) + up * stalk).toPoint();
    deleteAnchor = grips[2];                          // top-right corner
    deleteHandle = (QPointF(deleteAnchor) + up * stalk).toPoint();
}

QPainterPath SnipOverlay::shapeHoverPath() const
{
    if (const annotation::Annotation* a = shapeById(m_shapeHover))
        return a->outlinePath();
    return {};
}

SnipOverlay::ShapeHandles SnipOverlay::shapeHandles() const
{
    ShapeHandles h;
    const annotation::Annotation* a = selectedShape();
    if (!a)
        return h;
    QVector<QPoint> grips;
    QPoint rotH, rotA, delH, delA;
    shapeControlPoints(a, grips, rotH, rotA, delH, delA);
    if (grips.size() != 8)
        return h;
    h.active = true;
    h.grips = grips;
    h.box = { grips[0], grips[2], grips[4], grips[6] };   // TL, TR, BR, BL
    h.rotateHandle = rotH;
    h.rotateAnchor = rotA;
    h.deleteHandle = delH;
    h.deleteAnchor = delA;
    h.rotationDeg = a->rotation();
    return h;
}

SnipOverlay::ShapeRotateReadout SnipOverlay::shapeRotateReadout() const
{
    ShapeRotateReadout r;
    if (m_shapeEdit != ShapeEdit::Rotating)
        return r;
    const annotation::Annotation* a = selectedShape();
    if (!a)
        return r;
    QVector<QPoint> grips;
    QPoint rotH, rotA, delH, delA;
    shapeControlPoints(a, grips, rotH, rotA, delH, delA);
    if (grips.size() != 8)
        return r;
    const QPointF c = QRectF(a->bounds()).center();  // rotation preserves the centre
    QPointF out = QPointF(rotH) - c;
    const double L = std::hypot(out.x(), out.y());
    out = (L > 0.1) ? out / L : QPointF(0, -1);
    const qreal scale = m_layout.scaleAt(c.toPoint());
    r.active = true;
    r.text = QString::number(qRound(a->rotation())) + QStringLiteral("°");
    r.pos = (QPointF(rotH) + out * (9.0 + 10.0) * scale).toPoint();   // past the disc (kHandleR+10)
    return r;
}

void SnipOverlay::syncPaintBarToShape(const annotation::Annotation* a)
{
    if (!a || !m_paintBar)
        return;
    using annotation::ToolId;
    const qreal s = m_layout.scaleAt(a->bounds().center());
    if (a->tool() == ToolId::Rect || a->tool() == ToolId::Ellipse) {
        const int lg = qBound(1, int(std::lround(a->style.penWidth / s)), 50);
        m_style.penWidth = lg;
        m_paintBar->setPenWidthValue(lg);
    } else if (a->tool() == ToolId::Mosaic || a->tool() == ToolId::Blur) {
        const int lg = qBound(1, int(std::lround(a->style.redactStrength / s)), 20);
        m_style.redactStrength = lg;
        m_paintBar->setRedactStrengthValue(lg);
    }
}

void SnipOverlay::selectShape(int id)
{
    m_shapeSel = id;
    m_shapeHover = 0;
    m_shapeEdit = ShapeEdit::None;
    syncPaintBarToShape(shapeById(id));   // toolbar reflects the shape's width / strength
    updateAllViews();
}

void SnipOverlay::deselectShape()
{
    flushShapeStyle();                   // bank any in-flight wheel width/strength change
    if (!m_shapeSel && m_shapeHover == 0 && m_shapeEdit == ShapeEdit::None)
        return;
    m_shapeSel = 0;
    m_shapeHover = 0;
    m_shapeEdit = ShapeEdit::None;
    m_shapeGrip = -1;
    updateAllViews();
}

void SnipOverlay::deleteSelectedShape()
{
    annotation::Annotation* a = selectedShape();
    if (!a)
        return;
    const int id = a->id;
    deselectShape();
    m_doc->remove(id);
    rescanBarcode();
    updateEditChrome();
    updateAllViews();
}

bool SnipOverlay::beginShapeEdit(const QPoint& g)
{
    annotation::Annotation* a = selectedShape();
    if (!a)
        return false;
    QVector<QPoint> grips;
    QPoint rotH, rotA, delH, delA;
    shapeControlPoints(a, grips, rotH, rotA, delH, delA);
    if (grips.size() != 8)
        return false;
    const qreal grab = shapeGrab(a->bounds().center());
    if (distF(g, delH) <= grab) {                    // × button → delete now (no drag)
        deleteSelectedShape();
        return true;
    }
    m_shapeBeforeGeom = a->geometry();
    m_shapeBeforeRot = a->rotation();
    if (distF(g, rotH) <= grab) {                    // spin handle
        m_shapeEdit = ShapeEdit::Rotating;
        hideAllToolbars();
        return true;
    }
    for (int i = 0; i < 8; ++i)                       // resize grips
        if (distF(g, grips[i]) <= grab) {
            m_shapeEdit = ShapeEdit::Resizing;
            m_shapeGrip = i;
            hideAllToolbars();
            return true;
        }
    if (a->hitTest(g)) {                              // body → move
        m_shapeEdit = ShapeEdit::Moving;
        m_shapeMoveLast = g;
        m_shapeMoveTotal = QPoint();
        hideAllToolbars();
        return true;
    }
    return false;
}

void SnipOverlay::applyShapeResize(const QPoint& g, Qt::KeyboardModifiers mods)
{
    annotation::Annotation* a = selectedShape();
    if (!a || m_shapeBeforeGeom.size() < 2 || m_shapeGrip < 0)
        return;
    // Compute against the box AT GESTURE START (no per-frame drift). Work in the box's
    // un-rotated frame anchored at the OPPOSITE grip, so that grip stays fixed on screen
    // even when the box is at an angle.
    const QRectF cr0(QRect(m_shapeBeforeGeom.first(), m_shapeBeforeGeom.last()).normalized());
    const qreal theta = m_shapeBeforeRot;
    const QPointF c0 = cr0.center();
    const qreal hw = cr0.width() / 2.0, hh = cr0.height() / 2.0;
    QPointF local[8];
    gripLocals(hw, hh, local);
    const int i = m_shapeGrip;
    const QPointF anchorWorld = rotateAbout(c0, theta, c0 + local[(i + 4) % 8]);
    QPointF gUn = rotateAbout(anchorWorld, -theta, QPointF(g));   // cursor, box tilt removed
    const QPointF cUnOld = rotateAbout(anchorWorld, -theta, c0);  // old centre, same frame

    qreal left = cUnOld.x() - hw, right = cUnOld.x() + hw;
    qreal top = cUnOld.y() - hh, bottom = cUnOld.y() + hh;
    const bool corner = (i % 2 == 0);
    if (corner) {
        // Ctrl = square, Shift = keep the start aspect — both about the pinned corner.
        QPoint gp = gUn.toPoint();
        const QPoint anc = anchorWorld.toPoint();
        if (mods & Qt::ControlModifier)
            gp = ShapeConstraints::ratioCorner(anc, gp, QSize(1, 1), QRect());
        else if (mods & Qt::ShiftModifier)
            gp = ShapeConstraints::ratioCorner(anc, gp, cr0.size().toSize(), QRect());
        gUn = QPointF(gp);
        left = qMin(anchorWorld.x(), gUn.x()); right = qMax(anchorWorld.x(), gUn.x());
        top = qMin(anchorWorld.y(), gUn.y());  bottom = qMax(anchorWorld.y(), gUn.y());
    } else if (i == 1 || i == 5) {                   // horizontal edge: move Y only
        top = qMin(gUn.y(), anchorWorld.y()); bottom = qMax(gUn.y(), anchorWorld.y());
    } else {                                          // vertical edge (3, 7): move X only
        left = qMin(gUn.x(), anchorWorld.x()); right = qMax(gUn.x(), anchorWorld.x());
    }
    QRectF rectUn(QPointF(left, top), QPointF(right, bottom));
    if (rectUn.width() < 1.0)  rectUn.setWidth(1.0);
    if (rectUn.height() < 1.0) rectUn.setHeight(1.0);
    const QPointF worldCentre = rotateAbout(anchorWorld, theta, rectUn.center());
    const QSizeF s = rectUn.size();
    const QRectF content(worldCentre.x() - s.width() / 2.0, worldCentre.y() - s.height() / 2.0,
                         s.width(), s.height());
    a->setGeometry({content.topLeft().toPoint(), content.bottomRight().toPoint()});
    a->setRotation(theta);
    updateAllViews();
}

void SnipOverlay::applyShapeRotate(const QPoint& g, Qt::KeyboardModifiers mods)
{
    annotation::Annotation* a = selectedShape();
    if (!a || m_shapeBeforeGeom.size() < 2)
        return;
    const QRectF cr(QRect(m_shapeBeforeGeom.first(), m_shapeBeforeGeom.last()).normalized());
    const QPointF c = cr.center();
    // The spin handle sits "up" from the box; rot = 0 points it at -90°, so the angle from
    // centre to cursor plus 90° is the rotation that turns the handle toward the cursor.
    qreal rot = qRadiansToDegrees(std::atan2(g.y() - c.y(), g.x() - c.x())) + 90.0;
    if (mods & Qt::ShiftModifier)                    // Shift snaps to 15°
        rot = std::round(rot / 15.0) * 15.0;
    a->setGeometry(m_shapeBeforeGeom);               // geometry fixed; only the angle changes
    a->setRotation(rot);
    updateAllViews();
}

void SnipOverlay::commitShapeEdit()
{
    if (m_shapeEdit == ShapeEdit::Moving)
        m_doc->moveApplied(m_shapeSel, m_shapeMoveTotal);
    else if (m_shapeEdit == ShapeEdit::Resizing || m_shapeEdit == ShapeEdit::Rotating)
        m_doc->reshapeApplied(m_shapeSel, m_shapeBeforeGeom, m_shapeBeforeRot);
    m_shapeEdit = ShapeEdit::None;
    m_shapeGrip = -1;
    m_shapeMoveTotal = QPoint();
    rescanBarcode();
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::beginShapeStyle(int id)
{
    if (m_shapeStyleId == id)
        return;                            // same shape, same burst — keep the first snapshot
    flushShapeStyle();                     // moved to a different shape: bank the old one
    annotation::Annotation* a = shapeById(id);
    if (!a)
        return;
    m_shapeStyleId = id;
    m_shapeStyleBefore = a->style;
    if (!m_shapeStyleFlush) {
        m_shapeStyleFlush = new QTimer(this);
        m_shapeStyleFlush->setSingleShot(true);
        m_shapeStyleFlush->setInterval(500);   // "the wheel stopped" — one gesture
        connect(m_shapeStyleFlush, &QTimer::timeout, this, [this] { flushShapeStyle(); });
    }
    m_shapeStyleFlush->start();
}

void SnipOverlay::flushShapeStyle()
{
    if (m_shapeStyleId <= 0)
        return;
    const int id = m_shapeStyleId;
    const annotation::Style before = m_shapeStyleBefore;
    m_shapeStyleId = 0;
    if (m_shapeStyleFlush)
        m_shapeStyleFlush->stop();
    m_doc->restyleApplied(id, before);     // no-op if the width/strength ended up unchanged
}

void SnipOverlay::onPress(const QPoint& g)
{
    if (m_colorPickOnly) { copyColor(); return; }   // colour picker: click copies + closes
    // Move mode owns every press inside the region: it is the only thing the overlay is
    // taking clicks back for, and the drawing/selection machinery below must not see it.
    if (m_scrollMoving && beginScrollMove(g)) return;
    flushLabelResize();                 // a click ends any in-flight wheel resize
    flushShapeStyle();                  // ...and any in-flight shape width/strength scroll
    if (m_ocrActive) { handleOcrPress(g); return; }   // OCR mode: drag selects text
    // The line tool has its own click-to-place / control-point state machine.
    if (m_editing && isLineTool()) { handleLinePress(g, QGuiApplication::keyboardModifiers()); return; }
    if (m_editing) {
        // On-canvas shape editing takes precedence over drawing (any tool): a press on the
        // selected shape's grips/spin-handle/body edits it; a press on a committed non-text
        // shape selects it; a press on empty canvas drops the selection and falls through.
        if (m_shapeSel && beginShapeEdit(g)) return;   // grip/spin/×/body handled (hides toolbars itself)
        if (annotation::Annotation* s = selectableShapeAt(g)) { selectShape(s->id); return; }
        if (m_shapeSel) deselectShape();
        // With a tool selected, the mouse draws annotations.
        if (m_tool != annotation::ToolId::None) {
            // A canvas click with the Text tool re-opens a committed text box if one is
            // under the cursor, otherwise commits any open box and opens a new one at the
            // click (clicks inside the editor go to it, not here).
            if (m_tool == annotation::ToolId::Text) {
                if (annotation::Annotation* t = textAt(g)) reopenText(t);
                else                                       placeText(g);
                return;
            }
            if (m_tool == annotation::ToolId::NumberLabel) {
                handleLabelPress(g);
                return;
            }
            startDraft(g);
            return;
        }
        // Read-only mode (area committed, no tool): grab an edge/corner grip to
        // resize, or drag inside to move it whole. The region can no longer be
        // re-drawn from scratch.
        const Handle h = handleAt(g);
        if (h != Handle::None && h != Handle::Move) {
            m_resizing = true;
            m_activeHandle = h;
            m_resizeOrig = m_sel->rect();
            hideAllToolbars();
        } else if (h == Handle::Move) {
            m_movingSel = true;
            m_moveStart = g;
            m_moveOrigin = m_sel->rect().topLeft();
            hideAllToolbars();
        } else if (m_historyActive) {
            // Viewing a restored past desktop: a drag starting outside the region re-snips a
            // fresh area on it (the toolbar/strip get out of the way). Normal snips (not on a
            // historical background) keep their behaviour — a click outside does nothing.
            m_editing = false;
            hideAllToolbars();
            if (m_stripOpen)
                closeHistoryStrip();
            m_sel->beginAt(g);
            updateAllViews();
        }
        return;
    }
    hideAllToolbars();
    // Starting a fresh selection = re-snip: dismiss the history strip so it doesn't
    // cloud the view, then draw on whatever (possibly historical) background is shown.
    if (m_stripOpen)
        closeHistoryStrip();
    // Don't begin the drag yet — hold the press as a click until it travels past the slop
    // ladder (onMove). Leaving SelectionModel alone is what keeps the detected region
    // highlighted meanwhile: hasSuggestion() goes dark the moment isDragging() turns true.
    // The gate only applies when there IS a region to fall back to.
    m_pressActive = true;
    m_pressPos = g;
    m_pressClock.start();
    m_pressSlopPx = hasSuggestion()
                      ? int(std::lround(m_clickSlopPx * m_layout.scaleAt(g)))   // logical → capture px
                      : 0;
    updateAllViews();
}

void SnipOverlay::onMove(const QPoint& g)
{
    m_lastCursor = g;
    m_haveCursor = true;
    if (m_moveDragging) { applyScrollMove(g); return; }   // Move mode owns the drag
    if (m_ocrActive) { handleOcrMove(g); return; }    // OCR mode: extend the text selection
    // A shape edit in progress owns the drag (move / resize / rotate).
    if (m_shapeEdit != ShapeEdit::None) {
        const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
        if (m_shapeEdit == ShapeEdit::Resizing)      applyShapeResize(g, mods);
        else if (m_shapeEdit == ShapeEdit::Rotating) applyShapeRotate(g, mods);
        else {                                        // Moving: translate live, record on release
            const QPoint d = g - m_shapeMoveLast;
            if (!d.isNull()) {
                if (annotation::Annotation* a = selectedShape()) a->translate(d);
                m_shapeMoveTotal += d;
                m_shapeMoveLast = g;
                updateAllViews();
            }
        }
        return;
    }
    // Text tool: highlight a committed text box under the cursor with a dashed outline.
    // Works even while editing another box — clicking the highlighted box commits the
    // open one and re-opens the hovered one (the box being edited isn't in the document,
    // so it can never match here).
    m_textHoverOutline.clear();
    if (m_editing && m_tool == annotation::ToolId::Text) {
        if (annotation::Annotation* t = textAt(g))
            m_textHoverOutline = t->outline();
    }
    // On-canvas shape hover: a dashed shape-matched outline on the committed non-text shape
    // under the cursor (any tool), unless one is already selected or a draw/region drag is
    // underway. A click there selects it (handled in onPress).
    {
        const int prev = m_shapeHover;
        m_shapeHover = 0;
        if (m_editing && !m_shapeSel && !m_drawing && !m_movingSel && !m_resizing
            && !m_sel->isDragging() && m_linePhase == LinePhase::Idle) {
            if (annotation::Annotation* s = selectableShapeAt(g))
                m_shapeHover = s->id;
        }
        if (m_shapeHover != prev)
            updateAllViews();
    }
    // Number label: a press on a badge is a click (renumber) until it moves past the
    // threshold, at which point it becomes a drag that carries the badge along.
    if (m_editing && m_tool == annotation::ToolId::NumberLabel) {
        if (const annotation::Annotation* ed = editedLabel()) {
            // Keep the dashed outline on the badge being renumbered — the clear above
            // would otherwise drop it the moment the mouse twitches.
            m_textHoverOutline = ed->outline();
            updateAllViews();
            return;
        }
        if (m_labelPressId > 0) {
            const QPoint d = g - m_labelPressPos;
            if (!m_labelMoved && d.manhattanLength() > 4)
                m_labelMoved = true;
            if (m_labelMoved && d != QPoint()) {
                // Translate directly (NOT via doc->move): one undo command per mouse-move
                // would make a single drag take dozens of Ctrl+Z. The whole gesture is
                // recorded once on release — see handleLabelRelease.
                for (const auto& item : m_doc->items())
                    if (item->id == m_labelPressId) { item->translate(d); break; }
                m_labelDragTotal += d;
                m_labelPressPos = g;
            }
        } else if (!labelEditing()) {
            if (annotation::Annotation* a = labelAt(g))
                m_textHoverOutline = a->outline();
        }
        updateAllViews();
        return;
    }
    if (m_editing && isLineTool() && m_linePhase != LinePhase::Idle) {
        handleLineMove(g, QGuiApplication::keyboardModifiers());
        return;
    }
    // Anti-misclick gate: a held press is still a click until it travels far enough, and
    // only then does the region drag begin — anchored back at the press point, so nothing
    // is lost by arming late.
    if (m_pressActive && !m_sel->isDragging()
        && click_slop::exceedsClickSlop(m_pressPos, g, m_pressClock.elapsed(),
                                        m_pressSlopPx, m_clickWindowMs))
        m_sel->beginAt(m_pressPos);
    if (m_drawing) {
        updateDraft(g, QGuiApplication::keyboardModifiers());
    } else if (m_resizing) {
        applyResize(g, QGuiApplication::keyboardModifiers());
    } else if (m_movingSel) {
        m_sel->moveTopLeftTo(m_moveOrigin + (g - m_moveStart));   // keep size, clamp
    } else if (m_sel->isDragging()) {
        // Ctrl drags out a square region — but only in an explicit snip (F1). In a
        // hold-modifier Quick-snip gesture the trigger modifier (often Ctrl) is still
        // held, so honouring it would force every quick drag square; keep it freeform.
        if (!m_quickSeeded && (QGuiApplication::keyboardModifiers() & Qt::ControlModifier))
            m_sel->dragTo(ShapeConstraints::ratioCorner(m_sel->anchor(), g,
                                                        QSize(1, 1), m_sel->bounds()));
        else
            m_sel->dragTo(g);
    } else if (!m_pressActive && !m_sel->isValid()) {
        refreshDetection();              // hover: highlight the window/element under cursor
    }
    // (While a press is held but unarmed the highlight deliberately stays frozen on the
    // press target — drifting onto a neighbour mid-slip must not change what a click grabs.)
    refreshQuickMark();                  // Alt+hover: preview the element a right-click would fill
    updateAllViews();
}

void SnipOverlay::onWheel(int steps)
{
    if (m_disableScroll)                            // snip/keys/disableScroll
        return;
    // While a text box is open, scrolling anywhere in the capture area scales its font
    // + box (about the box centre) — same as scrolling over the box itself.
    if (m_textEditor) {
        m_textEditor->wheelScale(steps);
        return;
    }
    // A selected non-text shape takes the wheel too — scroll ANYWHERE adjusts its pen width
    // (Rect/Ellipse) or redact strength (Mosaic/Blur), applied live and banked as one undo
    // step on idle. Same "scroll anywhere while it's active" model as an open text box.
    if (annotation::Annotation* a = selectedShape()) {
        using annotation::ToolId;
        const bool pen = a->tool() == ToolId::Rect || a->tool() == ToolId::Ellipse;
        const bool redact = a->tool() == ToolId::Mosaic || a->tool() == ToolId::Blur;
        if (pen || redact) {
            beginShapeStyle(a->id);                    // snapshot the pre-burst style once
            // Step in LOGICAL units (one notch = one width/strength unit, exactly like the
            // PaintBar), clamped to the toolbar control's range, then store back in capture px
            // (the same logical→capture scale captureStyle applies at creation).
            const qreal s = m_layout.scaleAt(a->bounds().center());
            if (pen) {
                const int lg = qBound(1, int(std::lround(a->style.penWidth / s)) + steps, 50);
                a->style.penWidth = qMax(1, int(std::lround(lg * s)));
            } else {
                const int lg = qBound(1, int(std::lround(a->style.redactStrength / s)) + steps, 20);
                a->style.redactStrength = qMax(1, int(std::lround(lg * s)));
            }
            syncPaintBarToShape(a);                    // reflect the new value in the toolbar
            if (m_shapeStyleFlush)
                m_shapeStyleFlush->start();            // restart the idle window each notch
            updateAllViews();
            return;
        }
    }
    // Number label: scrolling over an existing badge resizes THAT badge; anywhere else
    // it resizes the next one to be stamped (via the toolbar's size box, below).
    if (m_editing && m_tool == annotation::ToolId::NumberLabel && m_haveCursor) {
        annotation::Annotation* a = labelAt(m_lastCursor);
        if (a) {
            beginLabelResize(a->id);       // snapshot the pre-gesture size (once per burst)
            // style.fontPx is capture px; a wheel step is one LOGICAL px, so scale it
            // for the monitor under the cursor (the same factor captureStyle applies).
            const qreal s = m_layout.scaleAt(m_lastCursor);
            a->style.fontPx = qBound(4.0 * s, a->style.fontPx + steps * s, 200.0 * s);
            if (a->id == m_labelEditId)      // keep the outline/buttons on the resized badge
                m_textHoverOutline = a->outline();
            if (m_labelResizeFlush)
                m_labelResizeFlush->start();  // restart the idle window on every notch
            updateAllViews();
            return;
        }
    }
    // Scrolling the canvas with a tool active adjusts that tool's line width / eraser
    // size (the toolbar control + persisted per-tool width stay in sync via PaintBar).
    if (m_editing && m_tool != annotation::ToolId::None && m_paintBar) {
        m_paintBar->wheelAdjustWidth(steps);
        updateAllViews();                          // refresh the eraser ring / draft preview
    }
}

void SnipOverlay::onRelease(const QPoint& g)
{
    // Quick-snip gesture: the release arrives from the hook and (sometimes) natively —
    // handle only the first, so the action never runs twice.
    if (m_quickSeeded) {
        if (m_quickReleased)
            return;
        m_quickReleased = true;
    }
    const bool gatedClick = m_pressActive;   // the press never armed a drag (anti-misclick gate)
    m_pressActive = false;
    if (m_moveDragging) { endScrollMove(); return; }
    if (m_ocrActive) { handleOcrRelease(); return; }
    if (m_shapeEdit != ShapeEdit::None) { commitShapeEdit(); return; }
    if (m_labelPressId > 0) { handleLabelRelease(); return; }
    if (m_editing && isLineTool() && m_linePhase != LinePhase::Idle) {
        handleLineRelease(g, QGuiApplication::keyboardModifiers());
        return;
    }
    if (m_drawing) {
        updateDraft(g, QGuiApplication::keyboardModifiers());
        commitDraft();
        return;
    }
    if (m_resizing) {                           // finished dragging a resize grip
        m_resizing = false;
        m_activeHandle = Handle::None;
        m_sel->commit();
        updateEditChrome();                     // re-anchor the toolbars to the new region
        rescanBarcode();
        updateAllViews();
        return;
    }
    if (m_movingSel) {                          // finished dragging the committed region
        m_movingSel = false;
        m_sel->commit();                        // remember the new position as last
        updateEditChrome();                     // re-anchor the toolbar to the moved region
        rescanBarcode();
        updateAllViews();
        return;
    }
    const bool wasClick = !m_sel->isValid();   // press+release with no drag area
    m_sel->commit();
    // A bare click adopts the auto-detected window/element region (queried fresh).
    if (wasClick) {
        // Resolve it under the PRESS point, not the release point: the gate lets a click
        // end up to a slop radius from where it began, and the user meant the thing they
        // pressed on. Restored straight after so the magnifier keeps tracking the cursor.
        const QPoint at = m_lastCursor;
        if (gatedClick)
            m_lastCursor = m_pressPos;
        refreshDetection(true);
        m_lastCursor = at;
        if (hasSuggestion()) {
            m_sel->setRect(m_suggested);
            m_sel->commit();
        }
    }
    // Never let the user settle a 1x1 region (snip/behavior/hide1x1).
    if (m_hide1x1 && m_sel->isValid()
        && m_sel->rect().width() <= 1 && m_sel->rect().height() <= 1) {
        m_sel->setRect(QRect());
        m_sel->commit();
    }
    if (m_sel->isValid()) {
        // One-shot (SnipCopy hotkey): the confirmed region goes straight to the
        // clipboard. finish() deletes this overlay, so nothing may touch it after.
        if (m_oneShotCopy) { copy(/*thenQuit=*/true); return; }
        // Quick-snip (hold-modifier gesture): run the chosen extra action(s) on the dragged
        // region — no annotate phase — then close. copy() with no editing flattens to the
        // bare base image; the last action closes the overlay.
        if (m_quickMode) {
            if (m_quickCopy && m_quickPin) { copy(/*thenQuit=*/false); onPin(/*thenQuit=*/true); }
            else if (m_quickCopy)          { copy(/*thenQuit=*/true); }
            else                           { onPin(/*thenQuit=*/true); }
            return;
        }
        // Barcode scanner (ScanBarcode hotkey): decode the finalized region and copy the
        // payload, or toast that nothing was found — either way the overlay closes.
        if (m_barcodeScanOnly) {
            rescanBarcode();
            if (!m_barcodePayload.isEmpty())
                ColorPicker::copyToClipboard(m_barcodePayload);
            else if (m_notifySink)
                m_notifySink(I18n::instance().t(QStringLiteral("snip.barcode.none_title")),
                             I18n::instance().t(QStringLiteral("snip.barcode.none_body")));
            finish();
            return;
        }
        beginEditing();
    }
    rescanBarcode();          // first scan of the freshly dragged region
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::onDoubleClick()
{
    // While placing a line, a fast second click is just another vertex — don't let it
    // fall through to copy-and-close.
    if (m_editing && isLineTool() && m_linePhase != LinePhase::Idle) {
        handleLinePress(m_lastCursor, QGuiApplication::keyboardModifiers());
        return;
    }
    // Same for the number label: every click is a discrete action (stamp / renumber /
    // step), so a fast second click is always another one of those — never copy-and-close.
    // Qt sends this INSTEAD of the second press, so forward it as an ordinary click:
    // swallowing it would silently drop every other click while tapping +/−.
    if (m_editing && m_tool == annotation::ToolId::NumberLabel) {
        handleLabelPress(m_lastCursor);
        return;
    }
    if (m_sel->isValid())
        runSnipAction(m_doubleClickAction, m_doubleClickQuit);
}

void SnipOverlay::onAbort()
{
    // Esc leaves Move mode first, rather than throwing the whole capture away — the
    // mode is the innermost thing to back out of.
    if (m_scrollMoving) {
        setScrollMoveMode(false);
        return;
    }
    // A running scrolling session is the next thing in: leaving it drops the stitched
    // image and lands on a fresh capture of the same region, ready to annotate.
    // A second Esc from there closes the capture the ordinary way.
    if (m_scrolling) {
        cancelScrollCapture();
        return;
    }
    // In OCR mode, Esc / right-click leaves OCR first (back to annotate), not the capture.
    if (m_ocrActive) {
        if (m_paintBar) m_paintBar->setOcrChecked(false);
        toggleOcr(false);
        return;
    }
    // A right-click while the history strip is open just closes the strip.
    if (m_stripOpen) { closeHistoryStrip(); return; }
    // While a text box is open, a right-click confirms (bakes) that input rather than
    // closing the whole capture.
    if (m_textEditor) {
        commitTextEditor();
        return;
    }
    // While placing/adjusting a line, a right-click advances or finalizes it instead
    // of closing the overlay.
    if (handleLineSecondary())
        return;
    // Alt+right-click with a rect-family tool marks the UI element under the cursor.
    // A PLAIN right-click is untouched and still closes the capture.
    if (applyQuickMark())
        return;
    finish();
}

void SnipOverlay::onKey(int key, Qt::KeyboardModifiers mods)
{
    // OCR mode is a text-selection surface: keys act on the recognized text, not the snip.
    if (m_ocrActive) {
        const bool ctrl = mods & Qt::ControlModifier;
        if (key == Qt::Key_C && ctrl && !(mods & Qt::AltModifier)) {
            // Ctrl+C: copy the selected text and leave OCR (back to the annotate toolbar).
            if (m_ocrLayer.hasSelection()) {
                copyOcrText();
                if (m_paintBar) m_paintBar->setOcrChecked(false);
                toggleOcr(false);
            }
            return;
        }
        if (key == Qt::Key_A && ctrl) {          // Ctrl+A: select all recognized text
            m_ocrLayer.selectAll();
            updateAllViews();
            return;
        }
        if (key == Qt::Key_Escape) {             // Esc: leave OCR, keep the capture
            if (m_paintBar) m_paintBar->setOcrChecked(false);
            toggleOcr(false);
            return;
        }
        return;                                  // swallow other snip shortcuts while reading text
    }
    // Renumbering a badge in place: digits/Backspace/Enter/Esc belong to the badge, not
    // to the snip shortcuts (otherwise "1" etc. would trigger them). Anything else falls
    // through untouched.
    if (handleLabelEditKey(key))
        return;
    switch (key) {
        case Qt::Key_G:                                // G: crosshair guide lines on/off
            toggleAuxLines();
            return;
        case Qt::Key_Alt:
            // Alt going down arms quick-mark: show the preview right away instead of
            // making the user jiggle the mouse to get the next onMove(). Belt-and-braces
            // — Windows usually eats a lone Alt, so the mouse-move path is what normally
            // raises the preview, and onAltReleased() is what tears it down.
            refreshQuickMark();
            return;
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            if (m_shapeSel) { deleteSelectedShape(); return; }   // Del removes the selected shape
            break;
        case Qt::Key_Escape:
            // Esc unwinds the innermost thing first, and a scrolling capture's Move mode
            // is inner to the capture itself: it must not throw the whole session away.
            // (The right-click path in onAbort() does the same; this is the KEYBOARD one,
            // which finishes directly rather than going through it.)
            if (m_scrollMoving) { setScrollMoveMode(false); return; }
            // ...and the session itself is inner to the capture: leaving it keeps the
            // snip, on a freshly grabbed region. The next Esc closes the capture.
            if (m_scrolling) { cancelScrollCapture(); return; }
            if (m_shapeSel) { deselectShape(); return; }         // Esc first drops a shape selection
            if (m_stripOpen) { closeHistoryStrip(); return; }   // Esc closes the strip only
            // Only worth confirming once the region is COMMITTED and there is a capture to
            // lose. While the region is still being chosen, Esc is just "never mind" and a
            // question about discarding it would be noise.
            if (m_confirmEsc && m_editing) {
                m_suspendFocusQuit = true;             // the confirm dialog deactivates the app
                // The frozen overlay deliberately stays up — you should see what you are
                // about to discard — but a floated bar is always-on-top and would sit over
                // the question itself.
                const QVector<QWidget*> bars = hideFloatingBars();
                // Parent it to a view rather than to nothing: the views are always-on-top,
                // so an unparented dialog opens BEHIND the overlay and looks like the app
                // has hung. A window owned by one of them is always above it, and this
                // also puts the question on the monitor the selection is on.
                QWidget* owner = nullptr;
                if (m_sel->isValid()) {
                    const int hi = m_layout.indexForCapture(m_sel->rect().center());
                    if (hi >= 0 && hi < m_views.size())
                        owner = m_views[hi];
                }
                if (!owner)
                    owner = m_views.value(0);
                QMessageBox box(QMessageBox::Question,
                                I18n::instance().t(QStringLiteral("snip.esc_confirm_title")),
                                I18n::instance().t(QStringLiteral("snip.esc_confirm_text")),
                                QMessageBox::Yes | QMessageBox::No, owner);
                auto* dontAsk = new QCheckBox(
                    I18n::instance().t(QStringLiteral("snip.esc_confirm_dont_ask")));
                box.setCheckBox(dontAsk);              // takes ownership
                const int btn = box.exec();
                m_suspendFocusQuit = false;
                if (btn == QMessageBox::Yes) {
                    // The tick only counts alongside a confirmed discard: ticking it and
                    // then backing out must not change a setting behind the user's back.
                    if (dontAsk->isChecked() && m_appSettings)
                        m_appSettings->setValue(QStringLiteral("snip/keys/confirmEsc"), false);
                    finish();
                    return;
                }
                for (QWidget* w : bars)                // answered No: carry on as before
                    w->show();
                return;
            }
            finish();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_stripOpen) { pickHistoryStrip(); return; }    // Enter picks the highlighted snip
            runSnipAction(m_enterAction, m_enterQuit); return;
        case Qt::Key_C:
            if (mods & Qt::ControlModifier) {
                if (mods & Qt::AltModifier) copy(true, /*asFile=*/true);  // Ctrl+Alt+C: copy as file
                else                        copy();                       // Ctrl+C: copy the snip
            } else {
                copyColor();                                            // C: copy the pixel color
            }
            return;
        case Qt::Key_S:
            if (mods & Qt::ControlModifier) {
                if (mods & Qt::ShiftModifier) quickSave();   // Ctrl+Shift+S: silent quick-save
                else                          save();        // Ctrl+S: Save dialog
                return;
            }
            nudgeCursor(0, 1);                               // S: move the cursor down 1px
            return;
        case Qt::Key_A:
            if (mods & Qt::ControlModifier) {               // Ctrl+A: select the whole desktop
                m_sel->selectFullscreen();
                m_sel->commit();
                beginEditing();
                updateEditChrome();
                updateAllViews();
                return;
            }
            nudgeCursor(-1, 0);                              // A: move the cursor left 1px
            return;
        case Qt::Key_W: nudgeCursor(0, -1); return;         // W: move the cursor up 1px
        case Qt::Key_D: nudgeCursor(1, 0);  return;         // D: move the cursor right 1px
        case Qt::Key_T: if (mods & Qt::ControlModifier) { onPin(); return; } break;   // pin to screen
        case Qt::Key_Z:
            if (mods & Qt::ControlModifier) {
                if (mods & Qt::ShiftModifier) clearAnnotations();   // Ctrl+Shift+Z: clear all
                else                          undoAnnotation();     // Ctrl+Z: undo
                return;
            }
            break;
        case Qt::Key_Y:
            if (mods & Qt::ControlModifier) { redoAnnotation(); return; }   // Ctrl+Y: redo
            break;
        case Qt::Key_Space:                            // toggle the paint bar
            if (m_editing) {
                m_paintBarHidden = !m_paintBarHidden;
                updateEditChrome();
                return;
            }
            break;
        case Qt::Key_Tab: {
            // Cycle the auto-region detection mode (off → window → element).
            m_detectMode = m_detectMode == DetectMode::Off    ? DetectMode::Window
                         : m_detectMode == DetectMode::Window ? DetectMode::Element
                                                              : DetectMode::Off;
            if (!m_sel->isValid())
                refreshDetection(true);                // mode changed: re-query now
            updateAllViews();
            return;
        }
        case Qt::Key_Shift:                            // move the copy-target row down the readout
            if (!m_enabledFormats.isEmpty()) {
                m_formatIdx = (m_formatIdx + 1) % m_enabledFormats.size();
                if (m_appSettings)                     // persist so the choice survives across snips
                    m_appSettings->setValue(QStringLiteral("interface/magnifier/selectedFormat"),
                                            ColorPicker::codeFor(colorFormat()));
            }
            updateAllViews();
            return;
        case Qt::Key_F5:                               // refresh the frozen desktop (hold = continuous)
            beginRefresh(RefreshSource::Key);
            return;
        case Qt::Key_QuoteLeft:                        // ` (or !) toggles cursor capture live + export
        case Qt::Key_Exclam:
            if (m_disableCursorCapture) return;        // master "disable completely" locks the toggle
            m_cursorCaptureOn = !m_cursorCaptureOn;
            m_captureCursor = m_cursorCaptureOn;
            if (m_appSettings)                         // persist so the Preferences checkbox reflects it
                m_appSettings->setValue(QStringLiteral("snip/captureCursor"), m_cursorCaptureOn);
            updateAllViews();
            return;
        case Qt::Key_Comma:                            // `,` history strip: open / step older
            // Opens only from the region-choosing state, but once the strip is open it keeps
            // stepping even though restoring an entry now enters edit mode (toolbar shown).
            if (m_stripOpen || !m_editing) { historyStripKey(+1); return; }
            break;
        case Qt::Key_Period:                           // `.` history strip: open / step newer
            if (m_stripOpen || !m_editing) { historyStripKey(-1); return; }
            break;
        case Qt::Key_R:                                // `R`/`Shift+R` reuse an older/newer area
            if (!m_editing) { reuseArea((mods & Qt::ShiftModifier) ? 1 : -1); return; }
            break;
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down: {
            const SelectionModel::Edge edge =
                key == Qt::Key_Left  ? SelectionModel::Edge::Left  :
                key == Qt::Key_Right ? SelectionModel::Edge::Right :
                key == Qt::Key_Up    ? SelectionModel::Edge::Top   :
                                       SelectionModel::Edge::Bottom;
            if (mods & Qt::ControlModifier)    m_sel->grow(edge);
            else if (mods & Qt::ShiftModifier) m_sel->shrink(edge);
            else {
                const QPoint d = key == Qt::Key_Left  ? QPoint(-1, 0) :
                                 key == Qt::Key_Right ? QPoint(1, 0)  :
                                 key == Qt::Key_Up    ? QPoint(0, -1) : QPoint(0, 1);
                m_sel->moveBy(d.x(), d.y());
            }
            updateEditChrome();
            updateAllViews();
            return;
        }
        default: break;
    }
}

void SnipOverlay::onKeyRelease(int key)
{
    if (key == Qt::Key_F5)
        endRefresh(RefreshSource::Key);
    if (key == Qt::Key_Alt)
        clearQuickMark();          // letting go of Alt disarms quick-mark (if Qt sees it at all)
}

void SnipOverlay::nudgeCursor(int dx, int dy)
{
    // Move the OS cursor by exactly one CAPTURE (native physical) pixel. This posts a
    // mouse-move that flows back through SnipScreenView::mouseMoveEvent → onMove(), so
    // hover/detection, an in-progress drag, the magnifier and the C-copy colour all update as
    // if the mouse moved — which makes the "hold Mouse-left + W A S D" fine-adjust gesture work.
#ifdef Q_OS_WIN
    // Our process is PerMonitor-V2 DPI-aware, so GetCursorPos/SetCursorPos operate in physical
    // px — the same space as the snip pipeline's capture px (captureGeometry == rcMonitor). That
    // makes a 1-px step exact at any DPI. QCursor::setPos rounds to LOGICAL px instead, so on a
    // 150 % monitor one logical step is 1.5 physical px and W/A/S/D jump 1-2 px unevenly.
    POINT p;
    if (GetCursorPos(&p)) {
        SetCursorPos(int(p.x) + dx, int(p.y) + dy);
        return;
    }
#endif
    QCursor::setPos(QCursor::pos() + QPoint(dx, dy));   // non-Windows fallback (logical px)
}

QMap<QString, QString> SnipOverlay::filenameSpecials() const
{
    return {{QStringLiteral("title"), m_windowTitle}};
}

int SnipOverlay::outputQuality() const
{
    return m_appSettings
        ? m_appSettings->value(QStringLiteral("output/imageQuality"), -1).toInt()
        : -1;
}

bool SnipOverlay::removeAlphaOnSave() const
{
    return m_appSettings
        && m_appSettings->value(QStringLiteral("output/auto_alpha_channel_removal"), true).toBool();
}

PdfEncoder::PageMode SnipOverlay::pdfPageMode() const
{
    return PdfEncoder::pageModeFromString(
        m_appSettings
            ? m_appSettings->value(QStringLiteral("output/pdf/pageSize"), QString()).toString()
            : QString());
}

void SnipOverlay::copyImage(const QImage& img, bool asFile)
{
    if (img.isNull())
        return;
    if (!m_clipboard) {                 // no backend (tests / non-Windows) → plain Qt copy
        ImageExport::toClipboard(img);
        return;
    }
    platform::ImageClipboardOptions opt;
    if (m_appSettings) {
        opt.addFlatDib = m_appSettings->value(QStringLiteral("output/clipboard/dibScreenshot"),
                                              true).toBool();
        const QString mode = m_appSettings->value(QStringLiteral("output/clipboard/copyAsFile"),
                                                  QStringLiteral("disable")).toString();
        opt.addFile = (mode == QLatin1String("always"))
            || (mode == QLatin1String("hotkey") && asFile);   // hotkey mode: only via Ctrl+Alt+C
        // Name the temp file with the quick-save template basename (title/date tokens expanded).
        const QString tmpl = m_appSettings->value(QStringLiteral("output/quickSave/pathTemplate"),
                                                  QString()).toString();
        if (!tmpl.isEmpty())
            opt.fileName = QFileInfo(FilenameTemplate::expandNow(tmpl, filenameSpecials())).fileName();
        opt.quality = outputQuality();
    }
    m_clipboard->setImage(img, opt);
}

void SnipOverlay::copy(bool thenQuit, bool asFile)
{
    const QImage img = finalImage();
    if (!img.isNull()) {
        copyImage(img, asFile);
        recordHistory(img);
    }
    if (thenQuit) finish();
}

void SnipOverlay::recordHistory(const QImage& img)
{
    if (img.isNull())
        return;
    if (m_historySink)                      // crop + the full-screen freeze it was cut from
        m_historySink(img, m_shots, m_sel->rect());
    if (m_areaSink && m_sel->isValid())     // remember this area for `R` reuse
        m_areaSink(m_sel->rect());
    autoSaveIfEnabled(img);                 // Output→Auto Save: silent copy of every snip
}

void SnipOverlay::autoSaveIfEnabled(const QImage& img)
{
    if (m_autoSaved || img.isNull() || !m_appSettings)
        return;
    if (!m_appSettings->value(QStringLiteral("output/autoSave/enabled"), false).toBool())
        return;
    // The path template may be unset in config even though Preferences shows a default
    // (a pristine field never fires textChanged) — fall back to the same factory default.
    QString tmpl = m_appSettings->value(QStringLiteral("output/autoSave/pathTemplate"),
                                        QString()).toString();
    if (tmpl.isEmpty())
        tmpl = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                   .filePath(QStringLiteral("SnapCap_AutoSave/SnapCap_$yyyy-MM-dd_HH-mm-ss$.png"));
    const QString path = FilenameTemplate::expandNow(tmpl, filenameSpecials());
    QDir().mkpath(QFileInfo(path).path());
    if (ImageExport::toFile(img, path, outputQuality(), removeAlphaOnSave(), nullptr, pdfPageMode()))
        m_autoSaved = true;                 // one auto-save per snip, even if it's also copied/pinned
}

void SnipOverlay::historyStripKey(int older)
{
    if (!m_stripOpen) { openHistoryStrip(); return; }   // first `,`/`.` opens at the newest
    if (older > 0) {                                    // `,` → step toward older
        m_stripIndex = std::min(m_stripIndex + 1, int(m_stripThumbs.size()) - 1);
        applyHistoryFrame(m_stripIndex);
    } else {                                            // `.` → step toward newer
        if (m_stripIndex == 0) {                        // past the newest → back to live
            restoreLiveBackground();
            closeHistoryStrip();
            return;
        }
        --m_stripIndex;
        applyHistoryFrame(m_stripIndex);
    }
    updateAllViews();
}

void SnipOverlay::openHistoryStrip()
{
    if (!m_historyThumbs)
        return;
    m_stripThumbs = m_historyThumbs();
    if (m_stripThumbs.isEmpty())
        return;                         // nothing recorded yet — no-op
    // Stash the live freeze once so `.` past the newest can return to the present desktop.
    if (!m_liveStashed) { m_liveShots = m_shots; m_liveStashed = true; }
    m_stripOpen = true;
    m_stripIndex = 0;                   // highlight the newest snip
    applyHistoryFrame(0);              // restore its full screen + area for a re-snip
    updateAllViews();
}

void SnipOverlay::closeHistoryStrip()
{
    m_stripOpen = false;
    m_stripThumbs.clear();
    updateAllViews();
}

void SnipOverlay::applyHistoryFrame(int index)
{
    if (!m_historyRestore)
        return;
    const HistoryFrame f = m_historyRestore(index);
    if (f.shots.isEmpty())
        return;                         // legacy crop-only entry — leave the background as-is

    // Swap each monitor's frozen pixels to the stored freeze. Match a stored screen to a
    // live view by geometry (fall back to positional order) so the window stays pinned to
    // a real monitor while showing the past desktop. Selection coords stay in the shared
    // global-logical space, exact when the monitor layout is unchanged.
    QVector<bool> used(m_views.size(), false);
    for (const ScreenShot& hs : f.shots) {
        int vi = -1;
        for (int i = 0; i < m_views.size(); ++i)
            if (!used[i] && m_views[i]->targetScreen()
                && m_views[i]->targetScreen()->geometry() == hs.geo) { vi = i; break; }
        if (vi < 0)
            for (int i = 0; i < m_views.size(); ++i)
                if (!used[i]) { vi = i; break; }
        if (vi < 0)
            break;
        used[vi] = true;
        m_views[vi]->setBackground(hs.shot, hs.dpr);
        if (vi < m_shots.size()) {       // keep the live geo/screen; swap only pixels + dpr
            m_shots[vi].shot = hs.shot;
            m_shots[vi].dpr  = hs.dpr;
        }
    }

    m_cursorCaptureOn = false;           // the freeze carried no live cursor to composite
    if (m_doc)
        m_doc->clearAll();               // drop annotations drawn on the previous background
    m_sel->setRect(f.selection);
    m_sel->commit();
    if (m_sel->isValid())
        beginEditing();                  // toolbar + annotation, exactly like a manual selection
    updateEditChrome();
    m_historyActive = true;              // a drag outside the region re-snips this past screen
}

void SnipOverlay::restoreLiveBackground()
{
    if (!m_liveStashed)
        return;
    m_shots = m_liveShots;
    for (int i = 0; i < m_views.size() && i < m_shots.size(); ++i)
        m_views[i]->setBackground(m_shots[i].shot, m_shots[i].dpr);
    m_editing = false;
    m_historyActive = false;             // back on the live desktop: normal snip semantics
    m_sel->setRect(QRect());             // clear the selection back to "choose a region"
    m_sel->commit();
    updateEditChrome();
}

void SnipOverlay::pickHistoryStrip()
{
    if (!m_stripOpen || !m_historyPick)
        return;
    const QImage img = m_historyPick(m_stripIndex);
    if (!img.isNull())
        copyImage(img);
    finish();
}

SnipOverlay::StripLayout SnipOverlay::historyStripLayout() const
{
    StripLayout lay;
    if (!m_stripOpen || m_stripThumbs.isEmpty())
        return lay;

    // Anchor to the screen under the cursor (else the first screen). All geometry is
    // capture px; the cell metrics are scaled by that monitor's scale so the strip keeps
    // its on-screen size (the view maps the panel back to local-logical to draw it).
    const ScreenShot* anchor = m_shots.isEmpty() ? nullptr : &m_shots.first();
    for (const ScreenShot& s : m_shots)
        if (m_haveCursor && s.captureGeo.contains(m_lastCursor)) { anchor = &s; break; }
    if (!anchor || !anchor->captureGeo.isValid())
        return lay;
    const QRect screen = anchor->captureGeo;
    const qreal sc = anchor->geo.width() > 0
                         ? anchor->captureGeo.width() / qreal(anchor->geo.width()) : 1.0;
    auto S = [sc](int v) { return int(std::lround(v * sc)); };
    const int cellW = S(96), cellH = S(72), gap = S(8), pad = S(12), footerH = S(20);

    // How many cells fit across ~90% of the screen, then window around the selection.
    const int avail = int(screen.width() * 0.9) - 2 * pad;
    const int maxVisible = std::max(1, (avail + gap) / (cellW + gap));
    const HistoryStrip::Window w =
        HistoryStrip::window(m_stripThumbs.size(), m_stripIndex, maxVisible);

    const int panelW = 2 * pad + w.len * cellW + (w.len - 1) * gap;
    const int panelH = 2 * pad + cellH + footerH;
    const int px = screen.center().x() - panelW / 2;
    const int py = screen.bottom() - S(60) - panelH;   // float above the screen bottom
    lay.panel = QRect(px, py, panelW, panelH);
    lay.start = w.start;
    lay.selectedSlot = m_stripIndex - w.start;
    // Thumbnails are newest-first; place them right-to-left so the newest sits on the
    // RIGHT and the oldest on the LEFT — matching `,` (older ← left) / `.` (newer → right).
    for (int i = 0; i < w.len; ++i) {
        const int col = w.len - 1 - i;   // mirror: slot i (newer) → rightmost column
        lay.thumbRects.append(QRect(px + pad + col * (cellW + gap), py + pad, cellW, cellH));
        lay.thumbs.append(m_stripThumbs.at(w.start + i));
    }
    return lay;
}

bool SnipOverlay::handleStripClick(const QPoint& g)
{
    if (!m_stripOpen)
        return false;
    const StripLayout lay = historyStripLayout();
    if (!lay.panel.contains(g))
        return false;                   // outside the strip — not ours to handle
    for (int i = 0; i < lay.thumbRects.size(); ++i) {
        if (lay.thumbRects.at(i).contains(g)) {
            m_stripIndex = lay.start + i;
            pickHistoryStrip();
            return true;
        }
    }
    return true;                        // inside the panel but between thumbs — swallow
}

void SnipOverlay::reuseArea(int dir)
{
    if (!m_areaNav)
        return;
    const QRect r = m_areaNav(dir);
    if (r.isNull())
        return;                         // at an end of the ring — stay put
    // Restore a past selection rect and stay in the overlay (unlike `,`/`.` replay,
    // which copies + closes) so the user can finish or tweak it.
    m_sel->setRect(r);
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::beginEditing()
{
    if (m_editing)
        return;
    m_editing = true;
    if (!m_paintBar) {
        m_paintBar = new PaintBar();        // parented to the host view in updateEditChrome()
        m_paintBar->setSettings(m_appSettings);
        m_style.color = m_paintBar->currentColor();
        m_style.penWidth = m_paintBar->penWidth();
        m_style.redactStrength = m_paintBar->redactStrength();
        m_style.filled = m_paintBar->currentFilled();
        m_style.startCap = m_paintBar->currentStartCap();
        m_style.endCap = m_paintBar->currentEndCap();
        m_style.curved = m_paintBar->currentCurved();
        m_style.lineStyle = m_paintBar->currentLineStyle();
        m_style.bold = m_paintBar->currentBold();
        m_style.italic = m_paintBar->currentItalic();
        m_style.hollowText = m_paintBar->currentHollowText();
        m_style.fontFamily = m_paintBar->currentFontFamily();
        m_style.fontPx = m_paintBar->currentFontSize();
        m_style.label = m_paintBar->currentLabelStyle();
        m_style.numberStyle = m_paintBar->currentNumberStyle();
        connect(m_paintBar, &PaintBar::toolSelected, this, [this](annotation::ToolId t) {
            setTool(t);
            updateEditChrome();         // a tool's sub-panel (un)shows → resize + re-anchor
            applyRecordingInput();      // armed tool ⇒ the overlay takes clicks back
        });
        connect(m_paintBar, &PaintBar::colorSelected, this, [this](const QColor& c) { setColor(c); });
        connect(m_paintBar, &PaintBar::penWidthChanged, this, [this](int w) { setPenWidth(w); });
        connect(m_paintBar, &PaintBar::fillSelected, this, [this](bool f) {
            setFilled(f);
            updateEditChrome();         // the dash dropdown (un)shows → resize + re-anchor
        });
        connect(m_paintBar, &PaintBar::lineStyleChanged, this,
                [this](annotation::LineStyle s) { setLineStyle(s); });
        connect(m_paintBar, &PaintBar::startCapChanged, this,
                [this](annotation::CapStyle c) { setStartCap(c); });
        connect(m_paintBar, &PaintBar::endCapChanged, this,
                [this](annotation::CapStyle c) { setEndCap(c); });
        connect(m_paintBar, &PaintBar::curvedChanged, this,
                [this](bool c) { setCurved(c); });
        connect(m_paintBar, &PaintBar::labelStyleChanged, this,
                [this](annotation::LabelStyle s) { setLabelStyle(s); });
        connect(m_paintBar, &PaintBar::numberStyleChanged, this,
                [this](annotation::NumberStyle s) { setNumberStyle(s); });
        connect(m_paintBar, &PaintBar::boldChanged, this, [this](bool b) { setTextBold(b); });
        connect(m_paintBar, &PaintBar::italicChanged, this, [this](bool b) { setTextItalic(b); });
        connect(m_paintBar, &PaintBar::hollowTextChanged, this, [this](bool h) { setTextHollow(h); });
        connect(m_paintBar, &PaintBar::fontFamilyChanged, this,
                [this](const QString& f) { setTextFamily(f); });
        connect(m_paintBar, &PaintBar::fontSizeChanged, this, [this](qreal px) { setTextFontPx(px); });
        connect(m_paintBar, &PaintBar::redactStrengthChanged, this, [this](int s) { setRedactStrength(s); });
        connect(m_paintBar, &PaintBar::ocrToggled, this, [this](bool on) { toggleOcr(on); });
        connect(m_paintBar, &PaintBar::recordToggled, this, [this](bool on) { toggleRecordBar(on); });
        // Only the press starts a session. The button is disabled for the rest of it, so
        // it never emits false: Done and Cancel on the 2nd-level bar are the way out.
        connect(m_paintBar, &PaintBar::scrollToggled, this,
                [this](bool on) { if (on) startScrollCapture(); });
        // No recorder backend (built without FFmpeg) ⇒ the button explains itself
        // rather than silently doing nothing.
        m_paintBar->setRecordEnabled(m_recorder != nullptr,
                                     I18n::instance().t(QStringLiteral("record.unavailable_body")));
        connect(m_paintBar, &PaintBar::undoRequested, this, [this] { undoAnnotation(); });
        connect(m_paintBar, &PaintBar::redoRequested, this, [this] { redoAnnotation(); });
        connect(m_paintBar, &PaintBar::copyRequested, this, [this] { copy(); });
        connect(m_paintBar, &PaintBar::saveRequested, this, [this] { save(); });
        connect(m_paintBar, &PaintBar::pinRequested, this, [this] { onPin(); });
        connect(m_paintBar, &PaintBar::cancelRequested, this, [this] { onAbort(); });
    }
}

void SnipOverlay::updateEditChrome()
{
    QWidget* sub = m_paintBar ? m_paintBar->subBar() : nullptr;
    if (!m_editing || !m_sel->isValid() || m_paintBarHidden) {
        if (m_paintBar) m_paintBar->hide();
        if (sub) sub->hide();
        return;
    }
    if (!m_paintBar)
        return;

    // Host both bars on the screen view containing the selection (capture-px sel).
    const QRect sel = m_sel->rect();
    const int hi = m_layout.indexForCapture(sel.center());
    SnipScreenView* host = (hi >= 0 && hi < m_views.size()) ? m_views[hi] : nullptr;
    if (!host)
        return;
    // Not while the bars are floating: they are top-level windows during a recording,
    // and re-parenting them to a (click-through) view would make them unclickable.
    if (!m_barsFloating) {
        if (m_paintBar->parent() != host)
            m_paintBar->setParent(host);
        if (sub && sub->parent() != host)
            sub->setParent(host);
    }

    m_paintBar->adjustSize();
    const bool showSub = m_paintBar->subBarVisible();

    // The row stack under the main bar, top to bottom:
    //   row 2 — the tool sub-bar OR the OCR bar (mutually exclusive: entering OCR
    //           clears any active tool, so showSub is false then);
    //   row 3 — the recording bar, which is NOT exclusive with either. With no tool
    //           armed it simply takes row 2; arming one pushes it down a row.
    QWidget* toolRow = showSub ? sub : (m_ocrActive ? static_cast<QWidget*>(m_ocrBar) : nullptr);
    if (sub && toolRow != sub) sub->hide();
    if (m_ocrBar && toolRow != m_ocrBar) m_ocrBar->hide();
    QWidget* recRow = (m_recordBar && m_paintBar->recordChecked())
                          ? static_cast<QWidget*>(m_recordBar) : nullptr;
    if (m_recordBar && !recRow) m_recordBar->hide();
    // The scrolling bar takes row 2 and can never share with the others: entering the
    // session clears any tool and greys OCR + record out for good.
    QWidget* scrollRow = (m_scrollBar && m_scrolling)
                             ? static_cast<QWidget*>(m_scrollBar) : nullptr;
    if (m_scrollBar && !scrollRow) m_scrollBar->hide();

    QVector<QWidget*> rows;
    if (toolRow) rows.append(toolRow);
    if (scrollRow) rows.append(scrollRow);
    if (recRow)  rows.append(recRow);
    for (QWidget* r : rows) {
        // Floating bars are top-level windows; re-parenting them back to the host would
        // undo exactly what floatBars() set up.
        if (!m_barsFloating && r->parent() != host)
            r->setParent(host);
        r->adjustSize();
    }

    constexpr int kGap = 8;        // selection ↔ bars
    constexpr int kRowGap = 4;     // between stacked rows
    const int mw = m_paintBar->width(), mh = m_paintBar->height();
    int blockH = mh;
    for (QWidget* r : rows)
        blockH += kRowGap + r->height();

    // While the bars float they are top-level, so they are placed in GLOBAL logical
    // coords; otherwise in the host view's local coords. Either way the bounds are the
    // HOST MONITOR, never the whole virtual desktop: the desktop's bottom edge belongs to
    // whichever monitor reaches lowest, so on a stepped multi-monitor layout a selection
    // near the bottom of *this* screen still looked like it had room underneath and the
    // bars were placed off the screen entirely. (`host` is a full-screen window on the
    // monitor holding the selection, so its geometry is that monitor's rect.)
    const QRect area = m_barsFloating ? host->geometry()
                                      : QRect(QPoint(0, 0), host->size());
    const QRect lsel = m_barsFloating ? m_layout.captureRectToGlobalLogical(sel)
                                      : host->toLocalRect(sel);

    // Below the selection by preference; above it when that overflows the monitor; and
    // failing both — a selection tall enough to leave no room either side — inside its
    // top edge, which always fits. The x below right-aligns to the selection, so the
    // last case lands the bar in the selection's top-right corner.
    // A horizontal scrolling capture puts its preview strip under the selection when
    // there was no room above it; step below the strip rather than through it. (The
    // default placements never collide — the strip goes above, the bars below.)
    int yTop = lsel.bottom() + kGap + scrollStripBelowHeight();
    if (yTop + blockH > area.bottom() + 1) {
        const int above = lsel.top() - kGap - blockH;
        yTop = (above >= area.top()) ? above : qMax(area.top(), lsel.top() + kGap);
    }
    yTop = qBound(area.top(), yTop, std::max(area.top(), area.bottom() + 1 - blockH));

    // The main bar right-aligns to the selection; the rows left-align to the main bar so
    // they stay put as their widths change (the main bar's width is constant).
    const int mx = qBound(area.left(), lsel.right() - mw + 1,
                          std::max(area.left(), area.right() - mw));
    m_paintBar->move(mx, yTop);
    m_paintBar->show();
    m_paintBar->raise();

    int y = yTop + mh;
    for (QWidget* r : rows) {
        y += kRowGap;
        r->move(qBound(area.left(), mx, std::max(area.left(), area.right() - r->width())), y);
        r->show();
        r->raise();
        y += r->height();
    }
    syncPaintBarState();
}

void SnipOverlay::syncPaintBarState()
{
    if (m_paintBar)
        m_paintBar->setUndoRedoState(m_doc->canUndo(), m_doc->canRedo());
}

// ---- Screen recording ------------------------------------------------------------

bool SnipOverlay::recordingActive() const
{
    return m_recording || m_countdownLeft > 0;
}

void SnipOverlay::toggleRecordBar(bool on)
{
    if (!on) {
        // Turning the button off mid-session would strand a running recording, so it
        // means "stop" — the state machine, not just the bar, comes down.
        if (recordingActive()) { stopRecording(); return; }
        if (m_recordBar) m_recordBar->hide();
        updateEditChrome();
        return;
    }
    if (!m_recorder)
        return;
    if (!m_recordBar) {
        m_recordBar = new RecordBar();
        connect(m_recordBar, &RecordBar::startRequested, this, [this] { requestRecordStart(); });
        connect(m_recordBar, &RecordBar::stopRequested,  this, [this] { stopRecording(); });
        connect(m_recordBar, &RecordBar::pauseToggled, this, [this](bool paused) {
            if (!m_recorder || !m_recording) return;
            paused ? m_recorder->pause() : m_recorder->resume();
            m_recordBar->setState(paused ? RecordBar::State::Paused : RecordBar::State::Recording);
        });
        // Seed the delay from the same setting Preferences → Recording will own.
        if (m_appSettings)
            m_recordBar->setDelaySeconds(
                m_appSettings->value(QStringLiteral("recording/behavior/countdown"), 0).toInt());
        connect(m_recordBar, &RecordBar::delayChanged, this, [this](int s) {
            if (m_appSettings)
                m_appSettings->setValue(QStringLiteral("recording/behavior/countdown"), s);
        });
    }
    updateEditChrome();
}

void SnipOverlay::requestRecordStart()
{
    if (!m_recorder || recordingActive() || !m_sel->isValid())
        return;
    const int delay = m_recordBar ? m_recordBar->delaySeconds() : 0;
    if (delay <= 0) { startRecordingNow(); return; }

    // Count down on the same field, then start. The freeze is dropped up front so the
    // countdown is spent looking at the LIVE screen you are about to record — that is
    // the whole point of the delay.
    m_countdownLeft = delay;
    m_recordBar->setState(RecordBar::State::Counting);
    m_recordBar->setCountdown(m_countdownLeft);
    enterRecordingLive();
    if (!m_countdownTimer) {
        m_countdownTimer = new QTimer(this);
        m_countdownTimer->setInterval(1000);
        connect(m_countdownTimer, &QTimer::timeout, this, [this] {
            if (--m_countdownLeft <= 0) {
                m_countdownTimer->stop();
                startRecordingNow();
            } else {
                m_recordBar->setCountdown(m_countdownLeft);
            }
        });
    }
    m_countdownTimer->start();
}

void SnipOverlay::startRecordingNow()
{
    m_countdownLeft = 0;
    if (!m_recorder || !m_sel->isValid())
        return;

    m_recordRegion = recording::normalizeRegion(m_sel->rect(), m_layout.captureBounds());
    if (m_recordRegion.isEmpty()) {
        if (m_notifySink)
            m_notifySink(I18n::instance().t(QStringLiteral("record.failed_title")),
                         I18n::instance().t(QStringLiteral("record.error.region")));
        return;
    }

    platform::IRecorder::Request req;
    req.regionCapturePx = m_recordRegion;
    if (m_appSettings) {
        req.config.fps = recording::clampFps(
            m_appSettings->value(QStringLiteral("recording/video/fps"), req.config.fps).toInt());
        req.config.bitrateKbps =
            m_appSettings->value(QStringLiteral("recording/video/bitrateKbps"),
                                 req.config.bitrateKbps).toInt();
        // Audio sources (Preferences → Recording; defaults: desktop audio on,
        // microphone off).
        req.config.systemAudio =
            m_appSettings->value(QStringLiteral("recording/audio/system"),
                                 req.config.systemAudio).toBool();
        req.config.microphone =
            m_appSettings->value(QStringLiteral("recording/audio/mic"),
                                 req.config.microphone).toBool();
        req.config.audioBitrateKbps =
            m_appSettings->value(QStringLiteral("recording/audio/bitrateKbps"),
                                 req.config.audioBitrateKbps).toInt();
    }
    if (m_appSettings) {
        using namespace recording;
        auto get = [this](const QString& k, const QVariant& d) {
            return m_appSettings->value(QStringLiteral("recording/") + k, d);
        };
        const Config d;
        req.config.container = containerFromKey(
            get(QStringLiteral("video/container"), keyFor(d.container)).toString());
        req.config.codec = codecFromKey(
            get(QStringLiteral("video/codec"), keyFor(d.codec)).toString());
        req.config.hw = hwFromKey(
            get(QStringLiteral("video/hwPreference"), keyFor(d.hw)).toString());
        req.config.rateMode = rateModeFromKey(
            get(QStringLiteral("video/rateMode"), keyFor(d.rateMode)).toString());
        req.config.quality = get(QStringLiteral("video/quality"), d.quality).toInt();
        req.config.gifFps = get(QStringLiteral("gif/fps"), d.gifFps).toInt();
        req.config.gifMaxWidth = get(QStringLiteral("gif/maxWidth"), d.gifMaxWidth).toInt();
        req.config.gifLoop = get(QStringLiteral("gif/loop"), d.gifLoop).toBool();
        req.config.maxDurationMin =
            get(QStringLiteral("behavior/maxDurationMin"), 0).toInt();
        // A codec the chosen container cannot carry would fail at mux time with an
        // error no user could act on; fall back to one it can.
        if (req.config.container != Container::Gif
            && !supports(req.config.container, req.config.codec)) {
            const QVector<Container> fits = containersFor(req.config.codec);
            if (!fits.isEmpty())
                req.config.container = fits.first();
        }
    }
    // The Recording page owns the cursor setting; the snip's own toggle no longer
    // applies once the freeze is gone.
    req.config.captureCursor =
        m_appSettings ? m_appSettings->value(QStringLiteral("recording/behavior/captureCursor"),
                                             true).toBool()
                      : m_captureCursor;
    req.outputPath = recording::resolveOutputPath(
        m_appSettings ? m_appSettings->value(QStringLiteral("recording/output/pathTemplate"),
                                             recording::defaultPathTemplate()).toString()
                      : recording::defaultPathTemplate(),
        req.config.container, QDateTime::currentDateTime(), filenameSpecials());

    // The callbacks outlive this overlay in one real case: Esc (or any other abort)
    // tears the overlay down while the worker is still finalizing the file. So the
    // overlay is held weakly, while the toast sink — which belongs to the app, not to
    // us — is copied by value so the "saved" notification still arrives.
    QPointer<SnipOverlay> self(this);
    auto notify = m_notifySink;
    // Captured by value: the preview must still open when the overlay is torn down
    // first (Esc mid-recording), and this outlives it.
    const bool wantPreview =
        m_appSettings ? m_appSettings->value(QStringLiteral("recording/output/openPreview"),
                                             true).toBool()
                      : true;

    platform::IRecorder::Callbacks cb;
    cb.onTick = [self](qint64 ms) {
        if (self && self->m_recordBar && self->m_recording)
            self->m_recordBar->setElapsed(ms);
    };
    cb.onFinished = [self, notify, wantPreview](bool ok, const QString& path, const QString& message) {
        if (notify) {
            notify(I18n::instance().t(ok ? QStringLiteral("record.saved_title")
                                         : QStringLiteral("record.failed_title")),
                   ok ? QDir::toNativeSeparators(path) : message);
        }
        if (self) {
            self->m_recording = false;
            self->finish();        // the session ends with the recording
        }
        // The file is already saved, so this is review-only: it opens AFTER the
        // overlay is gone, and closing it can never lose the take.
        if (ok && wantPreview) {
            auto* dlg = new RecordPreviewDialog(path);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        }
    };

    if (!m_recorder->start(req, cb)) {
        if (m_notifySink)
            m_notifySink(I18n::instance().t(QStringLiteral("record.failed_title")),
                         m_recorder->error());
        m_recordBar->setState(RecordBar::State::Idle);
        return;
    }
    m_recording = true;
    m_recordBar->setState(RecordBar::State::Recording);
    enterRecordingLive();          // no-op when the countdown already did it
    pushRecordingOverlay();        // carry across anything drawn before Record was pressed
}

void SnipOverlay::stopRecording()
{
    if (m_countdownTimer) m_countdownTimer->stop();
    m_countdownLeft = 0;
    if (m_recording && m_recorder) {
        m_recorder->stop();        // onFinished fires the toast and closes the overlay
        return;
    }
    finish();                      // stopped during the countdown: nothing was written
}

void SnipOverlay::enterLiveRegion()
{
    for (SnipScreenView* v : m_views)
        v->setRecordingLive(true);
    floatBars(true);
}

void SnipOverlay::exitLiveRegion()
{
    for (SnipScreenView* v : m_views) {
        v->setRecordingLive(false);
        v->setClickThrough(false);      // the overlay wants its own clicks back
    }
    updateAllViews();
}

void SnipOverlay::enterRecordingLive()
{
    enterLiveRegion();
    // Redaction is off for the rest of the session: it samples the pixels underneath,
    // and the layer handed to the recorder is transparent there.
    if (m_paintBar)
        m_paintBar->setRedactEnabled(false,
            I18n::instance().t(QStringLiteral("record.redact_disabled")));
    applyRecordingInput();
    updateEditChrome();
    updateAllViews();
}

bool SnipOverlay::recordingInputArmed() const
{
    // "The overlay wants the mouse over the live region" — broader than its name: a
    // recording arms it for a drawing tool, a scrolling capture for Move mode's drag.
    // The view turns this into the alpha-1 veil that makes those pixels hittable at all.
    return toolSelected() || textEditing() || m_scrollMoving;
}

void SnipOverlay::applyRecordingInput()
{
    if (!recordingActive())
        return;
    // A tool (or an open text box) needs the clicks; otherwise they belong to whatever
    // is being recorded. Clearing WS_EX_TRANSPARENT is only half of it: the views are
    // per-pixel-alpha layered windows, which Windows hit-tests by ALPHA, so the repaint
    // below is what actually puts hittable pixels over the region.
    for (SnipScreenView* v : m_views)
        v->setClickThrough(!recordingInputArmed());
    updateAllViews();
}

void SnipOverlay::pushRecordingOverlay()
{
    if (!m_recorder || !m_recording || m_recordRegion.isEmpty())
        return;
    // Flatten over a TRANSPARENT base: what comes back is the annotation layer alone,
    // which the recorder composites onto each captured frame. Redaction tools are
    // disabled while recording precisely because they would sample this transparency
    // instead of the live pixels.
    QImage layer(m_recordRegion.size(), QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    m_recorder->setOverlay(m_doc->flatten(layer, m_recordRegion));
}

// ---- Scrolling capture -----------------------------------------------------------

// How long the wheel must be quiet before the region is grabbed. Long enough that
// one continuous scroll gesture produces one grab, short enough to feel immediate.
static constexpr int kScrollIdleMs = 150;
// Auto-scroll aims each step at this much of the region, leaving the rest as overlap for
// the stitcher to match on.
// Half a region, not more: browsers ACCELERATE repeated wheel notches, so a step moves
// further than its measured pixels-per-notch predicts, and the margin absorbs that.
static constexpr double kAutoStepFraction = 0.50;
// Wheel notches per step. Starts at 1 — deliberately timid, since a notch is worth wildly
// different pixel counts across apps and zoom levels, and one overshoot costs a warning.
static constexpr int kAutoNotchMin = 1;
static constexpr int kAutoNotchMax = 20;
// Steps that add nothing before we call it the end of the page. More than one, because a
// slow-rendering page can miss a single beat.
static constexpr int kAutoStallsToStop = 3;
// Consecutive overshoots tolerated before auto-scroll gives up. An overshoot is recovered
// by undoing the step and halving it, so a couple of these is a normal adjustment rather
// than a failure.
static constexpr int kAutoMaxBackoffs = 3;
// Breather between a frame landing and the next injection, so a smooth-scrolling app has
// settled before we move it again.
static constexpr int kAutoStepGapMs = 90;
// Move mode keeps at least this much of the region overlapping what is already captured,
// so a move always lands on matchable content instead of stranding the session.
static constexpr double kMoveMinOverlap = 0.40;
// How long after injecting to wait before grabbing — the page has to have finished
// animating the scroll. Matches the idle debounce a hand-driven scroll gets.
static constexpr int kAutoSettleMs = 250;

// A horizontal session feeds TRANSPOSED frames to the ordinary vertical matcher (why:
// see startScrollCapture() in the header), which is why ScrollStitcher needed no changes at all for this axis. Clockwise:
// content arriving at the region's RIGHT edge then lands at the bottom of the rotated
// frame and is appended, exactly as content arriving at the bottom is when scrolling
// down. The canvas is rotated back by the same angle, negated, at Done.
static QImage rotateForStitch(const QImage& img)
{
    return img.transformed(QTransform().rotate(90));
}
static QImage rotateFromStitch(const QImage& img)
{
    return img.transformed(QTransform().rotate(-90));
}

// Move mode's hint names the axis it drags along.
static QString moveHint(bool horizontal)
{
    return I18n::instance().t(horizontal ? QStringLiteral("scroll.move.hint_h")
                                         : QStringLiteral("scroll.move.hint"));
}

void SnipOverlay::startScrollCapture()
{
    auto refuse = [this](const char* key) {
        if (m_notifySink)
            m_notifySink(I18n::instance().t(QStringLiteral("scroll.title")),
                         I18n::instance().t(QString::fromLatin1(key)));
        if (m_paintBar) m_paintBar->setScrollChecked(false);
    };
    if (!m_wheelWatch) { refuse("scroll.unavailable"); return; }
    if (!m_sel->isValid()) { refuse("scroll.error.region"); return; }
    // Every frame is grabbed with our own overlay still on screen, which only works
    // because the views are excluded from capture (start()). Without that exclusion —
    // an OS older than Win10 2004 — each grab would come back full of our dimming mask
    // and the whole session would stitch garbage. Refuse rather than produce it.
    if (!m_captureExcluded) { refuse("scroll.unavailable"); return; }

    // One monitor only. A region straddling a seam has no coherent scroll, and
    // stitching across a mixed-DPI boundary would compound two coordinate systems.
    const QRect sel = m_sel->rect();
    const int home = m_layout.indexForCapture(sel.topLeft());
    if (home < 0
        || m_layout.indexForCapture(QPoint(sel.right(), sel.top())) != home
        || m_layout.indexForCapture(QPoint(sel.left(), sel.bottom())) != home
        || m_layout.indexForCapture(sel.bottomRight()) != home) {
        refuse("scroll.error.multi_monitor");
        return;
    }

    // The tools go for the whole session. Anything already drawn is discarded —
    // it is anchored to the frozen desktop and would land arbitrarily on a tall image.
    // The output block goes with them: while the session runs the whole first-level
    // bar is inert, and row 2's Done/Cancel are the only way on or out.
    m_doc->reset();                  // hard: not undoable, and undo is about to go away
    if (m_paintBar) {
        m_paintBar->setToolsEnabledForSession(false,
            I18n::instance().t(QStringLiteral("scroll.tools_disabled")));
        m_paintBar->setOutputEnabled(false);
        m_paintBar->setUndoRedoState(false, false);
    }
    m_tool = annotation::ToolId::None;

    m_scrollRegion = sel;
    m_stitcher = std::make_unique<scroll::ScrollStitcher>();
    m_scrolling = true;

    if (!m_scrollBar) {
        m_scrollBar = new ScrollCaptureBar();
        m_scrollBar->setAccent(m_accentColor);
        connect(m_scrollBar, &ScrollCaptureBar::doneRequested, this,
                [this] { finishScrollCapture(); });
        connect(m_scrollBar, &ScrollCaptureBar::cancelRequested, this,
                [this] { cancelScrollCapture(); });
        connect(m_scrollBar, &ScrollCaptureBar::autoScrollToggled, this, [this](bool on) {
            on ? startAutoScroll() : stopAutoScroll(QString());
        });
        connect(m_scrollBar, &ScrollCaptureBar::moveModeToggled, this,
                [this](bool on) { setScrollMoveMode(on); });
        connect(m_scrollBar, &ScrollCaptureBar::directionToggled, this,
                [this](bool horizontal) { setScrollDirection(horizontal); });
        connect(m_scrollBar, &ScrollCaptureBar::trimStartRequested, this,
                [this] { trimScrollCapture(scroll::ScrollStitcher::Side::Above); });
        connect(m_scrollBar, &ScrollCaptureBar::trimEndRequested, this,
                [this] { trimScrollCapture(scroll::ScrollStitcher::Side::Below); });
    }
    // The axis survives across snips, unlike Move and Auto: choosing it is a statement
    // about the content you shoot, not about this one session.
    m_scrollHorizontal = m_appSettings
                         && m_appSettings->value(QStringLiteral("scroll/direction"))
                                    .toString() == QLatin1String("horizontal");
    m_scrollBar->setDirection(m_scrollHorizontal);
    m_scrollBar->setDirectionEnabled(true);   // nothing stitched yet, so still free to flip
    // Cancel keeps the overlay alive, so the bar can be re-shown for a SECOND
    // session — it must not open still carrying the first one's warning, or offering to
    // trim sides that belonged to a canvas which no longer exists.
    m_scrollBar->setMessage(QString(), /*warning*/ false);
    m_scrollBar->setTrimEnabled(false, false);
    // Seed the readout before the bar is ever shown: the first frame IS the region, so
    // this is already the right answer and the bar never flashes a 0 x 0.
    m_scrollBar->setCapturedSize(m_scrollRegion.width(), m_scrollRegion.height());
    if (!m_scrollStrip) {
        m_scrollStrip = new ScrollStripWindow();
        m_scrollStrip->setAccent(m_accentColor);
        m_scrollStrip->setExcludedFromCapture(true);   // holds under BitBlt too: see there
    }
    m_scrollStrip->setHorizontal(m_scrollHorizontal);   // it outlives the session
    if (!m_scrollIdle) {
        m_scrollIdle = new QTimer(this);
        m_scrollIdle->setSingleShot(true);
        m_scrollIdle->setInterval(kScrollIdleMs);
        connect(m_scrollIdle, &QTimer::timeout, this, [this] { grabScrollFrame(); });
    }

    enterLiveRegion();
    for (SnipScreenView* v : m_views)      // no tool is armed, so the app gets the mouse
        v->setClickThrough(true);
    updateEditChrome();
    updateAllViews();

    // The first frame is the session's starting point, and it can be taken right away:
    // the views are excluded from capture, so what comes back is the app underneath, not
    // our mask — no repaint has to have happened first.
    grabScrollFrame();

    QPointer<SnipOverlay> self(this);
    m_wheelWatch->arm([self](QPoint posCapturePx, bool injected, bool horizontalWheel) {
        if (!self || !self->m_scrolling)
            return;
        // Ignore scrolling that happened somewhere else entirely: it cannot have moved
        // the content inside our region.
        if (!self->m_scrollRegion.contains(posCapturePx))
            return;
        // A horizontal session takes BOTH wheel axes, because the usual way to scroll a
        // page sideways is Shift+Wheel, which reaches us as an ordinary vertical event.
        // A vertical one takes only the vertical axis: a sideways scroll during it moves
        // the content where nothing can stitch it, so grabbing would buy a false
        // "scrolled too far" warning and nothing else.
        if (horizontalWheel && !self->m_scrollHorizontal)
            return;
        if (injected && self->m_autoScroll)
            return;      // our own wheel: auto-scroll sequences that grab itself
        // A real wheel while auto-scroll is running means the user is taking over, so
        // the two never fight.
        if (!injected && self->m_autoScroll)
            self->stopAutoScroll(QString());   // no message: the user knows what they did
        self->m_scrollIdle->start();       // restart: grab once the wheel goes quiet
    });
}

void SnipOverlay::grabScrollFrame()
{
    if (!m_scrolling || !m_capture || !m_stitcher)
        return;
    const int before = m_stitcher->canvas().height();
    const QImage grabbed = m_capture->grabRect(m_scrollRegion);
    const scroll::ScrollStitcher::Result r =
        m_stitcher->addFrame(m_scrollHorizontal ? rotateForStitch(grabbed) : grabbed);

    using R = scroll::ScrollStitcher::Result;
    if (m_scrollBar) {
        QString warn;
        if (r == R::NoOverlap)
            warn = I18n::instance().t(QStringLiteral("scroll.warn.no_overlap"));
        else if (r == R::Capped)
            warn = I18n::instance().t(QStringLiteral("scroll.warn.capped"));
        if (!warn.isEmpty() || r == R::Appended || r == R::Prepended)
            m_scrollBar->setMessage(warn, !warn.isEmpty());   // progress clears a stale note
        syncScrollBarState();             // size readout + what there is to trim
        m_scrollBar->setDoneEnabled(!m_stitcher->isEmpty());
        // One frame has no axis yet — it is just the region — so the direction is still
        // free to flip. The SECOND frame is what fixes it: from there the canvas is built
        // out of translations along one axis, and switching would contradict it.
        m_scrollBar->setDirectionEnabled(m_stitcher->frameCount() < 2);
    }
    if (m_scrollStrip)
        m_scrollStrip->setWarning(r == R::NoOverlap || r == R::Capped);
    updateScrollStrip();
    updateEditChrome();               // the bar's width tracks the size text

    if (m_autoScroll) {
        if (r == R::Capped)
            stopAutoScroll(QString());    // the warning above already says what happened
        else if (r == R::NoOverlap)
            autoScrollBackOff();          // overshot: undo it and go slower
        else
            afterAutoScrollFrame(m_stitcher->canvas().height() - before);
    }
}

void SnipOverlay::setScrollDirection(bool horizontal)
{
    // The axis outlives the session (it describes the content you shoot), so it is
    // persisted whether or not anything below acts on the change.
    if (m_appSettings)
        m_appSettings->setValue(QStringLiteral("scroll/direction"),
                                horizontal ? QStringLiteral("horizontal")
                                           : QStringLiteral("vertical"));
    if (!m_scrolling || m_scrollHorizontal == horizontal || !m_stitcher)
        return;
    m_scrollHorizontal = horizontal;
    stopAutoScroll(QString());        // it was driving the other axis
    if (m_scrollStrip)
        m_scrollStrip->setHorizontal(horizontal);
    if (m_scrollBar && m_scrollMoving)
        m_scrollBar->setMessage(moveHint(horizontal), /*warning*/ false);

    // Frames are transposed per axis, so the one already stitched has the wrong
    // dimensions for the new one and every later frame would be Rejected outright. The
    // button is only live while a single frame is captured — that frame is just the
    // region, committing to no axis — so re-grabbing it costs nothing.
    m_stitcher->reset();
    grabScrollFrame();
}

void SnipOverlay::setScrollMoveMode(bool on)
{
    if (!m_scrolling || m_scrollMoving == on)
        return;
    m_scrollMoving = on;
    m_moveDragging = false;
    if (on)
        stopAutoScroll(QString());   // the two would disagree about what a frame means

    // Taking the clicks back is what makes the region draggable — and it is also why the
    // wheel stops reaching the page while this is on: these views are per-pixel-alpha
    // layered windows that Windows hit-tests by ALPHA, so pixels hittable enough to drag
    // are hittable enough to swallow a wheel. Leaving the mode gives both back.
    for (SnipScreenView* v : m_views)
        v->setClickThrough(!on);
    if (m_scrollBar) {
        m_scrollBar->setMoveChecked(on);
        m_scrollBar->setMessage(on ? moveHint(m_scrollHorizontal) : QString(),
                                /*warning*/ false);
    }
    updateEditChrome();
    updateAllViews();
}

bool SnipOverlay::beginScrollMove(const QPoint& gCapture)
{
    if (!m_scrollMoving || !m_scrollRegion.contains(gCapture))
        return false;
    m_moveDragging = true;
    // Along the capture axis throughout: "top" is the region's leading edge, which is
    // its left edge for a horizontal capture.
    m_moveGrabDy = (m_scrollHorizontal ? gCapture.x() : gCapture.y())
                   - (m_scrollHorizontal ? m_scrollRegion.left() : m_scrollRegion.top());
    m_moveStartTop = m_scrollHorizontal ? m_scrollRegion.left() : m_scrollRegion.top();
    m_moveStartLive = m_stitcher ? m_stitcher->liveOffset() : 0;
    return true;
}

void SnipOverlay::applyScrollMove(const QPoint& gCapture)
{
    if (!m_moveDragging || !m_stitcher)
        return;
    const int h = scrollFrameLength();
    // One axis only, the capture's: the other coordinate never moves.
    int top = (m_scrollHorizontal ? gCapture.x() : gCapture.y()) - m_moveGrabDy;

    // Keep the region on its own monitor — it is a single-monitor session by construction
    // and a region half off the screen would grab undefined pixels.
    const int idx = m_layout.indexForCapture(m_scrollRegion.center());
    if (idx >= 0 && idx < m_shots.size()) {
        const QRect mon = m_shots[idx].captureGeo;
        top = m_scrollHorizontal ? qBound(mon.left(), top, mon.right() + 1 - h)
                                 : qBound(mon.top(), top, mon.bottom() + 1 - h);
    }

    // Clamp so the new position still overlaps the captured image by at least
    // kMoveMinOverlap of the region — a move can then never fail to match, which is the
    // whole point of doing this rather than leaving the user to aim.
    const int minOverlap = qMax(1, int(h * kMoveMinOverlap));
    const int canvasH = m_stitcher->canvas().height();
    const int predictedLive = m_moveStartLive + (top - m_moveStartTop);
    const int lo = minOverlap - h;                  // region entirely above the canvas top
    const int hi = canvasH - minOverlap;            // ...or below its bottom
    const int clampedLive = qBound(lo, predictedLive, hi);
    top += clampedLive - predictedLive;             // push the region back by the shortfall

    if (top == (m_scrollHorizontal ? m_scrollRegion.left() : m_scrollRegion.top()))
        return;
    m_sel->moveTopLeftTo(m_scrollHorizontal ? QPoint(top, m_scrollRegion.top())
                                            : QPoint(m_scrollRegion.left(), top));
    m_scrollRegion = m_sel->rect();

    // Move the strip's marker with the region, predicted rather than measured: a real
    // grab-and-stitch per mouse-move would cost ~26 ms each, and the arithmetic is exact
    // anyway while the page underneath is not scrolling. updateScrollStrip() reads this
    // instead of the stitcher's offset while a drag is in flight — the stitcher's is a
    // frame behind and would pin the marker in place, which is the one thing this must
    // not do.
    m_movePredictedLive = clampedLive;
    updateScrollStrip();
    updateEditChrome();               // the toolbars travel with the area, flipping side as needed
    updateAllViews();
}

void SnipOverlay::endScrollMove()
{
    if (!m_moveDragging)
        return;
    m_moveDragging = false;
    grabScrollFrame();                // fold the new position in; replaces the prediction
}

void SnipOverlay::startAutoScroll()
{
    if (!m_scrolling || m_autoScroll)
        return;
    m_autoScroll = true;
    m_autoNotches = kAutoNotchMin;
    m_autoStalls = 0;
    m_autoBackoffs = 0;
    m_autoLastSent = 0;
    if (m_scrollBar) {
        m_scrollBar->setAutoChecked(true);
        m_scrollBar->setMessage(QString(), false);
    }
    // Park the pointer over the content. Pressing this button was itself a click on the
    // toolbar, so the pointer is never in the right place to begin with, and an injected
    // wheel goes wherever the pointer is. From here on the pointer is the user's: moving
    // it back out is one of the ways to stop (autoScrollStep).
    QCursor::setPos(m_layout.captureRectToGlobalLogical(m_scrollRegion).center());
    autoScrollStep();
}

void SnipOverlay::stopAutoScroll(const QString& messageKey)
{
    if (!m_autoScroll)
        return;
    m_autoScroll = false;
    if (m_scrollBar) {
        m_scrollBar->setAutoChecked(false);
        if (!messageKey.isEmpty()) {
            m_scrollBar->setMessage(I18n::instance().t(messageKey), /*warning*/ false);
            // Unlike every other setMessage() site, both callers that pass a key here
            // return straight away. The message trails the buttons, so without a re-fit
            // the bar keeps its old width and the text lands outside it, unseen.
            updateEditChrome();
        }
    }
}

void SnipOverlay::autoScrollStep()
{
    if (!m_autoScroll || !m_scrolling)
        return;

    // The injected wheel goes to whatever sits under the pointer, so the pointer has to
    // be over the content. Moving it back out is the user's way of taking over, and it
    // stops us rather than dragging the pointer back.
    const QRect lsel = m_layout.captureRectToGlobalLogical(m_scrollRegion);
    if (!lsel.contains(QCursor::pos())) {
        stopAutoScroll(QStringLiteral("scroll.auto.pointer_left"));
        return;
    }
    m_autoLastSent = m_autoNotches;
    // Down, or right: auto-scroll only ever walks a page toward its far end.
    // The two wheels take opposite signs — vertical positive is UP, horizontal positive
    // is RIGHT. An app that ignores WM_MOUSEHWHEEL simply does not move, and the
    // end-of-content check below stops us with its message.
    if (m_scrollHorizontal)
        platform::injectHorizontalWheel(+m_autoNotches);
    else
        platform::injectVerticalWheel(-m_autoNotches);
    // Drive the grab ourselves rather than waiting for our own wheel to come back round
    // through the hook. Low-level hook delivery is lossy under load (Windows skips a hook
    // whose thread is busy), and a single dropped event would leave auto-scroll waiting
    // for a frame that never arrives, with nothing to restart it.
    QTimer::singleShot(kAutoSettleMs, this, [this] {
        if (m_autoScroll && m_scrolling)
            grabScrollFrame();
    });
}

void SnipOverlay::autoScrollBackOff()
{
    if (!m_autoScroll || !m_scrolling)
        return;
    if (++m_autoBackoffs > kAutoMaxBackoffs) {
        stopAutoScroll(QString());        // the "scroll back" warning already explains
        return;
    }
    // Undo the step that went too far, so there is overlap to match on again, and halve
    // the pace. Stopping outright was the original behaviour and it was wrong: a page
    // that accelerates its own scrolling overshoots routinely, and giving up on the first
    // one ends the session almost immediately.
    if (m_scrollHorizontal)
        platform::injectHorizontalWheel(-m_autoLastSent);
    else
        platform::injectVerticalWheel(+m_autoLastSent);
    m_autoNotches = qMax(kAutoNotchMin, m_autoLastSent / 2);
    m_autoStalls = 0;                     // a recovery frame is not evidence of an ending
    QTimer::singleShot(kAutoSettleMs, this, [this] {
        if (m_autoScroll && m_scrolling)
            grabScrollFrame();
    });
}

void SnipOverlay::afterAutoScrollFrame(int grewBy)
{
    if (!m_autoScroll || !m_scrolling)
        return;

    if (grewBy <= 0) {
        // Nothing new. Either the page has hit its end or it is still rendering; only a
        // run of these is convincing.
        if (++m_autoStalls >= kAutoStallsToStop) {
            stopAutoScroll(QStringLiteral("scroll.auto.reached_end"));
            return;
        }
    } else {
        m_autoStalls = 0;
        m_autoBackoffs = 0;               // a step that worked clears the overshoot streak
        // Tune the next step from what this one actually moved. Apps differ enormously
        // in pixels-per-notch, and the same app changes with zoom, so this is measured
        // rather than assumed. The step may at most DOUBLE: a frame grabbed while the
        // page was still animating under-reports how far it went, and without a cap that
        // one bad reading multiplies the next step into a guaranteed overshoot — measured
        // in the field going 1 → 2 → 10 notches and straight off the end.
        const int target = qMax(1, int(scrollFrameLength() * kAutoStepFraction));
        const int scaled = qRound(double(m_autoNotches) * target / grewBy);
        m_autoNotches = qBound(kAutoNotchMin, qMin(scaled, m_autoNotches * 2), kAutoNotchMax);
    }
    QTimer::singleShot(kAutoStepGapMs, this, [this] { autoScrollStep(); });
}

QRect SnipOverlay::scrollStripGeometry() const
{
    // GLOBAL LOGICAL coords: the strip is a top-level window. The last fallback in each
    // branch overlaps the region itself, which is safe only because the window is
    // excluded from capture (see ScrollStripWindow::setExcludedFromCapture).
    const QRect lsel = m_layout.captureRectToGlobalLogical(m_scrollRegion);
    const int idx = m_layout.indexForCapture(m_scrollRegion.center());
    const QRect screen = (idx >= 0 && idx < m_shots.size()) ? m_shots[idx].geo : lsel;
    const int t = ScrollStripWindow::kThickness;
    constexpr int kGap = 8;

    // The strip is only as long as the canvas drawn at its fixed scale, so it never shows
    // empty space ahead of what has been captured: it starts at one frame and extends as
    // the result grows. At the region's own length it stops, and from there the viewport
    // follows the live frame instead. The anchored edge — the region's top (↕) or
    // left (↔) — stays put whichever way the content is being added.
    const int full = m_scrollHorizontal ? lsel.width() : lsel.height();
    int len = full;
    if (m_stitcher && m_stitcher->canvas().width() > 0) {
        const QImage& canvas = m_stitcher->canvas();    // canvas space: width is ACROSS
        len = qBound(1, int(std::lround(double(canvas.height()) * t / canvas.width())),
                     full);
    }

    if (m_scrollHorizontal) {
        // Above by preference — that is the side the toolbars are not on — then below,
        // then inside the region's own top band.
        int y = lsel.top() - kGap - t;
        if (y < screen.top())
            y = lsel.bottom() + kGap;
        if (y + t > screen.bottom())
            y = qMax(screen.top(), lsel.top());
        return QRect(lsel.left(), y, len, t);
    }
    int x = lsel.left() - kGap - t;
    if (x < screen.left())
        x = lsel.right() + kGap;                       // no room left: try the right
    if (x + t > screen.right())
        x = qMax(screen.left(), lsel.left());          // no room either side: overlap
    return QRect(x, lsel.top(), t, len);
}

int SnipOverlay::scrollStripBelowHeight() const
{
    if (!m_scrolling || !m_scrollHorizontal)
        return 0;
    const QRect strip = scrollStripGeometry();
    const QRect lsel = m_layout.captureRectToGlobalLogical(m_scrollRegion);
    // Only the below placement collides with the toolbars; above and inside do not.
    // The extra 8 is the gap the bars keep from the strip's own bottom edge.
    return strip.top() > lsel.bottom() ? strip.height() + 8 : 0;
}

void SnipOverlay::updateScrollStrip()
{
    if (!m_scrollStrip || !m_stitcher)
        return;
    m_scrollStrip->setGeometry(scrollStripGeometry());
    // Mid-drag the stitcher's offset is a frame behind — the move has not been captured
    // yet — so the marker follows the predicted position instead (applyScrollMove).
    m_scrollStrip->setCanvas(m_stitcher->canvas(),
                             m_moveDragging ? m_movePredictedLive : m_stitcher->liveOffset(),
                             scrollFrameLength());
    if (!m_scrollStrip->isVisible())
        m_scrollStrip->show();
}

void SnipOverlay::syncScrollBarState()
{
    if (!m_scrollBar || !m_stitcher)
        return;
    // The canvas is transposed for a horizontal session, so the readout un-transposes it:
    // what the user is told is the size of the image they will get.
    const QSize c = m_stitcher->canvas().size();
    m_scrollBar->setCapturedSize(m_scrollHorizontal ? c.height() : c.width(),
                                 m_scrollHorizontal ? c.width() : c.height());
    // A side is trimmable only while it holds rows the live region does not: with a single
    // frame, or parked against either end, there is nothing there to throw away.
    const int live = m_stitcher->liveOffset();
    m_scrollBar->setTrimEnabled(live > 0,
                                live + scrollFrameLength() < c.height());
}

void SnipOverlay::trimScrollCapture(scroll::ScrollStitcher::Side side)
{
    if (!m_scrolling || !m_stitcher)
        return;
    // Auto-scroll would append the far side straight back, and a cut is a deliberate act
    // on a capture that should hold still while it happens — so it ends here, as Move does.
    stopAutoScroll(QString());
    if (m_stitcher->trim(side) <= 0)
        return;                       // that side was already empty: nothing to repaint
    syncScrollBarState();
    updateScrollStrip();              // the strip is scaled off the canvas, which just shrank
    updateEditChrome();               // ...and the bar's width tracks the size text
}

void SnipOverlay::finishScrollCapture()
{
    if (!m_scrolling || !m_stitcher || m_stitcher->isEmpty())
        return;
    stopAutoScroll(QString());        // Done while auto-scrolling: the session ends here
    m_scrollMoving = m_moveDragging = false;   // and Move mode goes with it
    // A horizontal session stitched transposed frames, so this is where the result is
    // turned back the right way up — once, over the whole canvas, rather than per frame.
    m_scrollResult = m_scrollHorizontal ? rotateFromStitch(m_stitcher->canvas())
                                        : m_stitcher->canvas();
    m_stitcher.reset();               // the rotated copy is the product now; drop the original
    m_scrolling = false;
    if (m_wheelWatch) m_wheelWatch->disarm();
    if (m_scrollIdle) m_scrollIdle->stop();
    if (m_scrollStrip) m_scrollStrip->hide();

    exitLiveRegion();                 // re-freeze; the region now shows the result
    for (SnipScreenView* v : m_views)
        v->setScrollPreview(m_scrollResult);
    if (m_scrollBar)
        m_scrollBar->hide();          // its job is done; row 1 owns the rest
    if (m_paintBar)
        m_paintBar->setOutputEnabled(true);   // Copy/Save/Pin act on the result
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::cancelScrollCapture()
{
    if (!m_scrolling)
        return;
    m_autoScroll = false;
    m_scrollMoving = m_moveDragging = false;
    m_scrolling = false;
    if (m_wheelWatch) m_wheelWatch->disarm();
    if (m_scrollIdle) m_scrollIdle->stop();
    m_stitcher.reset();               // everything stitched so far is thrown away
    if (m_scrollStrip) m_scrollStrip->hide();
    if (m_scrollBar)   m_scrollBar->hide();

    // Cancel drops back to ordinary annotation rather than closing the overlay. The
    // freeze taken when the snip started is stale — the window underneath scrolled out
    // from under it — so it is not what we go back to: the desktop is grabbed AGAIN here,
    // and the region keeps its rect and its committed state. What the user is left with is
    // exactly the snip they would have got by dragging that same region just now.
    exitLiveRegion();
    refreshFreeze();

    if (m_paintBar) {
        m_paintBar->setScrollChecked(false);       // the mode indicator goes out with it
        m_paintBar->setToolsEnabledForSession(true);
        m_paintBar->setOutputEnabled(true);
        // Availability this session never took away is re-applied, not assumed: a build
        // without a recorder must not gain a Record button by way of Cancel.
        m_paintBar->setRecordEnabled(m_recorder != nullptr,
            I18n::instance().t(QStringLiteral("record.unavailable_body")));
    }
    updateEditChrome();               // drops row 2 and re-syncs undo/redo off the doc
    updateAllViews();
}

QVector<QWidget*> SnipOverlay::hideFloatingBars()
{
    QVector<QWidget*> hidden;
    if (!m_barsFloating)
        return hidden;
    const QVector<QWidget*> candidates = {
        m_paintBar, m_paintBar ? m_paintBar->subBar() : nullptr,
        m_ocrBar, m_recordBar, m_scrollBar};
    for (QWidget* w : candidates) {
        if (w && w->isVisible()) {
            w->hide();
            hidden.append(w);          // only these come back: a hidden bar stays hidden
        }
    }
    return hidden;
}

void SnipOverlay::floatBars(bool floating)
{
    if (m_barsFloating == floating)
        return;
    m_barsFloating = floating;

    // While recording, the per-monitor views go click-through so the recorded app stays
    // usable — and a child widget of a click-through window is click-through too. So the
    // bars are promoted to their own top-level windows, which keeps them clickable and
    // lets them stay out of the capture on their own account.
    QWidget* bars[] = {m_paintBar, m_paintBar ? m_paintBar->subBar() : nullptr,
                       m_ocrBar, m_recordBar, m_scrollBar};
    for (QWidget* w : bars) {
        if (!w)
            continue;
        if (floating) {
            w->setParent(nullptr, Qt::Tool | Qt::FramelessWindowHint
                                  | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
            w->setAttribute(Qt::WA_ShowWithoutActivating, true);
#ifdef Q_OS_WIN
#  ifndef WDA_EXCLUDEFROMCAPTURE
#    define WDA_EXCLUDEFROMCAPTURE 0x00000011
#  endif
            w->createWinId();
            SetWindowDisplayAffinity(reinterpret_cast<HWND>(w->winId()), WDA_EXCLUDEFROMCAPTURE);
#endif
        }
    }
}

// ---- OCR mode ------------------------------------------------------------------

void SnipOverlay::toggleOcr(bool on)
{
    if (on) {
        if (!m_ocr || !m_sel->isValid()) {           // no backend / nothing to read
            if (m_paintBar) m_paintBar->setOcrChecked(false);
            m_ocrActive = false;
            if (!m_ocr && m_notifySink)
                m_notifySink(I18n::instance().t(QStringLiteral("ocr.title")),
                             I18n::instance().t(QStringLiteral("ocr.unavailable")));
            return;
        }
        m_ocrActive = true;
        if (!m_ocrBar) {
            m_ocrBar = new OcrBar();
            connect(m_ocrBar, &OcrBar::selectAllRequested, this, [this] {
                m_ocrLayer.selectAll();
                updateAllViews();
            });
            connect(m_ocrBar, &OcrBar::copyRequested, this, [this] { copyOcrText(); });
            connect(m_ocrBar, &OcrBar::keepLineBreaksChanged, this, [this](bool k) {
                m_ocrLayer.setKeepLineBreaks(k);
            });
            connect(m_ocrBar, &OcrBar::languageChanged, this, [this](const QString& c) {
                m_ocrLang = c;
                startOcrRecognition();
            });
        }
        m_ocrBar->setAccent(m_accentColor);
        if (m_ocrLang.isEmpty())
            m_ocrLang = defaultOcrLang();
        m_ocrBar->setLanguages(m_ocr->availableLanguages(), m_ocrLang);
        m_ocrLayer.setKeepLineBreaks(m_ocrBar->keepLineBreaks());
        startOcrRecognition();
    } else {
        m_ocrActive = false;
        m_ocrLayer.clear();
        if (m_ocrBar) m_ocrBar->hide();
    }
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::startOcrRecognition()
{
    if (!m_ocr || !m_sel->isValid())
        return;
    m_ocrSelCapture = m_sel->rect();                 // capture px; boxes come back relative to it
    const QImage img = compositeBase(m_ocrSelCapture);
    platform::IOcr* ocr = m_ocr;
    const QString lang = m_ocrLang;
    // Recognition can take a while — run it off the UI thread and post the result back,
    // guarded so a closed overlay (self-deleted) is not touched.
    QPointer<SnipOverlay> self(this);
    QThread* th = QThread::create([ocr, img, lang, self] {
        const platform::OcrResult r = ocr->recognize(img, lang);
        QMetaObject::invokeMethod(qApp, [self, r] { if (self) self->onOcrReady(r); },
                                  Qt::QueuedConnection);
    });
    connect(th, &QThread::finished, th, &QObject::deleteLater);
    th->start();
}

void SnipOverlay::onOcrReady(const platform::OcrResult& r)
{
    if (!m_ocrActive)                                // user left OCR mode while recognizing
        return;
    m_ocrLayer.setResult(r);
    if (m_ocrBar) m_ocrLayer.setKeepLineBreaks(m_ocrBar->keepLineBreaks());
    updateAllViews();
}

QPoint SnipOverlay::captureToOcrImage(const QPoint& gCapture) const
{
    // Mouse points already arrive in global capture px (SnipScreenView::toCapture),
    // the same space as m_ocrSelCapture — so image px is just the offset from its origin.
    return gCapture - m_ocrSelCapture.topLeft();
}

void SnipOverlay::handleOcrPress(const QPoint& g)
{
    if (m_ocrLayer.isEmpty())
        return;
    m_ocrLayer.beginSelect(captureToOcrImage(g));
    updateAllViews();
}

void SnipOverlay::handleOcrMove(const QPoint& g)
{
    if (!m_ocrLayer.selecting())
        return;
    m_ocrLayer.extendSelect(captureToOcrImage(g));
    updateAllViews();
}

void SnipOverlay::handleOcrRelease()
{
    m_ocrLayer.endSelect();
    updateAllViews();
}

void SnipOverlay::copyOcrText()
{
    if (!m_ocrLayer.hasSelection())                  // nothing picked → copy all
        m_ocrLayer.selectAll();
    const QString text = m_ocrLayer.selectedText();
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
    updateAllViews();
    if (m_notifySink)
        m_notifySink(I18n::instance().t(QStringLiteral("ocr.title")),
                     I18n::instance().t(QStringLiteral("ocr.copied")));
}

QString SnipOverlay::defaultOcrLang() const
{
    const QStringList have = m_ocr ? m_ocr->availableLanguages() : QStringList();
    if (m_appSettings) {                             // Preferences → OCR default (empty = auto)
        const QString pref = m_appSettings->value(QStringLiteral("ocr/language")).toString();
        if (!pref.isEmpty() && have.contains(pref))
            return pref;
    }
    return OcrBar::defaultLanguage(have);            // else follow the UI locale
}

void SnipOverlay::drawOcrLayer(QPainter& p, SnipScreenView* view) const
{
    if (!m_ocrActive || m_ocrLayer.isEmpty())
        return;
    const QPoint origin = m_ocrSelCapture.topLeft();
    auto mapRect = [view, origin](const QRect& imgBox) -> QRectF {
        return view->toLocalRectF(QRectF(imgBox.translated(origin)));
    };
    m_ocrLayer.paint(p, mapRect, m_accentColor);
}

annotation::Style SnipOverlay::captureStyle(const QPoint& anchor) const
{
    const qreal s = m_layout.scaleAt(anchor);
    annotation::Style st = m_style;
    st.penWidth       = qMax(1, int(std::lround(m_style.penWidth * s)));
    st.fontPx         = m_style.fontPx * s;
    st.redactStrength = qMax(1, int(std::lround(m_style.redactStrength * s)));
    return st;
}

void SnipOverlay::startDraft(const QPoint& g)
{
    hideAllToolbars();
    m_draft = annotation::create(m_tool, captureStyle(g));
    if (!m_draft) { m_drawing = false; return; }
    m_drawing = true;
    m_draftAnchor = g;
    m_draftPts = {g};
    m_draft->setGeometry(m_draftPts);
    updateAllViews();
}

void SnipOverlay::updateDraft(const QPoint& g, Qt::KeyboardModifiers mods)
{
    if (!m_drawing || !m_draft)
        return;
    const bool shift = mods & Qt::ShiftModifier;
    const bool ctrl  = mods & Qt::ControlModifier;
    const annotation::ToolId t = m_draft->tool();
    const bool isPoly = t == annotation::ToolId::Pencil || t == annotation::ToolId::Marker
                     || t == annotation::ToolId::LineStrip || t == annotation::ToolId::Eraser
                     || t == annotation::ToolId::MosaicBrush || t == annotation::ToolId::BlurBrush;
    if (isPoly) {
        // Pen + highlighter straighten a freehand stroke from where the drag began:
        // Shift follows the cursor at any angle, Ctrl locks to 45° steps (so a
        // near-horizontal drag lies flat, ideal for highlighting text). Ctrl wins if both
        // are held — the more specific request, same rule as the region resize.
        const bool straighten = (shift || ctrl)
                                && (t == annotation::ToolId::Pencil
                                    || t == annotation::ToolId::Marker);
        if (straighten)
            m_draftPts = {m_draftAnchor,
                          ctrl ? ShapeConstraints::snapLine(m_draftAnchor, g) : g};
        else
            m_draftPts.append(g);
    } else {
        // Ctrl squares the rect family, Shift snaps a line/arrow to 45°.
        m_draftPts = {m_draftAnchor,
                      ShapeConstraints::constrainedEnd(t, m_draftAnchor, g, mods)};
    }
    m_draft->setGeometry(m_draftPts);
    updateAllViews();
}

void SnipOverlay::commitDraft()
{
    m_drawing = false;
    if (m_draft) {
        const annotation::ToolId tool = m_draft->tool();
        const int id = m_doc->add(std::move(m_draft));   // ignored if degenerate/empty
        m_draft.reset();
        // A freshly drag-drawn selectable shape is auto-selected, so its edit chrome
        // (grips + spin/× handles) appears immediately without a separate click.
        if (id > 0 && isSelectableShape(tool))
            selectShape(id);
    }
    updateEditChrome();
    updateAllViews();
}

// ---- Quick-mark ------------------------------------------------------------
annotation::ToolId SnipOverlay::quickMarkToolFor(annotation::ToolId t)
{
    using annotation::ToolId;
    switch (t) {
        // Already two-corner: an element rect maps straight onto them.
        case ToolId::Rect:
        case ToolId::Ellipse:
        case ToolId::MarkerRect:
        case ToolId::Mosaic:
        case ToolId::Blur:
        case ToolId::EraserRect:   return t;
        // Freehand modes of the redaction tools. A quick-mark IS a rectangle, so these
        // mark as their rect twin rather than not working at all — picking Marker gives
        // you freehand mode by default (PaintBar::emitMarkerTool), and having the gesture
        // silently do nothing there is the last thing the user wants. The toolbar mode is
        // left alone: this one mark is a rect, the next stroke is still freehand.
        case ToolId::Marker:       return ToolId::MarkerRect;
        case ToolId::Eraser:       return ToolId::EraserRect;
        case ToolId::MosaicBrush:  return ToolId::Mosaic;
        case ToolId::BlurBrush:    return ToolId::Blur;
        // Pencil/line/text/label have no rectangular reading at all.
        default:                   return ToolId::None;
    }
}

void SnipOverlay::refreshQuickMark(bool force)
{
    // Alt is the gesture — unlike Ctrl it carries no meaning for the rect tools, so it
    // needs no disambiguating. (Alt's own aux-line toggle yields; see toggleAuxLines.)
    // Still suppressed mid-draw: a dashed element box over a rect being dragged is noise.
    if (!m_editing || !m_detect || !m_haveCursor || m_drawing || !quickMarkTool()
        || !(QGuiApplication::keyboardModifiers() & Qt::AltModifier)) {
        clearQuickMark();
        return;
    }
    // Same throttle rationale as refreshDetection(): a provider hit test is too costly
    // to run on every mouse-move. `force` bypasses it so a click can't commit a stale rect.
    if (!force && m_qmClock.isValid() && m_qmClock.elapsed() < 30)
        return;
    m_qmClock.restart();

    const QRect r = m_layout.globalLogicalRectToCapture(
        m_detect->elementAt(m_layout.captureToGlobalLogical(m_lastCursor)));
    // Deliberately NOT intersected with the selection: the mark keeps the true element
    // bounds, and the preview has to show the overflow that decision creates.
    const QRect next = (r.width() > 1 && r.height() > 1) ? r : QRect();
    if (next != m_qmRect) {
        m_qmRect = next;
        updateAllViews();
    }
}

void SnipOverlay::clearQuickMark()
{
    if (m_qmRect.isNull())
        return;
    m_qmRect = QRect();
    updateAllViews();
}

bool SnipOverlay::applyQuickMark()
{
    if (!m_editing || !quickMarkTool() || !m_detect || !m_haveCursor
        || !(QGuiApplication::keyboardModifiers() & Qt::AltModifier))
        return false;
    refreshQuickMark(true);            // the throttle may be holding a stale rect
    if (m_qmRect.isNull())
        return false;

    auto a = annotation::create(quickMarkToolFor(m_tool), captureStyle(m_qmRect.topLeft()));
    if (!a)
        return false;
    // Two opposite corners, exactly as a drag from one to the other would produce.
    a->setGeometry({m_qmRect.topLeft(), m_qmRect.bottomRight()});
    m_doc->add(std::move(a));          // a single add = a single Ctrl+Z
    updateEditChrome();
    updateAllViews();
    return true;
}

// ---- Line tool: build → control → finalize ---------------------------------
void SnipOverlay::setTool(annotation::ToolId t)
{
    if (t != m_tool) {
        resetLineState();        // a half-built line is abandoned when the tool changes
        if (m_textEditor)        // switching tools commits the open text box
            commitTextEditor();
        if (labelEditing())      // ...and banks an open badge renumber
            commitLabelEdit();
        flushLabelResize();      // ...and any in-flight wheel resize
        flushShapeStyle();       // ...and any in-flight shape width/strength scroll
        m_textHoverOutline.clear();   // drop any text hover highlight from the old tool
        clearQuickMark();             // ...and any quick-mark preview (the new tool may not support it)
        deselectShape();              // ...and any on-canvas shape selection
        // (Re-)selecting the number-label tool restarts the count at 1.
        if (t == annotation::ToolId::NumberLabel)
            m_nextLabel = 1;
    }
    m_tool = t;
}

// The three line-tool style setters. Each updates the tool's style for the NEXT line,
// live-updates a line still under construction, and — like colour/width/fill/dash —
// restyles the currently selected shape as one undo step.
void SnipOverlay::setStartCap(annotation::CapStyle c)
{
    m_style.startCap = c;
    if (m_draft)
        m_draft->style.startCap = c;
    if (annotation::Annotation* a = selectedShape()) {
        annotation::Style s = a->style;
        s.startCap = c;
        m_doc->restyle(a->id, s);
    }
    updateAllViews();
}

void SnipOverlay::setEndCap(annotation::CapStyle c)
{
    m_style.endCap = c;
    if (m_draft)
        m_draft->style.endCap = c;
    if (annotation::Annotation* a = selectedShape()) {
        annotation::Style s = a->style;
        s.endCap = c;
        m_doc->restyle(a->id, s);
    }
    updateAllViews();
}

void SnipOverlay::setCurved(bool c)
{
    m_style.curved = c;
    if (m_draft)
        m_draft->style.curved = c;
    if (annotation::Annotation* a = selectedShape()) {
        annotation::Style s = a->style;
        s.curved = c;
        m_doc->restyle(a->id, s);
    }
    updateAllViews();
}

QVector<QPoint> SnipOverlay::lineControlPoints() const
{
    return m_linePhase == LinePhase::Controlling ? m_lineVerts : QVector<QPoint>();
}

QPoint SnipOverlay::lineSnap(const QPoint& from, const QPoint& g, Qt::KeyboardModifiers mods)
{
    return ShapeConstraints::constrainedEnd(annotation::ToolId::Line, from, g, mods);
}

void SnipOverlay::rebuildLineDraft(const QPoint* floating)
{
    if (!m_draft)
        return;
    QVector<QPoint> pts = m_lineVerts;
    if (floating)
        pts.append(*floating);
    if (pts.size() == 1)
        pts.append(pts.first());     // keep a valid (if zero-length) 2-point geometry
    m_draft->style = captureStyle(m_lineVerts.isEmpty() ? QPoint() : m_lineVerts.first());
    m_draft->setGeometry(pts);
}

int SnipOverlay::lineControlHit(const QPoint& g) const
{
    int idx = -1;
    double best = 10.0 * m_layout.scaleAt(g);   // capture-px grab radius
    for (int i = 0; i < m_lineVerts.size(); ++i) {
        const double d = std::hypot(double(m_lineVerts[i].x() - g.x()),
                                    double(m_lineVerts[i].y() - g.y()));
        if (d < best) { best = d; idx = i; }
    }
    return idx;
}

void SnipOverlay::handleLinePress(const QPoint& g, Qt::KeyboardModifiers mods)
{
    switch (m_linePhase) {
        case LinePhase::Idle:                       // first press: start building
            m_linePhase = LinePhase::Building;
            m_lineVerts = { g };
            m_linePressPos = g;
            m_lineDragArmed = true;                  // a drag from here = single segment
            m_lineDragging = false;
            m_draft = annotation::create(annotation::ToolId::Line, captureStyle(g));
            rebuildLineDraft(&g);
            break;
        case LinePhase::Building: {                  // each click adds a vertex
            // Snap the placed vertex, not just the preview, or the committed line
            // would differ from what the preview showed.
            const QPoint v = lineSnap(m_lineVerts.last(), g, mods);
            m_lineVerts.append(v);
            rebuildLineDraft(&v);
            break;
        }
        case LinePhase::Controlling:                 // grab a control point to drag
            m_dragVert = lineControlHit(g);
            break;
    }
    updateAllViews();
}

void SnipOverlay::handleLineMove(const QPoint& g, Qt::KeyboardModifiers mods)
{
    if (m_linePhase == LinePhase::Building) {
        if (m_lineDragArmed && !m_lineDragging) {
            // Measured on the raw cursor: this asks "did the mouse move?", not "where
            // does the segment end?".
            const double moved = std::hypot(double(g.x() - m_linePressPos.x()),
                                            double(g.y() - m_linePressPos.y()));
            if (moved > 4.0)                         // press moved enough → it's a drag
                m_lineDragging = true;
        }
        const QPoint end = lineSnap(m_lineVerts.last(), g, mods);
        rebuildLineDraft(&end);                      // cursor previews the segment / next vertex
    } else if (m_linePhase == LinePhase::Controlling && m_dragVert >= 0) {
        // Snap a control point only on a single-segment line, where the opposite end
        // is an unambiguous anchor. A middle vertex has two neighbours — no one angle.
        m_lineVerts[m_dragVert] = m_lineVerts.size() == 2
                                      ? lineSnap(m_lineVerts.at(1 - m_dragVert), g, mods)
                                      : g;
        rebuildLineDraft(nullptr);
    }
    updateAllViews();
}

void SnipOverlay::handleLineRelease(const QPoint& g, Qt::KeyboardModifiers mods)
{
    if (m_linePhase == LinePhase::Building) {
        if (m_lineDragArmed && m_lineDragging) {
            // A drag (not a click) means a single-segment line: skip building and go
            // straight to the control phase.
            m_lineVerts = { m_linePressPos, lineSnap(m_linePressPos, g, mods) };
            m_dragVert = -1;
            m_linePhase = LinePhase::Controlling;
            rebuildLineDraft(nullptr);
            updateEditChrome();
        }
        m_lineDragArmed = false;                      // a plain click → multi-segment mode
        m_lineDragging = false;
    } else if (m_linePhase == LinePhase::Controlling) {
        m_dragVert = -1;                             // drop the dragged control point
    }
    updateAllViews();
}

bool SnipOverlay::handleLineSecondary()
{
    if (!(m_editing && isLineTool()) || m_linePhase == LinePhase::Idle)
        return false;

    if (m_linePhase == LinePhase::Building) {
        if (m_lineVerts.size() >= 2) {               // enough for a line → edit handles
            m_linePhase = LinePhase::Controlling;
            m_dragVert = -1;
            rebuildLineDraft(nullptr);               // drop the floating preview point
        } else {
            resetLineState();                        // a single click is not a line
        }
    } else {                                         // Controlling → finalize
        if (m_draft) {
            rebuildLineDraft(nullptr);
            m_doc->add(std::move(m_draft));
            m_draft.reset();
        }
        m_linePhase = LinePhase::Idle;
        m_lineVerts.clear();
        m_dragVert = -1;
    }
    updateEditChrome();
    updateAllViews();
    return true;
}

void SnipOverlay::resetLineState()
{
    const bool had = m_linePhase != LinePhase::Idle || m_draft;
    m_linePhase = LinePhase::Idle;
    m_lineVerts.clear();
    m_dragVert = -1;
    m_lineDragArmed = false;
    m_lineDragging = false;
    m_draft.reset();
    if (had)
        updateAllViews();
}

void SnipOverlay::placeText(const QPoint& g)
{
    if (m_textEditor)            // an existing box is open: bake it before starting a new one
        commitTextEditor();

    // Host the box on the view whose monitor contains the click (capture-px point).
    const int hi = m_layout.indexForCapture(g);
    SnipScreenView* host = (hi >= 0 && hi < m_views.size()) ? m_views[hi] : nullptr;
    if (!host)
        return;
    m_textHost = host;

    m_textEditor = new TextBoxEditor(host);
    m_textEditor->setTextColor(m_style.color);
    m_textEditor->setFontFamily(m_style.fontFamily);
    m_textEditor->setFontPixelSize(m_style.fontPx);   // logical: the editor lives in local coords
    m_textEditor->setBold(m_style.bold);
    m_textEditor->setItalic(m_style.italic);
    m_textEditor->setHollow(m_style.hollowText);

    // Default box: a small wrap box with its top-left at the click (host-local coords).
    const QPoint local = host->toLocalF(g).toPoint();
    constexpr int kDefW = 64, kDefH = 44;    // compact; grows right as you type, down on Enter
    m_textEditor->setContentBox(QRect(local, QSize(kDefW, kDefH)));

    wireTextEditor();

    m_textEditor->show();
    m_textEditor->raise();
    m_textEditor->focusEditor();
    updateAllViews();
}

void SnipOverlay::wireTextEditor()
{
    connect(m_textEditor, &TextBoxEditor::committed, this, [this] { commitTextEditor(); });
    connect(m_textEditor, &TextBoxEditor::canceled,  this, [this] { cancelTextEditor(); });
    connect(m_textEditor, &TextBoxEditor::fontSizeChanged, this, [this](qreal px) {
        m_style.fontPx = px;
        if (m_paintBar) m_paintBar->setFontSizeValue(px);   // reflect the scaled size (no echo)
    });
}

annotation::Annotation* SnipOverlay::textAt(const QPoint& g) const
{
    if (!m_doc)
        return nullptr;
    const auto& items = m_doc->items();
    for (auto it = items.rbegin(); it != items.rend(); ++it) {   // topmost first
        annotation::Annotation* a = it->get();
        if (a->tool() == annotation::ToolId::Text && a->hitTest(g))
            return a;
    }
    return nullptr;
}

void SnipOverlay::reopenText(annotation::Annotation* a)
{
    if (!a || a->tool() != annotation::ToolId::Text)
        return;
    if (m_textEditor)              // bake any currently-open box before re-opening another
        commitTextEditor();

    // Snapshot the shape's state, then pull it out of the document — the editor now owns
    // it; committing the edit re-adds it (cancel/× discards, matching new-box semantics).
    const annotation::Style st = a->style;
    const QString textVal = a->text();
    const QSize boxSz = a->box();
    const qreal rot = a->rotation();
    const QPoint posG = a->geometry().isEmpty() ? a->bounds().topLeft() : a->geometry().first();
    const QRect innerG(posG, boxSz.isEmpty() ? a->bounds().size() : boxSz);   // inner text rect (global)

    // Host the editor on the monitor containing the text (capture-px inner rect).
    const int hi = m_layout.indexForCapture(innerG.center());
    SnipScreenView* host = (hi >= 0 && hi < m_views.size()) ? m_views[hi] : nullptr;
    if (!host)
        return;
    const qreal hs = host->scaleX();

    m_doc->remove(a->id);          // undoable; baking the edit adds a fresh annotation
    m_textHoverOutline.clear();
    m_textHost = host;

    m_textEditor = new TextBoxEditor(host);
    m_textEditor->setTextColor(st.color);
    m_textEditor->setFontFamily(st.fontFamily);
    m_textEditor->setFontPixelSize(st.fontPx / hs);   // capture px -> logical for the local editor
    m_textEditor->setBold(st.bold);
    m_textEditor->setItalic(st.italic);
    m_textEditor->setHollow(st.hollowText);
    m_textEditor->setText(textVal);

    m_textEditor->setTextRect(host->toLocalRect(innerG));   // inner rect, host-local
    m_textEditor->setRotationDeg(rot);

    // Adopt the shape's text style so live toolbar edits + the re-commit stay consistent.
    m_style.color = st.color;
    m_style.fontFamily = st.fontFamily;
    m_style.fontPx = st.fontPx / hs;                 // capture px -> logical for the toolbar
    m_style.bold = st.bold;
    m_style.italic = st.italic;
    m_style.hollowText = st.hollowText;
    if (m_paintBar) m_paintBar->setFontSizeValue(st.fontPx / hs);

    wireTextEditor();

    m_textEditor->show();
    m_textEditor->raise();
    m_textEditor->focusEditor();
    updateAllViews();
}

// ---- Number label ----------------------------------------------------------
void SnipOverlay::setLabelCaretOn(bool on)
{
    if (m_labelCaretOn == on)
        return;
    m_labelCaretOn = on;
    updateAllViews();
}

void SnipOverlay::restartLabelCaret()
{
    // Typing shows a solid caret and restarts the blink, so it never blinks out mid-key.
    if (!m_labelCaret)
        return;
    m_labelCaretOn = true;
    m_labelCaret->start();
}

void SnipOverlay::beginLabelResize(int id)
{
    if (m_labelResizeId == id)
        return;                            // same badge, same burst — keep the first size
    flushLabelResize();                    // moved to a different badge: bank the old one
    const annotation::Annotation* a = nullptr;
    for (const auto& item : m_doc->items())
        if (item->id == id) { a = item.get(); break; }
    if (!a)
        return;
    m_labelResizeId = id;
    m_labelResizeBefore = a->style.fontPx;
    if (!m_labelResizeFlush) {
        m_labelResizeFlush = new QTimer(this);
        m_labelResizeFlush->setSingleShot(true);
        m_labelResizeFlush->setInterval(500);   // "the wheel stopped" — one gesture
        connect(m_labelResizeFlush, &QTimer::timeout, this, [this] { flushLabelResize(); });
    }
    m_labelResizeFlush->start();
}

void SnipOverlay::flushLabelResize()
{
    if (m_labelResizeId <= 0)
        return;
    const int id = m_labelResizeId;
    const qreal before = m_labelResizeBefore;
    m_labelResizeId = 0;
    if (m_labelResizeFlush)
        m_labelResizeFlush->stop();
    m_doc->resizeApplied(id, before);      // no-op if the size ended up unchanged
    updateEditChrome();
}

void SnipOverlay::setLabelStyle(annotation::LabelStyle s)
{
    m_style.label = s;
    if (m_draft)
        m_draft->style.label = s;
    if (annotation::Annotation* a = const_cast<annotation::Annotation*>(editedLabel()))
        a->style.label = s;            // live-update the badge being renumbered
    updateAllViews();
}

void SnipOverlay::setNumberStyle(annotation::NumberStyle s)
{
    if (m_style.numberStyle == s)
        return;
    m_style.numberStyle = s;
    if (m_draft)
        m_draft->style.numberStyle = s;
    // The badges are one sequence, so the style applies to the whole set — including the
    // ones already stamped. Restyle them together so a single Ctrl+Z takes them all back.
    if (m_doc) {
        QVector<QPair<int, annotation::Style>> pending;   // collected first: restyle() mutates
        for (const auto& item : m_doc->items()) {
            if (item->tool() != annotation::ToolId::NumberLabel || item->style.numberStyle == s)
                continue;
            annotation::Style st = item->style;
            st.numberStyle = s;
            pending.push_back({item->id, st});
        }
        if (!pending.isEmpty()) {         // guarded: an empty macro would be a no-op undo step
            m_doc->beginMacro();
            for (const auto& p : pending)
                m_doc->restyle(p.first, p.second);
            m_doc->endMacro();
        }
    }
    updateAllViews();
}

const annotation::Annotation* SnipOverlay::editedLabel() const
{
    if (m_labelEditId <= 0 || !m_doc)
        return nullptr;
    for (const auto& item : m_doc->items())
        if (item->id == m_labelEditId)
            return item.get();
    return nullptr;
}

QVector<QRect> SnipOverlay::labelStepButtons() const
{
    const annotation::Annotation* a = editedLabel();
    if (!a)
        return {};
    // Fixed on-screen size: scale the button box by the monitor's factor so it doesn't
    // grow with the badge (which lives in capture px).
    const QRect b = a->bounds();
    const qreal s = m_layout.scaleAt(b.center());
    const int sz  = qMax(12, qRound(18 * s));
    const int gap = qMax(2, qRound(4 * s));
    const int x = b.right() + gap;
    const int cy = b.center().y();
    return { QRect(x, cy - sz - 1, sz, sz),      // + above the centre line
             QRect(x, cy + 1,      sz, sz) };    // − below it
}

QRect SnipOverlay::labelDeleteButton() const
{
    const annotation::Annotation* a = editedLabel();
    if (!a)
        return {};
    const QRect b = a->bounds();
    const qreal s = m_layout.scaleAt(b.center());
    const int sz  = qMax(12, qRound(18 * s));
    const int gap = qMax(2, qRound(4 * s));
    return QRect(b.center().x() - sz / 2, b.top() - gap - sz, sz, sz);
}

void SnipOverlay::deleteLabel()
{
    const annotation::Annotation* a = editedLabel();
    if (!a)
        return;
    const int id = a->id;
    bool ok = false;
    const int n = a->text().toInt(&ok);

    // What this edit session has changed so far (live, not yet on the undo stack) — banked
    // inside the macro so one Ctrl+Z restores the numbering as well as the badge.
    QHash<int, QString> cur;
    for (const auto& item : m_doc->items())
        if (item->tool() == annotation::ToolId::NumberLabel && m_labelBefore.contains(item->id))
            cur.insert(item->id, item->text());

    // Closing the gap: every remaining badge numbered above the deleted one drops by one,
    // so 1,2,3,4 minus the 2 becomes 1,2,3 rather than 1,3,4.
    QHash<int, QString> gapBefore, gapAfter;
    if (ok) {
        for (const auto& item : m_doc->items()) {
            if (item->id == id || item->tool() != annotation::ToolId::NumberLabel)
                continue;
            bool k = false;
            const int v = item->text().toInt(&k);
            if (!k)
                continue;                       // a hand-typed non-number: leave it alone
            gapBefore.insert(item->id, item->text());
            gapAfter.insert(item->id, QString::number(v > n ? v - 1 : v));
        }
    }

    m_doc->beginMacro();
    m_doc->setTexts(m_labelBefore, cur);        // this session's renumbering (no-op if none)
    m_doc->remove(id);
    m_doc->setTexts(gapBefore, gapAfter);       // close the gap (no-op if nothing above)
    m_doc->endMacro();

    // One badge fewer → the next stamp takes the number just freed at the top.
    m_nextLabel = qMax(1, m_nextLabel - 1);

    // Leave edit mode without committing again (the macro already recorded everything).
    m_labelEditId = 0;
    m_labelBefore.clear();
    m_textHoverOutline.clear();
    if (m_labelCaret)
        m_labelCaret->stop();
    updateEditChrome();
    updateAllViews();
}

bool SnipOverlay::labelCanStep(int delta) const
{
    const annotation::Annotation* a = editedLabel();
    if (!a || !m_doc)
        return false;
    bool ok = false;
    const int n = a->text().toInt(&ok);
    if (!ok)
        return false;
    // Numbering stays inside the range the badges already span, so stepping can only ever
    // reorder existing numbers — never open a gap or run away past the last badge.
    int lo = n, hi = n;
    for (const auto& item : m_doc->items()) {
        if (item->tool() != annotation::ToolId::NumberLabel)
            continue;
        bool k = false;
        const int v = item->text().toInt(&k);
        if (!k) continue;
        lo = qMin(lo, v);
        hi = qMax(hi, v);
    }
    return delta > 0 ? n < hi : n > lo;
}

void SnipOverlay::stepLabel(int delta)
{
    if (!labelCanStep(delta))
        return;
    annotation::Annotation* a = const_cast<annotation::Annotation*>(editedLabel());
    if (!a)
        return;
    const int n = a->text().toInt();
    const int target = n + delta;
    // Hand our number to whichever badge holds the target. For a ±1 step that swap is
    // exactly "move this badge one place in the order" — every other badge keeps its
    // relative position (1,2,3,4 → renumber the 2nd up → 1,3,2,4).
    for (const auto& item : m_doc->items()) {
        if (item.get() == a || item->tool() != annotation::ToolId::NumberLabel)
            continue;
        bool k = false;
        if (item->text().toInt(&k) == target && k) {
            item->setText(QString::number(n));
            break;
        }
    }
    a->setText(QString::number(target));
    m_labelEditFresh = false;          // stepping is an explicit edit; typing now appends
    restartLabelCaret();
    updateAllViews();
}

int SnipOverlay::labelCount() const
{
    int n = 0;
    if (m_doc)
        for (const auto& item : m_doc->items())
            if (item->tool() == annotation::ToolId::NumberLabel)
                ++n;
    return n;
}

void SnipOverlay::syncNextLabel(int labelsBefore)
{
    // Undoing a stamp takes the badge back, so the counter has to step back with it —
    // otherwise 1,2,3 followed by Ctrl+Z would stamp 4 next. The undo stack is opaque (any
    // command can be on top), so measure what actually happened: how many badges came or
    // went IS how far the counter moves. A renumber/move/restyle changes no count and so
    // leaves it alone, undoing a delete re-raises it (mirroring deleteLabel's decrement),
    // and the floor keeps a freshly re-picked tool's restart-at-1 intact.
    m_nextLabel = qMax(1, m_nextLabel + labelCount() - labelsBefore);
}

void SnipOverlay::syncNumberStyleFromDoc()
{
    // The numbering restyle is retroactive, so undoing it puts the badges back on their old
    // glyphs — and the toolbar + cursor preview have to follow, or the next stamp would come
    // out in a style nothing else on the canvas uses.
    if (!m_doc)
        return;
    bool any = false;
    annotation::NumberStyle s = m_style.numberStyle;
    for (const auto& item : m_doc->items()) {
        if (item->tool() != annotation::ToolId::NumberLabel)
            continue;
        if (!any) { s = item->style.numberStyle; any = true; }
        else if (item->style.numberStyle != s) return;   // mixed (restored pin): leave it be
    }
    if (!any || s == m_style.numberStyle)
        return;
    m_style.numberStyle = s;
    if (m_draft)
        m_draft->style.numberStyle = s;
    if (m_paintBar)
        m_paintBar->setNumberStyleValue(s);
}

annotation::Annotation* SnipOverlay::labelAt(const QPoint& g) const
{
    if (!m_doc)
        return nullptr;
    const auto& items = m_doc->items();
    for (auto it = items.rbegin(); it != items.rend(); ++it) {   // topmost first
        annotation::Annotation* a = it->get();
        if (a->tool() == annotation::ToolId::NumberLabel && a->hitTest(g))
            return a;
    }
    return nullptr;
}

void SnipOverlay::handleLabelPress(const QPoint& g)
{
    // The +/− buttons sit outside the badge but belong to the edit — check them first so
    // they neither stamp a new badge nor end the edit.
    const QVector<QRect> steps = labelStepButtons();
    if (steps.size() == 2) {
        if (steps[0].contains(g)) { stepLabel(+1); return; }
        if (steps[1].contains(g)) { stepLabel(-1); return; }
    }
    if (labelDeleteButton().contains(g)) { deleteLabel(); return; }
    // Clicking an existing badge arms it: release without movement renumbers it,
    // movement drags it. Clicking anywhere else commits any open edit and stamps.
    if (annotation::Annotation* a = labelAt(g)) {
        if (labelEditing() && a->id != m_labelEditId)   // another badge → bank this edit
            commitLabelEdit();
        m_labelPressId = a->id;
        m_labelPressPos = g;
        m_labelMoved = false;
        return;
    }
    if (labelEditing())
        commitLabelEdit();
    else
        placeLabel(g);
}

void SnipOverlay::handleLabelRelease()
{
    const int id = m_labelPressId;
    const bool moved = m_labelMoved;
    const QPoint total = m_labelDragTotal;
    m_labelPressId = 0;
    m_labelMoved = false;
    m_labelDragTotal = QPoint();
    if (moved) {
        // The drag was applied live; record the whole gesture as ONE undo step.
        m_doc->moveApplied(id, total);
        updateEditChrome();
        updateAllViews();
        return;
    }
    if (!m_doc)
        return;
    for (const auto& item : m_doc->items())
        if (item->id == id) { beginLabelEdit(item.get()); break; }
}

void SnipOverlay::placeLabel(const QPoint& g)
{
    if (!m_doc)
        return;
    std::unique_ptr<annotation::Annotation> a =
        annotation::create(annotation::ToolId::NumberLabel, captureStyle(g));
    if (!a)
        return;
    a->setGeometry({g});                 // the click point is the badge CENTRE
    a->setText(QString::number(m_nextLabel));
    if (m_doc->add(std::move(a)) > 0)
        ++m_nextLabel;                   // only advance if it was actually recorded
    m_textHoverOutline.clear();
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::beginLabelEdit(annotation::Annotation* a)
{
    if (!a || a->tool() != annotation::ToolId::NumberLabel)
        return;
    // The badge STAYS in the document and is mutated live for display. Snapshot every
    // badge's number now: a step can also change the badge it swaps with, and commit
    // pushes the whole before→after as ONE undo step.
    m_labelEditId = a->id;
    m_labelEditOrig = a->text();
    m_labelBefore.clear();
    for (const auto& item : m_doc->items())
        if (item->tool() == annotation::ToolId::NumberLabel)
            m_labelBefore.insert(item->id, item->text());
    m_labelEditFresh = true;             // the first digit replaces the whole number
    m_textHoverOutline = a->outline();   // mark which badge is being edited
    if (!m_labelCaret) {
        m_labelCaret = new QTimer(this);
        // Half the OS flash time = one on/off phase, so the caret matches system carets.
        m_labelCaret->setInterval(qMax(200, QGuiApplication::styleHints()->cursorFlashTime() / 2));
        connect(m_labelCaret, &QTimer::timeout, this, [this] { setLabelCaretOn(!m_labelCaretOn); });
    }
    restartLabelCaret();
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::commitLabelEdit()
{
    if (!labelEditing())
        return;
    annotation::Annotation* a = const_cast<annotation::Annotation*>(editedLabel());
    // Deleting every digit is not a way to erase a badge (that's the eraser's job) — an
    // empty edit reverts to the number it had.
    if (a && a->text().isEmpty())
        a->setText(m_labelEditOrig);

    // One undo step for the whole edit, covering the badge AND anything it swapped with.
    // The values are already applied (live display), so the command's redo is a no-op.
    QHash<int, QString> after;
    for (const auto& item : m_doc->items())
        if (item->tool() == annotation::ToolId::NumberLabel && m_labelBefore.contains(item->id))
            after.insert(item->id, item->text());
    m_doc->setTexts(m_labelBefore, after);

    m_labelEditId = 0;
    m_labelBefore.clear();
    m_textHoverOutline.clear();
    if (m_labelCaret)
        m_labelCaret->stop();
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::cancelLabelEdit()
{
    if (!labelEditing())
        return;
    // Esc reverts every badge the edit touched, and records nothing on the undo stack.
    for (const auto& item : m_doc->items())
        if (m_labelBefore.contains(item->id))
            item->setText(m_labelBefore.value(item->id));
    m_labelEditId = 0;
    m_labelBefore.clear();
    m_textHoverOutline.clear();
    if (m_labelCaret)
        m_labelCaret->stop();
    updateEditChrome();
    updateAllViews();
}

bool SnipOverlay::handleLabelEditKey(int key)
{
    annotation::Annotation* a = const_cast<annotation::Annotation*>(editedLabel());
    if (!a)
        return false;
    if (key == Qt::Key_Escape)                    { cancelLabelEdit(); return true; }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) { commitLabelEdit(); return true; }
    if (key == Qt::Key_Up)                        { stepLabel(+1); return true; }
    if (key == Qt::Key_Down)                      { stepLabel(-1); return true; }
    if (key == Qt::Key_Delete)                    { deleteLabel(); return true; }
    if (key == Qt::Key_Backspace) {
        QString t = a->text();
        if (m_labelEditFresh) t.clear();          // whole number was "selected"
        else                  t.chop(1);
        m_labelEditFresh = false;
        a->setText(t);
        restartLabelCaret();
        updateAllViews();
        return true;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        // Typed numbers are taken RAW — no swap. +/− (and ↑/↓) are the tidy reorder; typing
        // is the escape hatch, and may legitimately produce duplicates.
        QString t = m_labelEditFresh ? QString() : a->text();
        m_labelEditFresh = false;
        if (t.size() < 4)                         // 4 digits is well past legible
            t += QChar('0' + (key - Qt::Key_0));
        a->setText(t);
        restartLabelCaret();
        updateAllViews();
        return true;
    }
    return false;                                 // anything else falls through to snip keys
}

void SnipOverlay::commitTextEditor()
{
    if (!m_textEditor)
        return;
    TextBoxEditor* ed = m_textEditor;
    SnipScreenView* host = m_textHost;
    m_textEditor = nullptr;
    m_textHost = nullptr;

    const QString text = ed->text();
    if (!text.trimmed().isEmpty() && host && host->targetScreen()) {
        // Use the inner text rect (not the chrome box) so the baked text lands exactly
        // where the editor previewed it — no top-left drift. Convert the editor's local
        // (logical) rect + font size into capture px, the annotation's coordinate space.
        const QRect textCap = host->toCaptureRect(ed->textRect());
        annotation::Style st = m_style;
        st.fontPx = ed->fontPixelSize() * host->scaleX();
        std::unique_ptr<annotation::Annotation> a = annotation::create(annotation::ToolId::Text, st);
        if (a) {
            a->setGeometry({textCap.topLeft()});
            a->setBox(textCap.size());
            a->setRotation(ed->rotationDeg());
            a->setText(text);
            m_doc->add(std::move(a));     // ignored if empty
        }
    }
    ed->deleteLater();
    if (host)
        host->setFocus();
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::cancelTextEditor()
{
    if (!m_textEditor)
        return;
    SnipScreenView* host = m_textHost;
    m_textEditor->deleteLater();
    m_textEditor = nullptr;
    m_textHost = nullptr;
    if (host)
        host->setFocus();
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::setColor(const QColor& c)
{
    m_style.color = c;
    if (m_textEditor) m_textEditor->setTextColor(c);
    if (m_draft)                      // see setLineStyle: the line tool's draft is long-lived
        m_draft->style.color = c;
    if (annotation::Annotation* a = selectedShape()) {
        annotation::Style s = a->style;
        s.color = c;
        m_doc->restyle(a->id, s);
    }
    updateAllViews();
}

// The next three also restyle the currently-selected shape (as one undo step). Pen and
// redact sizes come from the PaintBar in LOGICAL px, so they are scaled to the shape's
// capture-px style the same way captureStyle() scales a freshly-drawn shape.
void SnipOverlay::setPenWidth(int w)
{
    m_style.penWidth = w;
    // Same long-lived-draft case as setLineStyle — but a draft's style is already in
    // CAPTURE px, so the logical width has to be scaled the way captureStyle() would,
    // off the shape's own position. Assigning `w` raw would shrink the in-progress line
    // on any monitor that isn't at 100%.
    if (m_draft && !m_draft->isEmpty()) {
        m_draft->style.penWidth =
            qMax(1, int(std::lround(w * m_layout.scaleAt(m_draft->bounds().center()))));
    }
    if (annotation::Annotation* a = selectedShape()) {
        annotation::Style s = a->style;
        s.penWidth = qMax(1, int(std::lround(w * m_layout.scaleAt(a->bounds().center()))));
        m_doc->restyle(a->id, s);
    }
    updateAllViews();
}

void SnipOverlay::setRedactStrength(int strength)
{
    m_style.redactStrength = strength;
    if (annotation::Annotation* a = selectedShape()) {
        annotation::Style s = a->style;
        s.redactStrength = qMax(1, int(std::lround(strength * m_layout.scaleAt(a->bounds().center()))));
        m_doc->restyle(a->id, s);
    }
}

void SnipOverlay::setFilled(bool f)
{
    m_style.filled = f;
    if (annotation::Annotation* a = selectedShape()) {
        annotation::Style s = a->style;
        s.filled = f;
        m_doc->restyle(a->id, s);
    }
}

void SnipOverlay::setLineStyle(annotation::LineStyle s)
{
    m_style.lineStyle = s;
    // The line tool is the only dashable tool with a LONG-LIVED draft — pencil and the
    // shapes commit on mouse release — so it is the only one that needs the in-progress
    // shape pushed to and repainted here. Without this the new style appears only on the
    // next mouse move (which re-runs rebuildLineDraft), and not at all once the line has
    // reached its control phase.
    if (m_draft)
        m_draft->style.lineStyle = s;
    if (annotation::Annotation* a = selectedShape()) {
        annotation::Style st = a->style;
        st.lineStyle = s;
        m_doc->restyle(a->id, st);
    }
    updateAllViews();
}

void SnipOverlay::setTextBold(bool b)
{
    m_style.bold = b;
    if (m_textEditor) m_textEditor->setBold(b);
}

void SnipOverlay::setTextItalic(bool b)
{
    m_style.italic = b;
    if (m_textEditor) m_textEditor->setItalic(b);
}

void SnipOverlay::setTextHollow(bool h)
{
    m_style.hollowText = h;
    if (m_textEditor) m_textEditor->setHollow(h);
}

void SnipOverlay::setTextFamily(const QString& family)
{
    m_style.fontFamily = family;
    if (m_textEditor) m_textEditor->setFontFamily(family);
}

void SnipOverlay::setTextFontPx(qreal px)
{
    m_style.fontPx = px;
    if (m_textEditor) m_textEditor->setFontPixelSize(px);
}

// Bank any in-flight wheel resize FIRST, so Ctrl+Z right after scrolling undoes that
// resize rather than the edit before it (and so the pending flush can't land later, on
// top of an unrelated command).
void SnipOverlay::undoAnnotation()
{
    flushLabelResize();
    flushShapeStyle();
    const int before = labelCount();
    m_doc->undo();
    syncNextLabel(before);
    syncNumberStyleFromDoc();
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::redoAnnotation()
{
    flushLabelResize();
    flushShapeStyle();
    const int before = labelCount();
    m_doc->redo();
    syncNextLabel(before);
    syncNumberStyleFromDoc();
    updateEditChrome();
    updateAllViews();
}

void SnipOverlay::clearAnnotations(){ flushLabelResize(); flushShapeStyle(); deselectShape(); m_doc->clearAll(); updateAllViews(); }

void SnipOverlay::copyColor()
{
    if (!m_hoverColor.isValid())
        return;
    ColorPicker::copyToClipboard(
        ColorPicker::format(m_hoverColor, colorFormat(), m_uppercaseHex, m_noHash));
    finish();
}

void SnipOverlay::onPin(bool thenQuit)
{
    if (!m_sel->isValid() || !m_pinSink)
        return;
    const QRect captureRect = m_sel->rect();
    QImage img = finalImage();
    if (img.isNull()) {
        if (thenQuit) finish();
        return;
    }
    recordHistory(img);             // history stores native pixels (before the dpr tag)

    // Tag the pin image with the capture DPR (the max dpr of the screens the selection
    // touches — exactly how compositeSelection chose the output resolution) so the pin
    // shows at its true logical size on ANY monitor, not the dpr of whatever screen the
    // window first lands on (mixed-DPI fix).
    qreal capDpr = 0.0;
    for (const ScreenShot& s : m_shots)
        if (!s.shot.isNull() && captureRect.intersects(s.captureGeo))
            capDpr = qMax(capDpr, s.dpr);
    img.setDevicePixelRatio(capDpr > 0.0 ? capDpr : 1.0);

    // Hand the flattened snip to the app along with the capture geometry mapped back to
    // global-logical, so the pin lands where it was captured. When quitting, tear the
    // overlay down first.
    const QRect logicalRect = m_layout.captureRectToGlobalLogical(captureRect);
    if (thenQuit) {
        auto sink = m_pinSink;
        finish();
        sink(img, logicalRect);
    } else {
        m_pinSink(img, logicalRect);
    }
}

void SnipOverlay::runSnipAction(const QString& action, bool thenQuit)
{
    if (action == QLatin1String("copy"))            copy(thenQuit);
    else if (action == QLatin1String("save"))       save(thenQuit);
    else if (action == QLatin1String("quicksave"))  quickSave(thenQuit);
    else if (action == QLatin1String("pin"))        onPin(thenQuit);
    // "nothing" (and any unknown value) is a no-op.
}

void SnipOverlay::onMiddleClick()
{
    if (m_sel->isValid())
        runSnipAction(m_middleClickAction, m_middleClickQuit);
}

void SnipOverlay::save(bool thenQuit)
{
    const QImage img = finalImage();
    if (img.isNull()) {
        if (thenQuit) finish();
        return;
    }
    for (SnipScreenView* v : m_views)        // clear the overlays for the dialog
        v->hide();
    const QVector<QWidget*> floatingBars = hideFloatingBars();   // they'd cover the dialog

    // Suggested name comes from the user's filename template (Output → Manual Save).
    // The template's extension drives the saved format; -1 quality = library default.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const int quality = outputQuality();
    const QString fileName = ManualSave::suggestedName(m_appSettings, filenameSpecials());
    const QString suggested = QDir(dir).filePath(fileName);
    const QString filter = ManualSave::filterFor(fileName);

    const QString title = I18n::instance().t(QStringLiteral("snip.save_title"));
    m_suspendFocusQuit = true;                       // our own dialogs deactivate the app
    const QString path = QFileDialog::getSaveFileName(nullptr, title, suggested, filter);
    if (!path.isEmpty() && SaveFeedback::confirmIcoDownscale(nullptr, img, path)) {
        QString err;
        if (ImageExport::toFile(img, path, quality, removeAlphaOnSave(), &err, pdfPageMode())) {
            ManualSave::rememberExtension(m_appSettings, path);
            recordHistory(img);                      // only a file that exists belongs in history
        } else {
            SaveFeedback::warnSaveFailed(nullptr, path, err);
        }
    }
    m_suspendFocusQuit = false;
    if (thenQuit) {
        finish();
    } else {
        for (SnipScreenView* v : m_views)            // dialog dismissed: restore the overlays
            v->show();
        for (QWidget* w : floatingBars)              // ...and the bars that were up with them
            w->show();
        updateAllViews();
    }
}

void SnipOverlay::quickSave(bool thenQuit)
{
    commitTextEditor();             // bake any open text box before flattening
    const QImage base = compositeBase(m_sel->rect());
    const QImage img = RoundedCorners::apply(m_doc->flatten(base, m_sel->rect()),
                                             effectiveCornerRadius());
    if (img.isNull()) {
        if (thenQuit) finish();
        return;
    }
    // Quick-save writes straight to the Output→Quick Save path template (a full path,
    // directory included) with no dialog. Falls back to Desktop + the default name.
    QString pathTemplate = QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))
                               .filePath(QStringLiteral("SnapCap_$yyyy-MM-dd_HH-mm-ss$.png"));
    if (m_appSettings) {
        const QString t = m_appSettings->value(QStringLiteral("output/quickSave/pathTemplate"),
                                               QString()).toString();
        if (!t.isEmpty())
            pathTemplate = t;
    }
    const QString path = FilenameTemplate::expandNow(pathTemplate, filenameSpecials());
    QDir().mkpath(QFileInfo(path).path());
    const bool ok = ImageExport::toFile(img, path, outputQuality(), removeAlphaOnSave(),
                                        nullptr, pdfPageMode());
    recordHistory(img);

    // Toast (output/quickSave/showNotification), routed to the tray via the app.
    if (ok && m_notifySink && m_appSettings
        && m_appSettings->value(QStringLiteral("output/quickSave/showNotification"), true).toBool()) {
        m_notifySink(I18n::instance().t(QStringLiteral("snip.saved_notification")),
                     QDir::toNativeSeparators(path));
    }
    if (thenQuit) finish();
}

void SnipOverlay::finish()
{
    // Aborting (Esc, the toolbar's ×) while recording must still finalize the file
    // rather than abandon a half-written one. The worker's onFinished holds only a
    // QPointer to us, so it will skip the UI half and just fire the toast.
    if (m_recording && m_recorder) {
        m_recording = false;
        m_recorder->stop();
    }
    if (m_countdownTimer)
        m_countdownTimer->stop();
    if (m_recordBar) {              // top-level while recording; nothing else owns it
        m_recordBar->setParent(nullptr);
        m_recordBar->deleteLater();
        m_recordBar = nullptr;
    }
    // Drop the wheel hook and the session chrome. Both outlive nothing: the strip and
    // the bar are top-level windows we own outright.
    m_autoScroll = false;             // any step still in flight sees this and stops
    m_scrollMoving = m_moveDragging = false;
    m_scrolling = false;
    if (m_wheelWatch) m_wheelWatch->disarm();
    if (m_scrollIdle) m_scrollIdle->stop();
    if (m_scrollStrip) {
        m_scrollStrip->close();
        m_scrollStrip->deleteLater();
        m_scrollStrip = nullptr;
    }
    if (m_scrollBar) {
        m_scrollBar->setParent(nullptr);
        m_scrollBar->deleteLater();
        m_scrollBar = nullptr;
    }
    if (m_keyFilter)                // drop the LL keyboard hook before tearing down
        m_keyFilter->uninstall();
    if (m_textEditor) {             // discard any half-typed text box
        m_textEditor->deleteLater();
        m_textEditor = nullptr;
        m_textHost = nullptr;
    }
    if (m_ocrBar) {                 // detach the OCR sub-bar (parented to a view)
        m_ocrBar->setParent(nullptr);
        m_ocrBar->deleteLater();
        m_ocrBar = nullptr;
    }
    if (m_paintBar) {
        if (QWidget* sub = m_paintBar->subBar()) {  // detach the separate sub-bar first
            sub->setParent(nullptr);
            sub->deleteLater();
        }
        m_paintBar->setParent(nullptr);
        m_paintBar->deleteLater();
        m_paintBar = nullptr;
    }
    for (SnipScreenView* v : m_views) {
        v->close();
        v->deleteLater();
    }
    m_views.clear();
    deleteLater();
}
