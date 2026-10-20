#include "ui/SnipScreenView.h"
#include "ui/SnipOverlay.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QWindow>
#include <QScreen>
#include <QDebug>
#include <QGuiApplication>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLineEdit>
#include <QIntValidator>
#include <QToolTip>
#include <algorithm>
#include <cmath>

#include "core/i18n/I18n.h"
#include "core/ColorPicker.h"
#include "core/TipsPlacement.h"
#include "core/barcode/BarcodeHint.h"
#include "core/annotation/AnnotationDocument.h"

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {
constexpr int kMagViewPx = 15;                 // source pixels shown (odd -> true center pixel)
                                               // rows always; columns unless a wide readout
                                               // value needs more (see drawMagnifier)
constexpr int kMagCell   = 12;                 // screen px per source pixel at 100% size
                                               // (the default loupe is 15 * 12 = 180 px)
}

SnipScreenView::SnipScreenView(SnipOverlay* controller, const ScreenShot& shot, QWidget* parent)
    : QWidget(parent), m_controller(controller), m_s(shot)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // Translucent from the start, though it changes nothing for a normal snip: the
    // frozen shot is opaque and covers the whole widget. Setting it up front is what
    // lets recording mode simply stop painting the shot and reveal the live desktop —
    // switching this attribute on a shown window would rebuild it and drop both the
    // capture exclusion and the z-order.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void SnipScreenView::showOnScreen()
{
    createWinId();
    QWindow* wh = windowHandle();
    if (wh)
        wh->setScreen(m_s.screen);
    setGeometry(m_s.geo);
    // A frameless, always-on-top window at the monitor's exact logical geometry already
    // covers it fully. showFullScreen() was retargeting a not-yet-realized frameless
    // window to the WRONG monitor under mixed DPI (leaving the other monitor undimmed —
    // bug 1), so place deterministically: show() at the geometry, then re-assert the
    // intended screen + geometry once the window is realized.
    show();
    if (wh)
        wh->setScreen(m_s.screen);
    setGeometry(m_s.geo);
    raise();

    qInfo() << "[snip] overlay on" << (m_s.screen ? m_s.screen->name() : QStringLiteral("?"))
            << "intended" << m_s.geo << "final" << geometry()
            << "landed on" << (wh && wh->screen() ? wh->screen()->name() : QStringLiteral("?"));
}

void SnipScreenView::setRecordingLive(bool on)
{
    if (m_recordingLive == on)
        return;
    m_recordingLive = on;
    update();
}

void SnipScreenView::setScrollPreview(const QImage& img)
{
    m_scrollPreview = img;
    update();
}

void SnipScreenView::setClickThrough(bool on)
{
#ifdef Q_OS_WIN
    createWinId();
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    // WS_EX_LAYERED is required for WS_EX_TRANSPARENT to actually pass clicks through
    // on a top-level window; Qt's translucent-background windows are already layered,
    // but set it explicitly so this does not depend on that implementation detail.
    ex = on ? (ex | WS_EX_TRANSPARENT | WS_EX_LAYERED) : (ex & ~WS_EX_TRANSPARENT);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
#else
    Q_UNUSED(on);
#endif
}

bool SnipScreenView::setExcludedFromCapture(bool on)
{
#ifdef Q_OS_WIN
    // WDA_EXCLUDEFROMCAPTURE is Windows 10 2004+; older SDK headers may not declare it.
#  ifndef WDA_EXCLUDEFROMCAPTURE
#    define WDA_EXCLUDEFROMCAPTURE 0x00000011
#  endif
    createWinId();
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    return SetWindowDisplayAffinity(hwnd, on ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE) != FALSE;
#else
    Q_UNUSED(on);
    return false;
#endif
}

void SnipScreenView::forceForeground()
{
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    // A global hotkey leaves the foreground lock on the app we came from, so plain
    // SetForegroundWindow is ignored. Two bypasses together make it reliable: drop the
    // foreground-lock timeout to 0, AND attach to the current foreground thread's input
    // queue. Both are restored afterwards.
    DWORD prevTimeout = 0;
    SystemParametersInfoW(SPI_GETFOREGROUNDLOCKTIMEOUT, 0, &prevTimeout, 0);
    SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, nullptr, SPIF_SENDCHANGE);

    const HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD myThread = GetCurrentThreadId();
    const bool attach = fgThread && fgThread != myThread;
    if (attach)
        AttachThreadInput(myThread, fgThread, TRUE);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    if (attach)
        AttachThreadInput(myThread, fgThread, FALSE);

    SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0,
                          reinterpret_cast<PVOID>(static_cast<UINT_PTR>(prevTimeout)),
                          SPIF_SENDCHANGE);
#endif
    activateWindow();
    setFocus(Qt::OtherFocusReason);
    repaint();                           // paint the mask now, not on the first mouse event
}

void SnipScreenView::seedCursor(const QPoint& global)
{
    if (!m_s.geo.contains(global))
        return;                          // the cursor is on another monitor
    m_cursor = global - m_s.geo.topLeft();
    m_haveCursor = true;
    if (!m_s.shot.isNull())              // feed the C-copy color readout too
        m_controller->reportHoverColor(ColorPicker::sampleAt(m_s.shot, cursorPhysLocal()));
}

void SnipScreenView::setBackground(const QImage& shot, qreal dpr)
{
    m_s.shot = shot;
    m_s.dpr = dpr > 0.0 ? dpr : 1.0;
    update();
}

// --- capture <-> local coordinate bridges -----------------------------------
QPoint SnipScreenView::toCapture(const QPointF& local) const
{
    return QPoint(int(std::lround(m_s.captureGeo.left() + local.x() * scaleX())),
                  int(std::lround(m_s.captureGeo.top()  + local.y() * scaleY())));
}

QRect SnipScreenView::toCaptureRect(const QRect& local) const
{
    const QPoint tl = toCapture(local.topLeft());
    const QPoint br = toCapture(local.topLeft() + QPoint(local.width(), local.height()));
    return QRect(tl, QSize(br.x() - tl.x(), br.y() - tl.y()));
}

QPointF SnipScreenView::toLocalF(const QPoint& cap) const
{
    return QPointF((cap.x() - m_s.captureGeo.left()) / scaleX(),
                   (cap.y() - m_s.captureGeo.top())  / scaleY());
}

QRect SnipScreenView::toLocalRect(const QRect& cap) const
{
    const qreal sx = scaleX(), sy = scaleY();
    const int l = int(std::lround((cap.left()               - m_s.captureGeo.left()) / sx));
    const int r = int(std::lround((cap.left() + cap.width() - m_s.captureGeo.left()) / sx));
    const int t = int(std::lround((cap.top()                - m_s.captureGeo.top())  / sy));
    const int b = int(std::lround((cap.top()  + cap.height() - m_s.captureGeo.top())  / sy));
    return QRect(l, t, r - l, b - t);
}

QRectF SnipScreenView::toLocalRectF(const QRectF& cap) const
{
    const qreal sx = scaleX(), sy = scaleY();
    return QRectF((cap.x() - m_s.captureGeo.left()) / sx, (cap.y() - m_s.captureGeo.top()) / sy,
                  cap.width() / sx, cap.height() / sy);
}

void SnipScreenView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    m_magRect = QRect();                           // re-filled by drawMagnifier if it runs

    // Recording: the window is transparent so the LIVE desktop shows through. Only the
    // region outline, the annotations and the active tool's cursor are ours to draw — no
    // frozen shot, no dim, no selection chrome (grips would invite a resize the recorder
    // can't follow), and no magnifier/tips. Annotations still render so you can see what
    // you are drawing; the recorder burns its own copy into the frames.
    if (m_recordingLive) {
        if (!m_controller->selectionValid())
            return;
        const QRect lsel = toLocalRect(m_controller->selection());
        // Windows hit-tests a per-pixel-alpha layered window (which WA_TranslucentBackground
        // makes this) by its ALPHA: fully transparent pixels hand the mouse to the desktop
        // and the window is never even asked, whatever WS_EX_TRANSPARENT says. So an armed
        // tool needs hittable pixels to draw on — an alpha-1 veil, invisible to the eye and
        // absent from the video (the views are excluded from capture for the whole snip).
        // Region-only, so the rest of the desktop stays usable mid-take.
        // Taking the clicks takes the foreground too: the first click activates the
        // overlay, so the recorded app visibly goes inactive in the video. The escape
        // hatch, if that ever matters: answer WM_MOUSEACTIVATE with MA_NOACTIVATE in
        // nativeEvent, except while the Text tool (which needs focus) is armed.
        if (m_controller->recordingInputArmed())
            p.fillRect(lsel.intersected(rect()), QColor(0, 0, 0, 1));
        paintAnnotations(p, lsel.intersected(rect()));
        QPen border(m_controller->accentColor(), m_controller->borderWidth());
        border.setJoinStyle(Qt::MiterJoin);
        p.setPen(border);
        p.setBrush(Qt::NoBrush);
        const int inset = m_controller->borderWidth() / 2;
        p.drawRect(lsel.adjusted(-inset, -inset, inset, inset));
        // The one piece of chrome this branch DOES owe: cursorAt() blanks the system
        // cursor for the pen/eraser/badge tools whether recording or not, so without
        // their drawn replacement those tools would have no visible pointer at all.
        drawToolCursor(p);
        return;
    }

    if (!m_s.shot.isNull())
        p.drawImage(rect(), m_s.shot);             // device px -> logical

    // Freeze-time cursor (snip/captureCursor), drawn as part of the frozen screen:
    // once here so it dims with the background, then again (clipped) over each bright
    // slice below so it stays bright inside the selection. Toggled live with ` / !.
    auto drawCapturedCursor = [&](const QRect& clip) {
        if (!m_controller->captureCursorActive()) return;
        const QRectF lr = toLocalRectF(m_controller->capturedCursorRectCapture());
        p.save();
        if (clip.isValid())
            p.setClipRect(clip);
        p.drawImage(lr, m_controller->capturedCursorImage());
        p.restore();
    };
    drawCapturedCursor(QRect());                      // full (about to be dimmed)

    QColor mask = m_controller->maskColor();         // RGB from snip/display/maskColor
    mask.setAlpha(110);                              // translucency kept fixed for now
    // The colour picker leaves the frozen desktop at full brightness so the loupe reads
    // (and the eye sees) true colours; every other mode dims it.
    if (!m_controller->colorPickOnly())
        p.fillRect(rect(), mask);                     // dim

    // Auxiliary guide lines (toggled by Alt): dashed lines through the region's
    // left/center/right and top/center/bottom, spanning the screen. Called below AFTER
    // the bright slice but BEFORE the selection border and control points, so it sits
    // under them; disabled by snip/display/noAuxLines via showAuxLines().
    auto drawAuxLines = [&](const QRect& region) {
        if (!m_controller->showAuxLines() || !region.isValid()) return;
        const QRect a = toLocalRect(region);
        QPen auxPen(m_controller->accentColor(), m_controller->borderWidth());
        auxPen.setDashPattern({5, 3});
        auxPen.setCapStyle(Qt::FlatCap); // Add this to prevent cap bleed
        p.setPen(auxPen);
        for (int x : {a.left(), a.center().x(), a.right()})
            p.drawLine(x, 0, x, height());
        for (int y : {a.top(), a.center().y(), a.bottom()})
            p.drawLine(0, y, width(), y);
    };

    if (m_controller->selectionValid()) {
        const QRect lsel = toLocalRect(m_controller->selection());
        const QRect overlap = lsel.intersected(rect());
        if (!overlap.isEmpty()) {
            const QRectF src(overlap.x() * m_s.dpr, overlap.y() * m_s.dpr,
                             overlap.width() * m_s.dpr, overlap.height() * m_s.dpr);
            p.drawImage(QRectF(overlap), m_s.shot, src);   // bright slice
        }
        // Scrolling-capture result: replace the bright slice with the stitched image,
        // scaled to fit and centred, on a dark letterbox. The frozen pixels underneath
        // are stale anyway — the window scrolled — and this is what Copy/Save/Pin will
        // act on, so it is what the region must show.
        if (!m_scrollPreview.isNull() && !lsel.intersected(rect()).isEmpty()) {
            p.save();
            p.setClipRect(lsel.intersected(rect()));
            p.fillRect(lsel, QColor(0x1e, 0x1e, 0x1e));
            QSize fit = m_scrollPreview.size();
            fit.scale(lsel.size(), Qt::KeepAspectRatio);
            const QRect dst(lsel.center() - QPoint(fit.width() / 2, fit.height() / 2), fit);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(dst, m_scrollPreview);
            p.restore();
        }
        drawCapturedCursor(lsel.intersected(rect()));    // bright cursor inside the selection
        // Annotations render on top of the bright slice, clipped to the selection.
        paintAnnotations(p, lsel.intersected(rect()));

        // Rounded corners: put the dim mask BACK over the four corner slivers the export
        // will cut away. Painting the same mask (rather than clipping the bright slice)
        // keeps the corners looking exactly like the surrounding dimmed desktop and gets
        // antialiased arcs — QPainter's clip paths are aliased and would look ragged.
        const qreal rLocal = cornerRadiusLocal();
        if (rLocal > 0.0) {
            QPainterPath full;
            full.addRect(QRectF(lsel));
            QPainterPath rounded;
            rounded.addRoundedRect(QRectF(lsel), rLocal, rLocal);
            p.save();
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(mask);
            p.drawPath(full.subtracted(rounded));
            p.restore();
        }

        drawAuxLines(m_controller->selection());   // under the border + control points

        p.setPen(QPen(m_controller->accentColor(), m_controller->borderWidth()));
        p.setBrush(Qt::NoBrush);
        if (rLocal > 0.0) {
            p.save();
            p.setRenderHint(QPainter::Antialiasing, true);
            p.drawRoundedRect(QRectF(lsel.adjusted(0, 0, -1, -1)), rLocal, rLocal);
            p.restore();
        } else {
            p.drawRect(lsel.adjusted(0, 0, -1, -1));
        }

        // 8-direction resize grips (read-only mode): squares with a blue edge, at the
        // corners + edge midpoints of the committed region (snip/display/showAnchors).
        if (m_controller->isReadOnly() && m_controller->showAnchors()) {
            const int cx = lsel.center().x(), cy = lsel.center().y();
            const QPoint grips[8] = {
                {lsel.left(), lsel.top()},    {cx, lsel.top()},    {lsel.right(), lsel.top()},
                {lsel.right(), cy},           {lsel.right(), lsel.bottom()},
                {cx, lsel.bottom()},          {lsel.left(), lsel.bottom()}, {lsel.left(), cy},
            };
            constexpr int gh = 4;             // grip half-size (screen px)
            p.setBrush(m_controller->anchorColor());
            p.setPen(QPen(m_controller->anchorBorderColor(), 2));
            for (const QPoint& gp : grips)
                p.drawRect(QRect(gp.x() - gh, gp.y() - gh, 2 * gh, 2 * gh));
            p.setBrush(Qt::NoBrush);
        }

        // Position + size readout above the region. save/restore so the enlarged font
        // does NOT leak into the magnifier, whose readout fonts are derived from the
        // painter's current font.
        const HintLayout hl = hintLayout();
        if (hl.valid) {
            p.save();
            p.setFont(hl.font);
            p.setPen(Qt::white);
            for (int i = 0; i < 2; ++i) {
                p.fillRect(hl.box[i], QColor(0, 0, 0, 170));
                p.drawText(hl.box[i], Qt::AlignCenter, hl.label[i]);
            }
            // corner-shape chip: the icon shows the shape currently in force; the bending
            // level sits beside it in rounded mode only (right angles have none to show).
            if (hl.cornerBox.isValid()) {
                const bool on = m_controller->cornerRounded();
                p.fillRect(hl.cornerBox, on ? QColor(m_controller->accentColor())
                                            : QColor(0, 0, 0, 170));
                drawCornerGlyph(p, hl.cornerGlyph, Qt::white, on);
                if (!hl.cornerText.isEmpty()) {
                    p.setPen(Qt::white);
                    p.drawText(QRect(hl.cornerGlyph.right() + 6, hl.cornerBox.top(),
                                     hl.cornerBox.right() - hl.cornerGlyph.right() - 6,
                                     hl.cornerBox.height()),
                               Qt::AlignVCenter | Qt::AlignLeft, hl.cornerText);
                }
            }
            // refresh button: same plate, brightened while a continuous refresh runs so
            // the hold is visibly doing something.
            if (hl.refreshBox.isValid()) {
                const bool live = m_controller->refreshing();
                p.fillRect(hl.refreshBox, live ? QColor(m_controller->accentColor())
                                               : QColor(0, 0, 0, 170));
                drawRefreshGlyph(p, hl.refreshGlyph, Qt::white, m_controller->refreshSpin());
            }
            // code chip: same plate as the readout boxes, glyph then the elided link.
            if (hl.barcodeBox.isValid()) {
                p.fillRect(hl.barcodeBox, QColor(0, 0, 0, 170));
                drawBarcodeGlyph(p, hl.barcodeGlyph, Qt::white);
                p.drawText(QRect(hl.barcodeGlyph.right() + 6, hl.barcodeBox.top(),
                                 hl.barcodeBox.right() - hl.barcodeGlyph.right() - 6, hl.barcodeBox.height()),
                           Qt::AlignVCenter | Qt::AlignLeft, hl.barcodeText);
            }
            p.restore();
        }
    } else if (m_controller->hasSuggestion()) {
        // Auto-detected window/element candidate (dashed) — click to adopt it.
        const QRect lsel = toLocalRect(m_controller->suggestion());
        const QRect overlap = lsel.intersected(rect());
        if (!overlap.isEmpty()) {
            const QRectF src(overlap.x() * m_s.dpr, overlap.y() * m_s.dpr,
                             overlap.width() * m_s.dpr, overlap.height() * m_s.dpr);
            p.drawImage(QRectF(overlap), m_s.shot, src);
        }
        drawCapturedCursor(lsel.intersected(rect()));   // bright cursor inside the candidate
        drawAuxLines(m_controller->suggestion());   // under the candidate border
        p.setPen(QPen(m_controller->accentColor(), m_controller->borderWidth()));
        p.setBrush(Qt::NoBrush);
        p.drawRect(lsel.adjusted(0, 0, -1, -1));
    }

    // Fullscreen crosshair while choosing a region (snip/display/crossLines): the cross
    // meets exactly at the cursor; the system cross cursor is blanked in mouseMoveEvent.
    if (m_controller->crossLines() && m_haveCursor && !m_controller->isEditing()) {
        p.setPen(QPen(m_controller->accentColor(), 1));
        p.drawLine(0, m_cursor.y(), width(), m_cursor.y());
        p.drawLine(m_cursor.x(), 0, m_cursor.x(), height());
    }

    // The loupe + colour readout follow the cursor freely while choosing a region;
    // once the region is determined (editing) they only show when the cursor is
    // inside the capture region. They never show while an annotation tool is active
    // (only when no tool is selected).
    bool showMag = m_haveCursor && !m_controller->toolSelected() && !m_controller->ocrActive();
    if (showMag && m_controller->isEditing())
        showMag = m_controller->selection().contains(toCapture(m_cursor));
    // save/restore so the magnifier's size-scaled readout font does NOT leak into the
    // panels drawn after it (the key-tips panel and history strip both derive their
    // fonts from the painter's current font).
    if (showMag) {
        p.save();
        drawMagnifier(p, m_cursor);
        p.restore();
    }

    drawToolCursor(p);

    // OCR mode: grey "selectable text" plates over each word + the accent selection.
    m_controller->drawOcrLayer(p, this);

    // The key-binding tips panel (corner hint list) sits below the history strip.
    drawKeyTips(p);

    // The history strip floats above everything on the screen that holds it.
    drawHistoryStrip(p);
}

void SnipScreenView::drawToolCursor(QPainter& p)
{
    if (!m_haveCursor || !m_controller->selection().contains(toCapture(m_cursor)))
        return;

    // Circle eraser + mosaic/blur brush: a brush cursor at the pointer previewing the
    // affected region (the system cursor is blanked over the region by cursorAt), drawn
    // only inside the capture region. The eraser shows two thin rings; the redact brush
    // fills the ring with translucent gray to read as "this area gets mosaiced".
    const annotation::ToolId at = m_controller->activeTool();
    const bool redactBrush = at == annotation::ToolId::MosaicBrush
                          || at == annotation::ToolId::BlurBrush;
    if (at == annotation::ToolId::Eraser || redactBrush) {
        if (redactBrush) drawMosaicCursor(p, m_cursor);
        else             drawEraserCursor(p, m_cursor);
    }

    // Pen: a dot sized to the stroke width framed by a crosshair (system cursor blanked
    // by cursorAt), drawn only inside the capture region.
    if (at == annotation::ToolId::Pencil)
        drawPenCursor(p, m_cursor);

    // Number label: the badge the next click will stamp, drawn at the pointer in the
    // controller's LOGICAL style — i.e. at exactly the on-screen size, colour and look
    // it will have once committed (the system cursor is blanked by cursorAt).
    if (at == annotation::ToolId::NumberLabel) {
        const QString n = m_controller->nextLabelNumber();
        if (!n.isEmpty())
            annotation::paintNumberBadge(p, m_controller->activeStyle(),
                                         QPointF(m_cursor), n);
    }
}

void SnipScreenView::drawEraserCursor(QPainter& p, const QPoint& cursorLocal)
{
    const double r = qMax(2, m_controller->activePenWidth()) / 2.0;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    // White underlay + black overlay so both rings read on any background.
    auto ring = [&](double rad) {
        p.setPen(QPen(QColor(255, 255, 255, 235), 2.0));
        p.drawEllipse(QPointF(cursorLocal), rad, rad);
        p.setPen(QPen(QColor(0, 0, 0, 235), 1.0));
        p.drawEllipse(QPointF(cursorLocal), rad, rad);
    };
    ring(r);          // outer ring = brush boundary (erased diameter)
    ring(r-3);        // inner ring marks the precise center
    p.restore();
}

void SnipScreenView::drawMosaicCursor(QPainter& p, const QPoint& cursorLocal)
{
    const double r = qMax(2, m_controller->activePenWidth()) / 2.0;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    // Translucent gray fill = the region the brush will redact; a white-under/black-over
    // ring keeps the boundary legible on any background.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(128, 128, 128, 110));
    p.drawEllipse(QPointF(cursorLocal), r, r);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 235), 2.0));
    p.drawEllipse(QPointF(cursorLocal), r, r);
    p.setPen(QPen(QColor(0, 0, 0, 235), 1.0));
    p.drawEllipse(QPointF(cursorLocal), r, r);
    p.restore();
}

void SnipScreenView::drawPenCursor(QPainter& p, const QPoint& cursorLocal)
{
    const QPointF c(cursorLocal);
    const double r = qMax(1.0, m_controller->activePenWidth() / 2.0);   // dot = stroke radius
    const QColor col = m_controller->activeColor();
    const QColor outline(0, 0, 0, 235);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // Crosshair arms around the dot, with a small gap so the dot stays legible. Each arm
    // is the pen colour over a thicker black underlay, giving it a black outline that
    // reads on any background (the dot below uses the same colour + outline).
    const double gap   = r + 4.0;        // arm starts just beyond the dot edge
    const double reach = gap + 6.0;      // arm length
    auto arm = [&](const QPointF& a, const QPointF& b) {
        p.setPen(QPen(outline, 1.8)); p.drawLine(a, b);   // black outline
        p.setPen(QPen(col, 1.4));     p.drawLine(a, b);   // pen-colour core
    };
    arm({c.x(), c.y() - gap}, {c.x(), c.y() - reach});   // up
    arm({c.x(), c.y() + gap}, {c.x(), c.y() + reach});   // down
    arm({c.x() - gap, c.y()}, {c.x() - reach, c.y()});   // left
    arm({c.x() + gap, c.y()}, {c.x() + reach, c.y()});   // right

    // The dot itself: filled with the current pen colour (so the cursor previews the
    // colour too) with a black outline so its size stays visible on any background.
    p.setPen(QPen(outline, 0.5));
    p.setBrush(col);
    p.drawEllipse(c, r, r);
    p.restore();
}

void SnipScreenView::paintAnnotations(QPainter& p, const QRect& clipLocal)
{
    AnnotationDocument* doc = m_controller->document();
    if (!doc)
        return;
    // Annotation geometry is capture px; the frozen shot is this monitor's capture
    // pixels (captureGeo, 1:1). Render capture-space points into this view's local
    // coords via one transform (scale 1/scale + translate −captureGeo.topLeft()); the
    // ctx describes the base in capture units so redaction samples the right pixels.
    annotation::PaintCtx ctx;
    ctx.base = &m_s.shot;                  // sample THIS screen's frozen pixels
    ctx.originLogical = m_s.captureGeo.topLeft();   // capture coord of base's pixel (0,0)
    ctx.scale = 1.0;                                 // base px per capture unit (1:1)

    p.save();
    p.setClipRect(clipLocal);
    p.scale(1.0 / scaleX(), 1.0 / scaleY());
    p.translate(-m_s.captureGeo.topLeft());
    // Pass the live draft INTO paint() so a draft mosaic/blur composites over the
    // committed annotations beneath it (redaction samples the composite-so-far).
    doc->paint(p, ctx, m_controller->draft());
    // The blinking caret for a badge being renumbered. Drawn inside the same capture-space
    // transform as the badge, so it scales with it; core supplies the rect so the caret
    // tracks the (possibly shrunk-to-fit) digit font.
    if (m_controller->labelCaretVisible()) {
        if (const annotation::Annotation* a = m_controller->editedLabel()) {
            if (!a->geometry().isEmpty())
                p.fillRect(annotation::numberBadgeCaretRect(
                               a->style, QPointF(a->geometry().first()), a->text()),
                           annotation::numberBadgeInk(a->style));
        }
    }
    p.restore();

    // Chrome (control points, text-hover outline) at fixed on-screen size: drawn in
    // local coords, mapping each capture-space point back to this view.
    const QVector<QPoint> cps = m_controller->lineControlPoints();
    if (!cps.isEmpty()) {
        p.save();
        p.setClipRect(clipLocal);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::white);
        p.setPen(QPen(m_controller->accentColor(), 1.5));
        for (const QPoint& c : cps) {
            const QPointF l = toLocalF(c);
            p.drawRect(QRectF(l.x() - 4, l.y() - 4, 8, 8));
        }
        p.restore();
    }
    // Text tool: a dashed outline around the committed text box under the cursor — a
    // click there re-opens it for editing.
    const QVector<QPoint> tho = m_controller->textHoverOutline();
    if (tho.size() >= 2) {
        QVector<QPointF> loc;
        loc.reserve(tho.size());
        for (const QPoint& c : tho)
            loc.append(toLocalF(c));
        p.save();
        p.setClipRect(clipLocal);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::NoBrush);
        // White underlay + blue dashes so the box reads on any background.
        p.setPen(QPen(QColor(255, 255, 255, 220), 2.0));
        p.drawPolygon(loc.constData(), loc.size());
        p.setPen(QPen(m_controller->accentColor(), 1.2, Qt::DashLine));
        p.drawPolygon(loc.constData(), loc.size());
        p.restore();
    }
    // On-canvas shape hover: a shape-matched dashed outline (rect follows the box, ellipse
    // an ellipse) on the committed non-text shape under the cursor. Drawn in capture space
    // (like the annotations) with cosmetic pens so the outline keeps a fixed on-screen width.
    const QPainterPath hoverPath = m_controller->shapeHoverPath();
    if (!hoverPath.isEmpty()) {
        p.save();
        p.setClipRect(clipLocal);
        p.scale(1.0 / scaleX(), 1.0 / scaleY());
        p.translate(-m_s.captureGeo.topLeft());
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::NoBrush);
        QPen underlay(QColor(255, 255, 255, 220), 2.0);
        underlay.setCosmetic(true);
        p.setPen(underlay);
        p.drawPath(hoverPath);
        QPen dashed(m_controller->accentColor(), 1.2, Qt::DashLine);
        dashed.setCosmetic(true);
        p.setPen(dashed);
        p.drawPath(hoverPath);
        p.restore();
    }
    // On-canvas shape selection: the oriented frame, 8 resize grips (corners + edge
    // midpoints) and the spin handle, all at fixed on-screen size (mapped per point).
    const SnipOverlay::ShapeHandles sh = m_controller->shapeHandles();
    if (sh.active && sh.grips.size() == 8 && sh.box.size() == 4) {
        p.save();
        p.setClipRect(clipLocal);
        p.setRenderHint(QPainter::Antialiasing, true);
        QVector<QPointF> box;
        for (const QPoint& c : sh.box) box.append(toLocalF(c));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 200), 2.0));   // white underlay
        p.drawPolygon(box.constData(), box.size());
        p.setPen(QPen(m_controller->accentColor(), 1.2));   // themed frame
        p.drawPolygon(box.constData(), box.size());
        const QColor accent = m_controller->accentColor();
        // Stalks from the box up to the spin handle (top-centre) and the × button (top-right).
        const QPointF rh = toLocalF(sh.rotateHandle), ra = toLocalF(sh.rotateAnchor);
        const QPointF dh = toLocalF(sh.deleteHandle), da = toLocalF(sh.deleteAnchor);
        p.setPen(QPen(accent, 1));
        p.drawLine(ra, rh);
        p.drawLine(da, dh);
        // A handle plate: a white disc with an accent rim (matches the Text tool's kHandleR=9).
        auto plate = [&](const QPointF& c) {
            p.setPen(QPen(accent, 1));
            p.setBrush(Qt::white);
            p.drawEllipse(c, 9, 9);
        };
        // Spin handle: the disc + a 270° circular-arrow glyph, oriented with the box.
        plate(rh);
        {
            p.save();
            p.translate(rh);
            p.rotate(sh.rotationDeg);
            const qreal rr = 4;
            const QRectF arcBox(-rr, -rr, 2 * rr, 2 * rr);
            QPainterPath arc;
            arc.arcMoveTo(arcBox, 60);
            arc.arcTo(arcBox, 60, 270);
            p.setPen(QPen(accent.darker(150), 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawPath(arc);
            p.restore();
        }
        // Delete button: the disc + a red × glyph, oriented with the box.
        plate(dh);
        {
            p.save();
            p.translate(dh);
            p.rotate(sh.rotationDeg);
            const qreal d = 3;
            p.setPen(QPen(QColor(0xc0, 0x39, 0x2b), 1.6));
            p.drawLine(QPointF(-d, -d), QPointF(d, d));
            p.drawLine(QPointF(-d, d), QPointF(d, -d));
            p.restore();
        }
        // The 8 grips.
        p.setPen(QPen(accent, 1));
        p.setBrush(Qt::white);
        for (const QPoint& c : sh.grips) {
            const QPointF l = toLocalF(c);
            p.drawRect(QRectF(l.x() - 4, l.y() - 4, 8, 8));
        }
        p.restore();
    }
    // The live degree readout while a shape is being rotated (screen-aligned opaque pill,
    // past the spin handle) — same as the Text tool's rotate feedback.
    const SnipOverlay::ShapeRotateReadout rr = m_controller->shapeRotateReadout();
    if (rr.active) {
        p.save();
        p.setClipRect(clipLocal);
        QFont vf = font();
        vf.setBold(true);
        p.setFont(vf);
        const QFontMetrics fm(vf);
        QRect pill(0, 0, fm.horizontalAdvance(rr.text) + 12, fm.height() + 4);
        pill.moveCenter(toLocalF(rr.pos).toPoint());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 190));
        p.drawRoundedRect(pill, 4, 4);
        p.setPen(Qt::white);
        p.drawText(pill, Qt::AlignCenter, rr.text);
        p.restore();
    }
    // Quick-mark: a dashed box around the UI element under the cursor while Alt is held,
    // previewing exactly what Alt+right-click would fill. Same white-underlay + dashed
    // look as the text hover above, but deliberately NOT clipped to the selection: the
    // mark keeps the element's full bounds, so the part hanging outside the region is the
    // one thing the preview most needs to show.
    const QRect qm = m_controller->quickMarkRect();
    if (!qm.isNull()) {
        const QRectF loc(toLocalF(qm.topLeft()), toLocalF(qm.bottomRight()));
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 220), 2.0));
        p.drawRect(loc);
        p.setPen(QPen(m_controller->accentColor(), 1.2, Qt::DashLine));
        p.drawRect(loc);
        p.restore();
    }
    // Number label being renumbered: the +/− step buttons on the right of its outline.
    // A step swaps numbers with the badge holding the target, so it reorders without
    // disturbing the others; greyed out at the ends of the badges' number range.
    const QVector<QRect> steps = m_controller->labelStepButtons();
    const QRect del = m_controller->labelDeleteButton();
    if (steps.size() == 2 || del.isValid()) {
        p.save();
        p.setClipRect(clipLocal);
        p.setRenderHint(QPainter::Antialiasing, true);
        // A white plate with a themed border, so the button reads on any background.
        auto plate = [&](const QRectF& r, const QColor& edge, bool on) {
            p.setPen(QPen(QColor(255, 255, 255, 220), 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 3, 3);
            p.setPen(QPen(on ? edge : QColor(150, 150, 150), 1.2));
            p.setBrush(QColor(255, 255, 255, on ? 235 : 140));
            p.drawRoundedRect(r, 3, 3);
        };
        for (int i = 0; i < steps.size(); ++i) {
            const int delta = (i == 0) ? +1 : -1;
            const bool on = m_controller->labelCanStep(delta);
            const QRectF r = toLocalRectF(QRectF(steps[i]));
            plate(r, m_controller->accentColor(), on);
            // The glyph: a full bar for −, a crossed pair for +.
            const QPointF c = r.center();
            const qreal a = r.width() * 0.28;
            p.setPen(QPen(on ? QColor(40, 40, 40) : QColor(160, 160, 160), 1.6));
            p.drawLine(QPointF(c.x() - a, c.y()), QPointF(c.x() + a, c.y()));
            if (delta > 0)
                p.drawLine(QPointF(c.x(), c.y() - a), QPointF(c.x(), c.y() + a));
        }
        if (del.isValid()) {
            // × sits above the outline, away from the steppers, and is drawn in red — it
            // destroys the badge, unlike everything else on this chrome.
            const QColor red(0xc0, 0x39, 0x2b);
            const QRectF r = toLocalRectF(QRectF(del));
            plate(r, red, true);
            const QPointF c = r.center();
            const qreal a = r.width() * 0.24;
            p.setPen(QPen(red, 1.8, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(c.x() - a, c.y() - a), QPointF(c.x() + a, c.y() + a));
            p.drawLine(QPointF(c.x() + a, c.y() - a), QPointF(c.x() - a, c.y() + a));
        }
        p.restore();
    }
}

QPoint SnipScreenView::cursorPhysLocal() const
{
#ifdef Q_OS_WIN
    // Our process is PerMonitor-V2, so GetCursorPos is in physical px == capture px
    // (captureGeo == rcMonitor). Subtracting this shot's capture origin gives the exact
    // physical pixel within the frozen image — no logical-grid rounding.
    POINT gp;
    if (GetCursorPos(&gp))
        return QPoint(int(gp.x), int(gp.y)) - m_s.captureGeo.topLeft();
#endif
    return QPoint(int(m_cursor.x() * m_s.dpr), int(m_cursor.y() * m_s.dpr));
}

namespace {
// One word of a magnifier hint line, carrying whether it is the bolded {key} token.
struct HintWord { QString text; bool bold; };
using HintLines = QVector<QVector<HintWord>>;

// Greedy word-wrap of a "Press {key} to …" template to maxW, so a long localized hint
// stacks inside the readout panel instead of spilling out of it. A single word too wide
// to fit on its own is broken by character (CJK locales have no spaces to break at).
HintLines wrapHint(const QString& templ, const QString& key, const QFont& base, int maxW)
{
    QFont reg = base;  reg.setBold(false);
    QFont bold = base; bold.setBold(true);
    const QFontMetrics fmR(reg), fmB(bold);

    const int idx = templ.indexOf(QStringLiteral("{key}"));
    QVector<HintWord> segs;
    if (idx >= 0)
        segs = {{templ.left(idx), false}, {key, true}, {templ.mid(idx + 5), false}};
    else
        segs = {{templ, false}};

    QVector<HintWord> words;
    for (const HintWord& seg : segs) {
        const QFontMetrics& fm = seg.bold ? fmB : fmR;
        for (const QString& w : seg.text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (fm.horizontalAdvance(w) <= maxW) {
                words.append({w, seg.bold});
                continue;
            }
            QString chunk;
            for (const QChar c : w) {
                if (!chunk.isEmpty() && fm.horizontalAdvance(chunk + c) > maxW) {
                    words.append({chunk, seg.bold});
                    chunk.clear();
                }
                chunk.append(c);
            }
            if (!chunk.isEmpty())
                words.append({chunk, seg.bold});
        }
    }

    const int spaceW = fmR.horizontalAdvance(QLatin1Char(' '));
    HintLines lines;
    QVector<HintWord> cur;
    int curW = 0;
    for (const HintWord& w : words) {
        const int ww = (w.bold ? fmB : fmR).horizontalAdvance(w.text);
        if (!cur.isEmpty() && curW + spaceW + ww > maxW) {
            lines.append(cur);
            cur.clear();
            curW = 0;
        }
        curW += (cur.isEmpty() ? 0 : spaceW) + ww;
        cur.append(w);
    }
    if (!cur.isEmpty())
        lines.append(cur);
    return lines;
}

// Draw wrapped hint lines, each centered in its own row of `area`, with the {key}
// token bolded and brightened.
void drawHintLines(QPainter& p, const QRect& area, const QFont& base,
                   const HintLines& lines, int lineH)
{
    QFont reg = base;  reg.setBold(false);
    QFont bold = base; bold.setBold(true);
    const QFontMetrics fmR(reg), fmB(bold);
    const int spaceW = fmR.horizontalAdvance(QLatin1Char(' '));

    for (int i = 0; i < lines.size(); ++i) {
        const QVector<HintWord>& words = lines.at(i);
        int lineW = 0;
        for (const HintWord& w : words)
            lineW += (w.bold ? fmB : fmR).horizontalAdvance(w.text)
                   + (&w == &words.first() ? 0 : spaceW);

        int x = area.left() + (area.width() - lineW) / 2;
        const int top = area.top() + i * lineH;
        const int baseY = top + (lineH + fmR.ascent() - fmR.descent()) / 2;
        for (const HintWord& w : words) {
            if (&w != &words.first())
                x += spaceW;
            p.setFont(w.bold ? bold : reg);
            p.setPen(w.bold ? QColor(Qt::white) : QColor(220, 220, 220));
            p.drawText(x, baseY, w.text);
            x += (w.bold ? fmB : fmR).horizontalAdvance(w.text);
        }
    }
}
}  // namespace

void SnipScreenView::drawMagnifier(QPainter& p, const QPoint& cursorLocal)
{
    if (m_s.shot.isNull())
        return;

    const int viewY = kMagViewPx;                   // source-pixel rows (fixed)
    // interface/magnifier/sizePercent scales the whole loupe + readout panel. The cell is
    // rounded to a whole number of screen px first (so the nearest-neighbour zoom keeps
    // crisp pixel edges), then the panel scale is re-derived from that integer cell so the
    // two halves stay in exact proportion instead of drifting apart by a pixel or two.
    const int cell = std::max(3, qRound(kMagCell * m_controller->magPercent() / 100.0));
    const double s = cell / double(kMagCell);
    const auto sc  = [s](int v) { return qRound(v * s); };

    const QPoint phys = cursorPhysLocal();          // exact physical px (not m_cursor*dpr)

    // The exact pixel under the cursor (clamped), for the readout + swatch.
    const QPoint cp(qBound(0, phys.x(), m_s.shot.width() - 1),
                    qBound(0, phys.y(), m_s.shot.height() - 1));
    const QColor color = m_s.shot.pixelColor(cp);
    const QPoint g = phys + m_s.captureGeo.topLeft();   // capture-px coord for the readout
    const QString coordStr = QStringLiteral("( %1 , %2 )").arg(g.x()).arg(g.y());

    // Every enabled format is listed, in the order the Interface preferences set;
    // the Shift-selected row is the one C copies.
    const QVector<ColorPicker::Format>& fmts = m_controller->enabledFormats();
    const int copyRow = m_controller->formatIndex();
    QVector<QPair<QString, QString>> rows;              // label -> formatted value
    rows.reserve(fmts.size());
    for (ColorPicker::Format f : fmts)
        rows.append({ColorPicker::labelFor(f),
                     ColorPicker::format(color, f, m_controller->uppercaseHex(),
                                         m_controller->noHash())});

    // Two fonts: a fixed-width readout (coords + color values, so digits don't jitter)
    // and a slightly larger hint font.
    QFont fMain = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    fMain.setPointSizeF((p.font().pointSizeF() + 2.0) * s);
    fMain.setBold(true);
    QFont fHint = p.font();
    fHint.setPointSizeF((p.font().pointSizeF() + 1.5) * s);
    const QFontMetrics fmMain(fMain);
    const QFontMetrics fmHint(fHint);

    const int kSwatch    = sc(14);                      // swatch bar height
    const int kSwatchW   = sc(52);                      // one swatch for the whole panel
    const int kPadX      = sc(10);                      // panel side padding
    const int kColonGap  = sc(8);                       // label column -> value column
    const bool showTips = m_controller->usageTips();
    const bool showCoords = m_controller->showCoordinates();
    const int lineMain  = fmMain.height() + 12;         // coordinate line
    const int lineRow   = fmMain.height() + 6;          // one format row (tighter)
    const int lineSw    = kSwatch + 10;                 // the lone swatch line
    const int lineHint  = fmHint.height() + 5;

    // Column widths: labels right-aligned to a common colon, values left-aligned
    // after it, so the list reads as a table however many formats are enabled.
    int labelW = 0, valueW = 0;
    for (const auto& [label, value] : rows) {
        labelW = std::max(labelW, fmMain.horizontalAdvance(label + QLatin1Char(':')));
        valueW = std::max(valueW, fmMain.horizontalAdvance(value));
    }

    // A long value (CMYK is the usual culprit) can need more width than the default
    // 15-cell loupe: the loupe then grows with it, showing extra source-pixel COLUMNS
    // rather than being letterboxed. The column count is kept odd so the sampled pixel
    // — and therefore the crosshair — stays dead centre; that also rounds the loupe up
    // to a whole cell, which is what finally sets the panel width for both halves.
    const int contentW = labelW + kColonGap + valueW + kPadX * 2;
    int viewX = kMagViewPx;                             // source-pixel columns
    if (contentW > viewX * cell) {
        viewX = (contentW + cell - 1) / cell;
        if (viewX % 2 == 0)
            ++viewX;
    }
    const int boxW   = viewX * cell;
    const int boxH   = viewY * cell;
    const int panelW = boxW;

    const QRect src(phys.x() - viewX / 2, phys.y() - viewY / 2, viewX, viewY);

    // The key hints never widen the panel — they wrap inside it instead, so a long
    // localized string can't stretch the magnifier.
    QVector<HintLines> hints;
    if (showTips) {
        hints.append(wrapHint(TR("snip.mag.copy_hint"), QStringLiteral("C"),
                              fHint, panelW - kPadX * 2));
        hints.append(wrapHint(TR("snip.mag.format_hint"), QStringLiteral("Shift"),
                              fHint, panelW - kPadX * 2));
    }
    int hintRows = 0;
    for (const HintLines& h : hints)
        hintRows += h.size();

    // coords (Coordinates pref) + swatch + one line per format, plus the key-hint
    // lines (as many as they wrapped to) only when Usage tips is on.
    const int panelH = (showCoords ? lineMain : 0) + lineSw + lineRow * rows.size()
                     + lineHint * hintRows + 6;

    // Place the loupe near the cursor, flipping to stay fully on-screen. Loupe and
    // panel are the same width now, so one width drives the flip.
    const int gap    = sc(18);                          // cursor -> loupe offset
    const int margin = sc(6);                           // keep-off-the-edge margin
    const int totalH = boxH + panelH;
    QPoint pos = cursorLocal + QPoint(gap, gap);
    if (pos.x() + panelW + margin > width())  pos.setX(cursorLocal.x() - panelW - gap);
    if (pos.y() + totalH + margin > height()) pos.setY(cursorLocal.y() - totalH - gap);
    pos.setX(std::max(0, pos.x()));
    pos.setY(std::max(0, pos.y()));
    const QRect dst(pos, QSize(boxW, boxH));
    // Record what the loupe+panel actually cover so drawKeyTips (which runs later in the
    // same paint) can place the tips clear of it. Taken from the real geometry rather than
    // re-predicted, so the two can't drift apart.
    m_magRect = QRect(pos, QSize(panelW, totalH));

    // Zoomed pixels (nearest-neighbor).
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(dst, m_s.shot, src);

    // Per-source-pixel grid (interface/magnifier/gridLines). Drawn before the crosshair,
    // the sampled-cell marker and the border so those all stay on top of it. A neutral
    // translucent grey so it reads over both light and dark content.
    if (m_controller->gridLines()) {
        p.setPen(QPen(QColor(128, 128, 128, 90), 1));
        for (int i = 0; i <= viewX; ++i) {
            const int x = dst.left() + i * cell;
            p.drawLine(x, dst.top(), x, dst.bottom());
        }
        for (int i = 0; i <= viewY; ++i) {
            const int y = dst.top() + i * cell;
            p.drawLine(dst.left(), y, dst.right(), y);
        }
    }

    // Full crosshair through the loupe: a translucent blue cross,
    // one source-pixel column/row wide, centered on the sampled pixel.
    const int cx = dst.left() + (viewX / 2) * cell + cell / 2;
    const int cy = dst.top()  + (viewY / 2) * cell + cell / 2;
    QColor cross = m_controller->accentColor();
    cross.setAlpha(140);
    // FlatCap so the thick stroke ends exactly at the box edges instead of
    // overhanging by half the pen width (which made the cross stick out of the loupe).
    p.setPen(QPen(cross, cell, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(cx, dst.top(), cx, dst.bottom());
    p.drawLine(dst.left(), cy, dst.right(), cy);

    // Mark the exact sampled pixel with a hollow square (dark halo + white core).
    const QRect centerCell(dst.left() + (viewX / 2) * cell,
                           dst.top()  + (viewY / 2) * cell, cell, cell);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 170), 3));
    p.drawRect(centerCell);
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(centerCell);

    // Loupe border.
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(dst.adjusted(0, 0, -1, -1));

    // Readout panel — attached directly under the loupe, centered text.
    const QRect panel(pos.x(), dst.bottom(), panelW, panelH);
    p.fillRect(panel, QColor(22, 22, 22, 214));

    int y = panel.top() + 3;
    // Line 1: coordinates (interface/magnifier/coordinates). Hidden → the swatch below
    // simply becomes the panel's first line.
    p.setFont(fMain);
    p.setPen(Qt::white);
    if (showCoords) {
        p.drawText(QRect(panel.left(), y, panel.width(), lineMain), Qt::AlignCenter, coordStr);
        y += lineMain;
    }

    // Line 2: the sampled color, once — every row below reads the same pixel, so a
    // per-row swatch would just repeat this square.
    const QRect sw(panel.left() + (panel.width() - kSwatchW) / 2,
                   y + (lineSw - kSwatch) / 2, kSwatchW, kSwatch);
    p.fillRect(sw, color);
    p.setPen(QPen(QColor(255, 255, 255, 140), 1));
    p.drawRect(sw);
    y += lineSw;

    // One row per enabled format: "LABEL:" right-aligned to a shared colon, value
    // left-aligned after it. The Shift-selected row gets an accent plate marking it
    // as what C copies.
    const int labelX = panel.left() + kPadX;
    const int valueX = labelX + labelW + kColonGap;
    for (int i = 0; i < rows.size(); ++i) {
        const QRect rowRect(panel.left() + 4, y, panel.width() - 8, lineRow);
        if (i == copyRow) {
            QColor hi = m_controller->accentColor();
            hi.setAlpha(150);
            p.setPen(Qt::NoPen);
            p.setBrush(hi);
            p.drawRoundedRect(rowRect, 3, 3);
            p.setBrush(Qt::NoBrush);
        }
        p.setPen(i == copyRow ? QColor(Qt::white) : QColor(210, 210, 210));
        p.drawText(QRect(labelX, y, labelW, lineRow), Qt::AlignVCenter | Qt::AlignRight,
                   rows.at(i).first + QLatin1Char(':'));
        p.setPen(Qt::white);
        p.drawText(QRect(valueX, y, valueW + 4, lineRow), Qt::AlignVCenter | Qt::AlignLeft,
                   rows.at(i).second);
        y += lineRow;
    }

    // Trailing lines: key hints (smaller, the action key bolded) — Usage-tips only.
    for (const HintLines& h : hints) {
        drawHintLines(p, QRect(panel.left(), y, panel.width(), lineHint * h.size()),
                      fHint, h, lineHint);
        y += lineHint * h.size();
    }
}

void SnipScreenView::drawHistoryStrip(QPainter& p)
{
    if (!m_controller->historyStripOpen())
        return;
    const SnipOverlay::StripLayout lay = m_controller->historyStripLayout();
    const QRect panelLocal = toLocalRect(lay.panel);
    if (!lay.panel.isValid() || !panelLocal.intersects(rect()))
        return;                                    // strip lives on another screen

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // Rounded dark panel with a subtle border.
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    p.setBrush(QColor(28, 28, 30, 235));
    p.drawRoundedRect(panelLocal, 8, 8);

    // Thumbnails: each scaled to fit inside its cell preserving aspect, centred; the
    // selected one gets an accent border.
    for (int i = 0; i < lay.thumbRects.size(); ++i) {
        const QRect cell = toLocalRect(lay.thumbRects.at(i));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 120));
        p.drawRoundedRect(cell, 4, 4);

        const QImage& thumb = lay.thumbs.at(i);
        if (!thumb.isNull()) {
            const QSize sz = thumb.size().scaled(cell.size(), Qt::KeepAspectRatio);
            const QRect dst(cell.center().x() - sz.width() / 2,
                            cell.center().y() - sz.height() / 2, sz.width(), sz.height());
            p.drawImage(dst, thumb);
        }
        if (i == lay.selectedSlot) {
            p.setPen(QPen(m_controller->accentColor(), 3));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(cell.adjusted(1, 1, -1, -1), 4, 4);
        }
    }

    // Footer hint under the thumbnails.
    QFont fHint = p.font();
    fHint.setPointSizeF(p.font().pointSizeF() + 1.0);
    p.setFont(fHint);
    p.setPen(QColor(230, 230, 230));
    const QRect footer(panelLocal.left(), panelLocal.bottom() - 20,
                       panelLocal.width(), 20);
    p.drawText(footer, Qt::AlignCenter, TR("snip.history_footer"));

    p.restore();
}

void SnipScreenView::drawKeyTips(QPainter& p)
{
    if (!m_controller->keyTipsEnabled())
        return;
    if (m_s.screen != QGuiApplication::primaryScreen())   // one panel, on the primary screen
        return;
    const QVector<SnipOverlay::TipRow> rows = m_controller->tipRows();
    if (rows.isEmpty())
        return;

    // Two fonts: bold for the key chips, regular for the descriptions.
    QFont fChip = p.font();  fChip.setBold(true);   fChip.setPointSizeF(p.font().pointSizeF() + 0.5);
    QFont fText = p.font();  fText.setBold(false);  fText.setPointSizeF(p.font().pointSizeF() + 1.0);
    const QFontMetrics fmChip(fChip), fmText(fText);

    // Layout metrics.
    constexpr int padL = 12, padR = 14, padT = 10, padB = 10;   // panel insets
    constexpr int chipPadX = 7, chipPadY = 3, chipGap = 5;      // inside/between chips
    constexpr int colGap = 16, rowGap = 8;                      // keys->label, between rows
    const int chipH = fmChip.height() + 2 * chipPadY;
    const int rowH  = std::max(chipH, fmText.height()) + rowGap;

    auto chipWidth = [&](const QString& s) {
        return std::max(chipH, fmChip.horizontalAdvance(s) + 2 * chipPadX);   // keep single keys squarish
    };
    // A row may carry a separator ("/" , "or") drawn as plain text between its chips, so
    // two keys read as two actions instead of one combination.
    auto sepWidth = [&](const SnipOverlay::TipRow& r) {
        return r.sep.isEmpty() ? 0 : fmText.horizontalAdvance(r.sep) + chipGap;
    };
    auto rowChipsWidth = [&](const SnipOverlay::TipRow& r) {
        int w = 0;
        for (int i = 0; i < r.keys.size(); ++i)
            w += chipWidth(r.keys.at(i)) + (i ? chipGap + sepWidth(r) : 0);
        return w;
    };

    int keysColW = 0, labelColW = 0;
    for (const SnipOverlay::TipRow& r : rows) {
        keysColW  = std::max(keysColW, rowChipsWidth(r));
        labelColW = std::max(labelColW, fmText.horizontalAdvance(r.label));
    }
    const int panelW = padL + keysColW + colGap + labelColW + padR;
    const int panelH = padT + rows.size() * rowH - rowGap + padB;

    // Choose a corner clear of everything currently on this screen (all coords local to
    // it). Recomputed every paint — the panel leaves as soon as something arrives over it
    // and returns to the bottom-left as soon as that clears.
    QVector<QRect> obstacles;
    if (m_controller->selectionValid())
        obstacles << toLocalRect(m_controller->selection());
    if (m_controller->hasSuggestion())              // auto-detected window/element candidate
        obstacles << toLocalRect(m_controller->suggestion());
    if (m_haveCursor) {
        constexpr int kCursorPad = 24;              // matters when the loupe is hidden
        obstacles << QRect(m_cursor.x() - kCursorPad, m_cursor.y() - kCursorPad,
                           2 * kCursorPad, 2 * kCursorPad);
    }
    obstacles << m_magRect;                         // loupe + colour panel (invalid when hidden)
    const HintLayout hl = hintLayout();             // the readout row above the region
    if (hl.valid) {
        obstacles << hl.box[0] << hl.box[1] << hl.cornerBox << hl.refreshBox << hl.barcodeBox;
    }
    // Every visible child widget — the PaintBar, its sub-bar, the OCR bar, an open in-place
    // number editor. They are children, so they paint OVER this panel: overlapping one would
    // clip the tips rather than merely crowd them.
    for (const QObject* o : children()) {
        const auto* w = qobject_cast<const QWidget*>(o);
        if (w && w->isVisible())
            obstacles << w->geometry();
    }
    const QRect panel = TipsPlacement::place(rect(), QSize(panelW, panelH), obstacles,
                                             /*margin=*/16);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // Panel background (matches the history strip's dark rounded look).
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    p.setBrush(QColor(28, 28, 30, 235));
    p.drawRoundedRect(panel, 8, 8);

    for (int i = 0; i < rows.size(); ++i) {
        const SnipOverlay::TipRow& r = rows.at(i);
        const int rowTop = panel.top() + padT + i * rowH;
        const int rowMid = rowTop + std::max(chipH, fmText.height()) / 2;

        // Key chips.
        int x = panel.left() + padL;
        for (int k = 0; k < r.keys.size(); ++k) {
            const QString& key = r.keys.at(k);
            const int w = chipWidth(key);
            const QRect chip(x, rowMid - chipH / 2, w, chipH);
            p.setFont(fChip);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(58, 58, 62));
            p.drawRoundedRect(chip, 4, 4);
            p.setPen(QPen(QColor(255, 255, 255, 45), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(chip.adjusted(0, 0, -1, -1), 4, 4);
            p.setPen(Qt::white);
            p.drawText(chip, Qt::AlignCenter, key);
            x += w + chipGap;

            if (!r.sep.isEmpty() && k + 1 < r.keys.size()) {
                const int sw = fmText.horizontalAdvance(r.sep);
                p.setFont(fText);
                p.setPen(QColor(170, 170, 170));
                p.drawText(QRect(x, rowMid - chipH / 2, sw, chipH), Qt::AlignCenter, r.sep);
                x += sw + chipGap;
            }
        }

        // Description.
        p.setFont(fText);
        p.setPen(QColor(225, 225, 225));
        const QRect lr(panel.left() + padL + keysColW + colGap, rowTop,
                       labelColW + 4, std::max(chipH, fmText.height()));
        p.drawText(lr, Qt::AlignVCenter | Qt::AlignLeft, r.label);
    }

    p.restore();
}

namespace {
// Substitute "{name}" placeholders in an i18n template, recording where each value
// landed in the finished string so a click can hit-test one number. Fills the
// leftmost placeholder first, so a locale may reorder them; a placeholder missing
// from the locale keeps its start at -1.
QString fillHint(const QString& templ, const QStringList& names,
                 const QStringList& values, int* starts)
{
    QString s = templ;
    for (int i = 0; i < names.size(); ++i)
        starts[i] = -1;
    for (int done = 0; done < names.size(); ++done) {
        int pick = -1, at = -1;
        for (int i = 0; i < names.size(); ++i) {
            if (starts[i] >= 0)
                continue;
            const int idx = s.indexOf(QLatin1Char('{') + names[i] + QLatin1Char('}'));
            if (idx >= 0 && (at < 0 || idx < at)) { at = idx; pick = i; }
        }
        if (pick < 0)
            break;
        s.replace(at, names[pick].size() + 2, values[pick]);
        starts[pick] = at;
    }
    return s;
}
}   // namespace

SnipScreenView::HintLayout SnipScreenView::hintLayout() const
{
    HintLayout hl;
    if (!m_controller->selectionValid())
        return hl;

    // Position of the selection's top-left, in capture px on the virtual desktop —
    // the same space the Custom Snip dialog's X/Y use (so they can go negative).
    const QPoint origin = m_controller->selection().topLeft();
    const QSize outPx = m_controller->outputPixelSize();
    hl.value[0] = origin.x();      hl.value[1] = origin.y();
    hl.value[2] = outPx.width();   hl.value[3] = outPx.height();

    int start[4];
    hl.label[0] = fillHint(TR("snip.hint_pos"), {QStringLiteral("x"), QStringLiteral("y")},
                           {QString::number(hl.value[0]), QString::number(hl.value[1])}, start);
    hl.label[1] = fillHint(TR("snip.hint_size"), {QStringLiteral("w"), QStringLiteral("h")},
                           {QString::number(hl.value[2]), QString::number(hl.value[3])}, start + 2);

    // --- Size hint look (tweak these to taste) -------------------------------
    hl.font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    hl.font.setPointSizeF(font().pointSizeF() + 5.0);      // font size: +N points
    hl.font.setBold(true);
    const int padX = 6;                                    // left/right space around text
    const int padY = 7;                                    // top/bottom space around text
    const int gap  = 4;                                    // space between the two boxes
    // -------------------------------------------------------------------------
    const QFontMetrics fm(hl.font);
    const QRect lsel = toLocalRect(m_controller->selection());
    const int boxH = fm.height() + 2 * padY;
    const int boxY = std::max(0, lsel.top() - boxH - 2);
    hl.box[0] = QRect(lsel.left(), boxY, fm.horizontalAdvance(hl.label[0]) + 2 * padX, boxH);
    hl.box[1] = QRect(hl.box[0].right() + 1 + gap, boxY,
                      fm.horizontalAdvance(hl.label[1]) + 2 * padX, boxH);

    // Each number's own rect inside its (centre-aligned) box — the click target.
    for (int i = 0; i < 4; ++i) {
        if (start[i] < 0)
            continue;
        const QRect& box = hl.box[i / 2];
        const QString& text = hl.label[i / 2];
        const int textLeft = box.left() + (box.width() - fm.horizontalAdvance(text)) / 2;
        hl.num[i] = QRect(textLeft + fm.horizontalAdvance(text.left(start[i])), box.top(),
                          fm.horizontalAdvance(QString::number(hl.value[i])), boxH);
    }

    // Everything past the two readout boxes is laid out left to right, each on its own
    // plate (deliberately not grouped with the boxes or each other), and all under the
    // same rule as the click-to-edit numbers: only while the readout is interactive
    // (committed region, no annotation tool) — with a tool active a click up there
    // belongs to the annotation.
    const int inset = std::max(2, boxH / 6);
    int rowRight = hl.box[1].right();               // right edge of the row so far

    // --- corner-shape chip: immediately right of the `w x h` box. The bending level is
    // shown only in rounded mode — with right angles there is no bend to report, so the
    // plate shrinks to just the icon. (The level itself is still remembered; toggling
    // back brings it and its number straight in.)
    if (m_controller->cornerOptionEnabled() && m_controller->isReadOnly()) {
        const int glyph = boxH - 2 * inset;
        int w = inset + glyph + inset;                        // square plate: icon only
        if (m_controller->cornerRounded()) {
            hl.cornerText = QString::number(m_controller->cornerRadiusLogical());
            w = inset + glyph + 6 + fm.horizontalAdvance(hl.cornerText) + padX;
        }
        hl.cornerBox = QRect(rowRight + 1 + gap, boxY, w, boxH);
        hl.cornerGlyph = QRect(hl.cornerBox.left() + inset, boxY + inset, glyph, glyph);
        rowRight = hl.cornerBox.right();
    }

    // --- refresh button: a square plate of its own, next along the row.
    if (m_controller->isReadOnly()) {
        hl.refreshBox = QRect(rowRight + 1 + gap, boxY, boxH, boxH);
        hl.refreshGlyph = hl.refreshBox.adjusted(inset, inset, -inset, -inset);
        rowRight = hl.refreshBox.right();
    }

    // --- code chip: glyph + the payload, elided to what's left of the selection's width.
    // The whole readout row is budgeted against the selection, so the link never runs
    // wider than the region it was decoded from. Below kBarcodeMinTextW the chip is allowed
    // to overhang the selection instead of shrinking to an unreadable stub — a narrow
    // region would otherwise show a lone ellipsis.
    const QString payload = m_controller->barcodePayload();
    if (!payload.isEmpty()) {
        constexpr int kBarcodeMinTextW = 90;                  // floor before we allow overhang
        // The glyph is a 20x20 module bitmap, so snap its size to a multiple of 20:
        // an exact integer px per module is what keeps the finder patterns crisp
        // instead of aliasing into a grey smudge. Largest multiple that still fits
        // the box, never below 20 (= 1 px per module).
        const int glyph = std::max(20, ((boxH - 4) / 20) * 20);
        const int chrome = 2 * padX + glyph + 6;         // padding + glyph + glyph-to-text gap
        const int used = (rowRight + 1 + gap) - lsel.left();
        const int budget = std::max(kBarcodeMinTextW, lsel.width() - used - chrome);

        const QString shown = m_controller->barcodeCopied()
            ? TR("snip.barcode.copied")
            : BarcodeHint::elide(payload, budget, [&fm](const QString& s) {
                  return fm.horizontalAdvance(s);
              });

        if (!shown.isEmpty()) {
            hl.barcodeText = shown;
            const int textW = fm.horizontalAdvance(shown);
            hl.barcodeBox = QRect(rowRight + 1 + gap, boxY, chrome + textW, boxH);
            // Keep the chip on-screen when the selection sits near the right edge —
            // but ONLY on a view the selection actually touches. hintLayout() runs for
            // every monitor's view; on one the selection isn't on, the row lands far
            // outside the widget and clips away harmlessly (that is why box[0]/box[1]
            // need no guard). Clamping unconditionally instead dragged the chip back
            // in and pinned a phantom copy to the wrong monitor's right edge.
            if (lsel.intersects(rect()) && hl.barcodeBox.right() >= width())
                hl.barcodeBox.moveRight(width() - 1);
            hl.barcodeGlyph = QRect(hl.barcodeBox.left() + padX, boxY + (boxH - glyph) / 2, glyph, glyph);
        }
    }

    hl.valid = true;
    return hl;
}

// The QR icon, transcribed module-for-module off the reference artwork: a 20x20
// grid whose three 7x7 finder patterns (2-module ring, 1-module gap, 1-module core)
// sit top-left, top-right and bottom-left. Kept as a bitmap rather than an asset
// file so it scales to any hint font size and always takes the colour of the text
// beside it.
//
// Module edges are computed by rounding both sides of each cell off the same
// left/top + index*size expression, so neighbouring modules share an exact edge:
// at the ~1 px per module this is drawn at, independently rounded rects would
// leave seams or double-cover and turn the pattern to mush.
void SnipScreenView::drawBarcodeGlyph(QPainter& p, const QRect& r, const QColor& color)
{
    static constexpr int kGrid = 20;
    static const char* kRows[kGrid] = {
        "#######...##.#######",
        "#######...##.#######",
        "##...##.##...##...##",
        "##.#.##.##...##.#.##",
        "##...##...##.##...##",
        "#######..###.#######",
        "#######.####.#######",
        "........##..........",
        "####..#.##..####...#",
        "####.##.##..####..##",
        "##..####..##....##..",
        "#...####..##....##..",
        "........##..#..#..##",
        "#######.##..#.....##",
        "#######...###...####",
        "##...##.##...###....",
        "##.#.##.##...###....",
        "##...##...##....##..",
        "#######...###..#..##",
        "#######...###..#..##",
    };

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    const auto edgeX = [&](int i) { return r.left() + qRound(qreal(i) * r.width()  / kGrid); };
    const auto edgeY = [&](int i) { return r.top()  + qRound(qreal(i) * r.height() / kGrid); };

    for (int row = 0; row < kGrid; ++row) {
        const char* cells = kRows[row];
        const int y = edgeY(row), h = edgeY(row + 1) - y;
        // Coalesce each horizontal run of set modules into one rect — fewer, wider
        // fills survive rounding far better than one rect per module.
        for (int col = 0; col < kGrid; ) {
            if (cells[col] != '#') { ++col; continue; }
            int end = col;
            while (end < kGrid && cells[end] == '#')
                ++end;
            const int x = edgeX(col);
            p.drawRect(QRect(x, y, edgeX(end) - x, h));
            col = end;
        }
    }

    p.restore();
}

// The refresh icon: two half-ring arrows chasing each other counter-clockwise, the top
// one ending in a head that points straight down at 9 o'clock and the bottom one in a
// head that points straight up at 3 o'clock. Drawn rather than shipped as an asset, like
// the QR glyph beside it, so it scales with the hint font and always takes the colour of
// the row it sits in. `spinDeg` rotates the whole glyph counter-clockwise (see
// SnipOverlay::refreshSpin) — 0 is the upright resting pose.
void SnipScreenView::drawRefreshGlyph(QPainter& p, const QRect& r, const QColor& color,
                                      qreal spinDeg)
{
    const qreal side = std::min(r.width(), r.height());
    if (side < 6)
        return;
    const QPointF c = QRectF(r).center();
    const qreal rad      = side * 0.33;    // radius of the ring's centre-line
    const qreal thick    = side * 0.12;    // arc stroke width
    const qreal headHalf = side * 0.145;   // arrowhead half-width (base)
    const qreal headLen  = side * 0.21;    // arrowhead length along the tangent
    const qreal headAt[] = {180.0, 0.0};   // heads: 9 o'clock (down), 3 o'clock (up)
    // The notch between one arc's head and the next arc's tail. It must clear the head's
    // own tangential reach (headLen/rad ≈ 36°) or the triangle runs straight into the
    // other arc and the two arrows read as one closed ring.
    constexpr qreal kTailGap = 62.0;
    // Negative: the arc runs slightly *past* the head's base angle so no antialiased
    // sliver of background shows between the two. The overlap can't poke out — the
    // triangle is drawn after the arc and stays wider than the stroke for ~21° past its
    // base (half-widths: 0.145 vs 0.06 of `side`).
    constexpr qreal kHeadGap = -3.0;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF ring(c.x() - rad, c.y() - rad, 2 * rad, 2 * rad);
    // Qt arc angles are 1/16°, counter-clockwise, 0 = 3 o'clock.
    p.setPen(QPen(color, thick, Qt::SolidLine, Qt::FlatCap));
    p.setBrush(Qt::NoBrush);
    const qreal span = 180.0 - kTailGap - kHeadGap;
    for (qreal h : headAt)
        p.drawArc(ring, qRound((h + spinDeg - 180.0 + kTailGap) * 16), qRound(span * 16));

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    for (qreal h : headAt) {
        const qreal a = (h + spinDeg) * M_PI / 180.0;
        const QPointF at(c.x() + rad * std::cos(a), c.y() - rad * std::sin(a));
        const QPointF tan(-std::sin(a), -std::cos(a));   // counter-clockwise, screen y down
        const QPointF nor(tan.y(), -tan.x());
        QPolygonF tri;
        tri << at + tan * headLen << at + nor * headHalf << at - nor * headHalf;
        p.drawPolygon(tri);
    }
    p.restore();
}

int SnipScreenView::hintFieldAt(const QPoint& local) const
{
    // Editable only while the region is committed and adjustable (the same state that
    // shows the grips) — with a tool active a click there belongs to the annotation.
    if (!m_controller->isReadOnly())
        return -1;
    const HintLayout hl = hintLayout();
    if (!hl.valid)
        return -1;
    for (int i = 0; i < 4; ++i)
        if (hl.num[i].isValid() && hl.num[i].contains(local))
            return i;
    return -1;
}

bool SnipScreenView::barcodeHintAt(const QPoint& local) const
{
    if (!m_controller->isReadOnly() || m_controller->barcodePayload().isEmpty())
        return false;
    const HintLayout hl = hintLayout();
    return hl.valid && hl.barcodeBox.isValid() && hl.barcodeBox.contains(local);
}

void SnipScreenView::updateRefreshButton()
{
    const HintLayout hl = hintLayout();
    if (hl.valid && hl.refreshBox.isValid())
        update(hl.refreshBox);
}

bool SnipScreenView::refreshHintAt(const QPoint& local) const
{
    if (!m_controller->isReadOnly())
        return false;
    const HintLayout hl = hintLayout();
    return hl.valid && hl.refreshBox.isValid() && hl.refreshBox.contains(local);
}

bool SnipScreenView::cornerHintAt(const QPoint& local) const
{
    if (!m_controller->cornerOptionEnabled() || !m_controller->isReadOnly())
        return false;
    const HintLayout hl = hintLayout();
    return hl.valid && hl.cornerBox.isValid() && hl.cornerBox.contains(local);
}

qreal SnipScreenView::cornerRadiusLocal() const
{
    const int cap = m_controller->effectiveCornerRadius();
    const qreal s = scaleX();
    return (cap <= 0 || s <= 0.0) ? 0.0 : cap / s;
}

// The corner icon: a stroke that comes up the left edge, turns at the top-left — sharply
// for right angles, through a quarter arc for rounded — and runs off to the right. Drawn
// rather than shipped as an asset so it takes the hint font's size and colour, like the
// QR and refresh glyphs beside it.
void SnipScreenView::drawCornerGlyph(QPainter& p, const QRect& r, const QColor& color,
                                     bool rounded)
{
    if (r.width() < 4 || r.height() < 4)
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, std::max(2.0, r.height() / 8.0));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // Inset by the pen so the stroke stays inside `r` instead of half-hanging out of it.
    const qreal in = pen.widthF() / 2.0;
    const QRectF b = QRectF(r).adjusted(in, in, -in, -in);
    const qreal arc = rounded ? b.width() * 0.55 : 0.0;

    QPainterPath path;
    path.moveTo(b.left(), b.bottom());                    // up the left edge
    path.lineTo(b.left(), b.top() + arc);
    if (rounded)                                          // quarter arc through the corner
        path.arcTo(QRectF(b.left(), b.top(), 2 * arc, 2 * arc), 180.0, -90.0);
    else
        path.lineTo(b.left(), b.top());
    path.lineTo(b.right(), b.top());                      // across the top edge
    p.drawPath(path);
    p.restore();
}

void SnipScreenView::openHintEdit(int field)
{
    const HintLayout hl = hintLayout();
    if (!hl.valid || field < 0 || field > 3 || !hl.num[field].isValid())
        return;
    closeHintEdit();
    m_hintField = field;
    m_hintEdit = new QLineEdit(this);
    m_hintEdit->setFont(hl.font);
    m_hintEdit->setAlignment(Qt::AlignCenter);
    m_hintEdit->setFrame(false);
    m_hintEdit->setStyleSheet(QStringLiteral("background:#ffffff;color:#000000;"));
    // x/y are virtual-desktop coords (may be negative); w/h are at least 1 px.
    m_hintEdit->setValidator(new QIntValidator(field < 2 ? -100000 : 1, 100000, m_hintEdit));
    m_hintEdit->setText(QString::number(hl.value[field]));
    m_hintEdit->selectAll();
    // A little wider than the painted number so a longer value still fits while typing.
    QRect r = hl.num[field].adjusted(-4, 0, 4, 0);
    r.setWidth(std::max(r.width(), 48));
    m_hintEdit->setGeometry(r);
    m_hintEdit->installEventFilter(this);
    m_hintEdit->show();
    m_hintEdit->setFocus(Qt::MouseFocusReason);
}

void SnipScreenView::commitHintEdit()
{
    if (!m_hintEdit)
        return;
    bool ok = false;
    const int v = m_hintEdit->text().toInt(&ok);
    const int field = m_hintField;
    closeHintEdit();
    if (ok)
        m_controller->setSelectionField(field, v);
}

void SnipScreenView::closeHintEdit()
{
    if (!m_hintEdit)
        return;
    m_hintEdit->removeEventFilter(this);
    m_hintEdit->deleteLater();
    m_hintEdit = nullptr;
    m_hintField = -1;
    setFocus();                       // hand the snip keys back to the overlay
    update();
}

bool SnipScreenView::eventFilter(QObject* o, QEvent* ev)
{
    if (m_hintEdit && o == m_hintEdit) {
        if (ev->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent*>(ev)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                commitHintEdit();
                return true;
            }
            // Esc cancels this edit only; the snip stays (a second Esc aborts it).
            if (key == Qt::Key_Escape) {
                closeHintEdit();
                return true;
            }
            if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
                const int next = (m_hintField + (key == Qt::Key_Tab ? 1 : 3)) % 4;
                commitHintEdit();
                openHintEdit(next);
                return true;
            }
        }
        // Clicking anywhere else takes the focus away: confirm what was typed.
        if (ev->type() == QEvent::FocusOut)
            commitHintEdit();
    }
    return QWidget::eventFilter(o, ev);
}

void SnipScreenView::applyCursor()
{
    // Move mode: the region is a vertical-only drag handle and nothing else on screen is
    // interactive, so this wins outright.
    if (m_controller->scrollMoveActive()) {
        setCursor(toLocalRect(m_controller->selection()).contains(m_cursor)
                      ? Qt::SizeVerCursor : Qt::ArrowCursor);
        return;
    }
    // A number in the position/size readout is click-to-edit.
    if (hintFieldAt(m_cursor) >= 0) {
        setCursor(Qt::IBeamCursor);
        return;
    }
    // The code chip is a link: click to copy, Ctrl+click to open.
    // The refresh button is a button: click to refresh, hold to keep refreshing.
    // The corner-shape chip is a button too: click to toggle, scroll to bend.
    if (barcodeHintAt(m_cursor) || refreshHintAt(m_cursor) || cornerHintAt(m_cursor)) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    // Resize/move/cross cursor depending on what the cursor is over (read-only mode). When
    // the fullscreen crosshair is active, blank the system cursor so it replaces it.
    if (m_controller->crossLines() && !m_controller->isEditing())
        setCursor(Qt::BlankCursor);
    else
        setCursor(m_controller->cursorAt(toCapture(m_cursor)));
}

void SnipScreenView::mousePressEvent(QMouseEvent* e)
{
    m_cursor = e->position().toPoint();
    m_haveCursor = true;
    if (e->button() == Qt::LeftButton) {
        // A click on a number in the position/size readout edits it in place.
        const int field = hintFieldAt(m_cursor);
        if (field >= 0) {
            openHintEdit(field);
            return;
        }
        // The code chip: Ctrl+click opens the link (and closes the snip), a plain
        // click copies the payload and leaves the overlay up.
        if (barcodeHintAt(m_cursor)) {
            m_barcodePressed = true;                // swallow the matching release
            if (e->modifiers() & Qt::ControlModifier)
                m_controller->openBarcodePayload();
            else
                m_controller->copyBarcodePayload();
            return;
        }
        // The refresh button: refresh once now, and keep refreshing if the press is held.
        if (refreshHintAt(m_cursor)) {
            m_refreshPressed = true;            // swallow the matching release
            m_controller->beginRefresh(SnipOverlay::RefreshSource::Button);
            return;
        }
        // The corner-shape chip: toggle right angles <-> rounded, keeping the level.
        if (cornerHintAt(m_cursor)) {
            m_cornerPressed = true;             // swallow the matching release
            m_controller->toggleCornerMode();
            return;
        }
        // A click on the open history strip picks a thumbnail instead of starting a drag.
        if (m_controller->handleStripClick(toCapture(e->position().toPoint())))
            return;
        m_controller->onPress(toCapture(e->position().toPoint()));
    }
    else if (e->button() == Qt::RightButton)
        m_controller->onAbort();
    else if (e->button() == Qt::MiddleButton)
        m_controller->onMiddleClick();         // configurable middle-click action
    applyCursor();                             // the click may have changed what's under it
}

void SnipScreenView::mouseMoveEvent(QMouseEvent* e)
{
    m_cursor = e->position().toPoint();
    m_haveCursor = true;
    if (!m_s.shot.isNull())                           // feed the C-copy color action
        m_controller->reportHoverColor(ColorPicker::sampleAt(m_s.shot, cursorPhysLocal()));
    // Hovering the code chip reveals the FULL payload, which the chip itself elides.
    if (barcodeHintAt(m_cursor))
        QToolTip::showText(e->globalPosition().toPoint(), m_controller->barcodePayload(), this);
    else if (refreshHintAt(m_cursor))
        QToolTip::showText(e->globalPosition().toPoint(), TR("snip.refresh.tooltip"), this);
    else
        QToolTip::hideText();
    applyCursor();
    m_controller->onMove(toCapture(m_cursor));        // controller repaints all views
}

void SnipScreenView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_barcodePressed) {
        m_barcodePressed = false;                   // the chip consumed this click entirely
        applyCursor();
        return;
    }
    if (e->button() == Qt::LeftButton && m_cornerPressed) {
        m_cornerPressed = false;                    // the chip consumed this click entirely
        applyCursor();
        return;
    }
    if (e->button() == Qt::LeftButton && m_refreshPressed) {
        m_refreshPressed = false;                   // the button consumed this click entirely
        m_controller->endRefresh(SnipOverlay::RefreshSource::Button);
        applyCursor();
        return;
    }
    if (e->button() == Qt::LeftButton)
        m_controller->onRelease(toCapture(e->position().toPoint()));
    applyCursor();                             // release may enter/leave a badge renumber
}

void SnipScreenView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    // Toggling the corner shape back and forth is a normal thing to do, and Qt turns the
    // second click into a double-click rather than a press — which would otherwise run the
    // configurable double-click action (copy + close) instead of toggling. Treat it as the
    // plain click it is.
    if (cornerHintAt(e->position().toPoint())) {
        m_cornerPressed = true;                 // swallow the matching release
        m_controller->toggleCornerMode();
        return;
    }
    m_controller->onDoubleClick();
}

void SnipScreenView::wheelEvent(QWheelEvent* e)
{
    // A scrolling capture normally leaves these views click-through, so no wheel ever
    // reaches Qt; Move mode is the one time it does. Nothing below applies to a scrolling
    // session — the tools are off for its duration — so swallow it rather than let a
    // stray corner-radius or pen-width change ride in.
    if (m_controller->scrollMoveActive()) {
        e->accept();
        return;
    }
    // Over the corner-shape chip the wheel bends the corners. Checked first: it is a
    // deliberate, tiny hover target, so it can't shadow either handler below.
    if (cornerHintAt(e->position().toPoint())) {
        m_controller->stepCornerRadius(e->angleDelta().y() > 0 ? 1 : -1);
        e->accept();
        return;
    }
    // Hovering in Element mode: the wheel steps out to the parent element / back in
    // to the child. Checked before the tool branch because no tool can be active
    // while we're still choosing the region.
    if (m_controller->elementHierarchyActive()) {
        m_controller->onElementLevelWheel(e->angleDelta().y() > 0 ? 1 : -1);
        e->accept();
        return;
    }
    // With a tool active, scrolling the canvas adjusts its line width / eraser size.
    if (m_controller->toolSelected()) {
        m_controller->onWheel(e->angleDelta().y() > 0 ? 1 : -1);
        e->accept();
        return;
    }
    QWidget::wheelEvent(e);
}

void SnipScreenView::keyPressEvent(QKeyEvent* e)
{
    // A lone Alt (the quick-mark modifier) is handled in nativeEvent — Windows reserves it
    // for menu activation and never turns the release into a QKeyEvent, so it never
    // reaches here.
    m_controller->onKey(e->key(), e->modifiers());
    if (m_haveCursor)
        applyCursor();                         // e.g. Enter ends a badge renumber
}

void SnipScreenView::keyReleaseEvent(QKeyEvent* e)
{
    // Only the refresh key cares about key-up (holding it refreshes continuously).
    // Auto-repeat manufactures release/press pairs, so those are ignored.
    if (!e->isAutoRepeat())
        m_controller->onKeyRelease(e->key());
    QWidget::keyReleaseEvent(e);
}

#ifdef Q_OS_WIN
bool SnipScreenView::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == "windows_generic_MSG") {
        const MSG* msg = static_cast<const MSG*>(message);
        // A solitary Alt arrives as WM_SYSKEYUP(VK_MENU) and is swallowed by Windows'
        // menu handling before Qt can raise a keyReleaseEvent — catch the raw release
        // here so letting go of the quick-mark modifier drops its preview. Not consumed,
        // so Qt's state stays correct. (The aux-line toggle used to hang off this; it is
        // a plain `G` in SnipOverlay::onKey now.)
        if (msg->message == WM_SYSKEYUP && msg->wParam == VK_MENU)
            m_controller->onAltReleased();
    }
    return QWidget::nativeEvent(eventType, message, result);
}
#endif

void SnipScreenView::leaveEvent(QEvent*)
{
    m_haveCursor = false;
    update();
}
