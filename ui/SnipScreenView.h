#pragma once

#include <QWidget>
#include <QFont>
#include "core/ScreenComposite.h"   // ScreenShot

class SnipOverlay;
class QScreen;
class QLineEdit;

// One frameless full-screen overlay window for a single monitor. Renders its
// frozen screen + dim + the slice of the (global) selection on this monitor +
// magnifier, and forwards input to the SnipOverlay controller in GLOBAL logical
// coords. See claude-plan/Multi-Monitor-Capture-Plan.md.
class SnipScreenView : public QWidget {
    Q_OBJECT
public:
    SnipScreenView(SnipOverlay* controller, const ScreenShot& shot, QWidget* parent = nullptr);

    void showOnScreen();                 // pin to its monitor + full-screen
    // Force this window to the foreground + focus it. A global hotkey doesn't transfer
    // foreground to our tray app, so plain activateWindow() is blocked by Windows'
    // foreground lock and the overlay stays inactive (mask/keys don't respond until a
    // mouse event). This bypasses the lock so F1 enters snipping immediately. No-op off Win.
    void forceForeground();
    // Ask Windows to leave this overlay OUT of whatever any screen capture reads back —
    // including our own re-grab — while it stays fully visible on screen. That is what
    // lets "refresh screenshot" re-freeze the desktop without hiding (and visibly
    // blinking) the mask. Returns false when the OS is too old for it (pre-Win10 2004),
    // which is the caller's cue to fall back to hiding the overlays for the grab.
    bool setExcludedFromCapture(bool on);
    // Recording mode: drop the frozen screenshot, the dim and the bright slice so the
    // LIVE desktop shows through this (translucent) window, leaving only the region
    // border and the annotations on screen. The window stays excluded from capture, so
    // the recorder sees the real desktop underneath, not our chrome.
    void setRecordingLive(bool on);
    bool recordingLive() const { return m_recordingLive; }
    // Scrolling capture result: the stitched image, drawn scaled-to-fit inside the
    // region once the session ends. It is far taller than the region, so this is
    // the only way to see what Copy/Save/Pin are about to act on. Null clears it.
    void setScrollPreview(const QImage& img);
    // Let mouse input fall through to whatever is beneath (WS_EX_TRANSPARENT), so the
    // app being recorded stays usable. Turned back off while an annotation tool is
    // armed, which is when this window needs the clicks itself.
    void setClickThrough(bool on);
    // Seed the hover cursor from a global point (if it lands on this monitor) so the
    // magnifier/loupe shows immediately on F1 without waiting for the first mouse move.
    void seedCursor(const QPoint& global);
    QScreen* targetScreen() const { return m_s.screen; }

    // --- coordinate bridges: this view's local (logical) coords <-> the shared
    // capture-pixel space. scale = captured shot px per logical px on this monitor.
    // Also used by SnipOverlay to host the in-place text editor (local widget coords).
    qreal scaleX() const { return m_s.geo.width()  > 0 ? m_s.captureGeo.width()  / qreal(m_s.geo.width())  : 1.0; }
    qreal scaleY() const { return m_s.geo.height() > 0 ? m_s.captureGeo.height() / qreal(m_s.geo.height()) : 1.0; }
    QPoint  toCapture(const QPointF& local) const;      // input point -> capture px
    QRect   toCaptureRect(const QRect& local) const;    // local rect -> capture rect
    QPointF toLocalF(const QPoint& cap) const;          // capture point -> local (widget placement)
    QRect   toLocalRect(const QRect& cap) const;        // capture rect -> local rect (selection chrome)
    QRectF  toLocalRectF(const QRectF& cap) const;      // capture rectF -> local (captured cursor)

    // Repaint just the readout row's refresh button. Drives the icon's spin without
    // repainting the whole frozen desktop for every animation frame.
    void updateRefreshButton();

    // Swap the frozen background pixels (keeps the view pinned to its real monitor).
    // Used by the `,`/`.` history replay to show a past desktop for a re-snip.
    void setBackground(const QImage& shot, qreal dpr);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif
    void leaveEvent(QEvent*) override;
    // Keep Tab as a snip key (cycle detect mode) instead of focus traversal.
    bool focusNextPrevChild(bool) override { return false; }
    // Watches the in-place hint editor for Enter/Esc/Tab and focus loss.
    bool eventFilter(QObject*, QEvent*) override;

private:
    // (Re-)pick the system cursor for whatever is under m_cursor. Called on mouse move,
    // but also after press/release/keys: state that changes the cursor (stamping a number
    // badge, entering/leaving a badge renumber) can happen with the mouse held still, and
    // a move-only refresh would leave a stale — possibly blanked — cursor on screen.
    void applyCursor();
    void paintAnnotations(QPainter& p, const QRect& clipLocal);
    void drawMagnifier(QPainter& p, const QPoint& cursorLocal);
    // The exact physical pixel under the OS cursor, local to this monitor's frozen shot.
    // Reads the real cursor (physical px) instead of quantizing m_cursor*dpr to the logical
    // grid — this is what lets the loupe/readout step one physical pixel under W/A/S/D.
    QPoint cursorPhysLocal() const;
    // The in-snip history strip (thumbnail row of recent snips), drawn on the view
    // whose screen holds it. No-op when the strip is closed / off this screen.
    void drawHistoryStrip(QPainter& p);
    // The key-binding tips panel (snip/display/keyTips): a hint list
    // pinned to a corner. Drawn only on the primary screen's view; auto-flips corner to
    // keep clear of the selection, the detection candidate, the cursor, the magnifier
    // and the toolbars. No-op when the setting is off.
    void drawKeyTips(QPainter& p);
    // Two concentric rings at the cursor previewing the circle eraser's reach.
    void drawEraserCursor(QPainter& p, const QPoint& cursorLocal);
    // A ring filled with translucent gray, previewing the mosaic/blur brush's reach.
    void drawMosaicCursor(QPainter& p, const QPoint& cursorLocal);
    // A filled dot the size of the pen stroke, framed by a small crosshair, so the
    // pen cursor previews exactly how thick/where the next stroke will land.
    void drawPenCursor(QPainter& p, const QPoint& cursorLocal);
    // The active tool's drawn-in-paint cursor (pen dot, eraser/redact rings, number
    // badge preview), for the tools whose system cursor `cursorAt()` blanks. Called from
    // BOTH paint paths — the recording one draws no chrome, but blanking the system
    // cursor is the controller's call and applies there too, so skipping this would
    // leave those tools with no visible pointer at all.
    void drawToolCursor(QPainter& p);

    // --- editable position/size readout --------------------------------------
    // Geometry of the two readout boxes above the selection plus the four number
    // sub-rects inside them (all in this view's local coords). Shared by paintEvent,
    // click hit-testing and the in-place editor so they can't drift apart.
    struct HintLayout {
        bool valid = false;
        QFont font;
        QRect box[2];                    // "( x , y )" box, "w x h" box
        QString label[2];                // the translated text drawn in each box
        QRect num[4];                    // the x, y, w and h numbers
        int value[4] = {0, 0, 0, 0};
        // code hint — only when the region holds a decodable code. `barcodeBox` is the
        // whole clickable chip (glyph + text); `barcodeGlyph` is the square the QR icon
        // is painted into; `barcodeText` is the elided payload actually drawn.
        QRect barcodeBox;                     // invalid when there is no QR code
        QRect barcodeGlyph;
        QString barcodeText;
        // Refresh button: its own square plate right of the `w x h` box (deliberately not
        // grouped with it). Click = refresh once, press-and-hold = refresh continuously.
        QRect refreshBox;
        QRect refreshGlyph;                   // the circular-arrows icon inside the plate
        // Corner-shape chip: another plate of its own (invalid unless the Preferences
        // option is on). Click toggles right-angle/rounded, wheel changes the level.
        QRect cornerBox;                      // the whole clickable plate (glyph + number)
        QRect cornerGlyph;                    // the square the corner icon is painted into
        QString cornerText;                   // the bending level, as drawn
    };
    HintLayout hintLayout() const;
    // Paint the QR icon (three finder squares + a few data modules) into `r`.
    static void drawBarcodeGlyph(QPainter& p, const QRect& r, const QColor& color);
    // Paint the refresh icon (two half-circle arrows chasing each other) into `r`,
    // rotated `spinDeg` counter-clockwise (non-zero only while a continuous refresh runs).
    static void drawRefreshGlyph(QPainter& p, const QRect& r, const QColor& color,
                                 qreal spinDeg = 0.0);
    // Paint the corner-shape icon into `r`: an L running up the left edge and across the
    // top, its join either a sharp right angle or a quarter arc. Mirrors what the region's
    // top-left corner will look like, which is what makes the chip self-explanatory.
    static void drawCornerGlyph(QPainter& p, const QRect& r, const QColor& color, bool rounded);
    // The current corner radius in THIS view's local logical px (0 = square corners).
    // The controller keeps it in capture px; every view converts with its own scale so
    // the preview matches the export on a mixed-DPI desktop.
    qreal cornerRadiusLocal() const;
    int  hintFieldAt(const QPoint& local) const;   // 0=x 1=y 2=w 3=h, or -1
    // Is `local` over the clickable code chip? Like the number fields, only while the
    // region is committed and no tool is active — with a tool selected a click up
    // there belongs to the annotation.
    bool barcodeHintAt(const QPoint& local) const;
    // Is `local` over the refresh button? Same gate as the number fields.
    bool refreshHintAt(const QPoint& local) const;
    // Is `local` over the corner-shape chip? Same gate as the number fields.
    bool cornerHintAt(const QPoint& local) const;
    void openHintEdit(int field);                  // white in-place input over the number
    void commitHintEdit();                         // apply the typed value + close
    void closeHintEdit();                          // close, leaving the value untouched

    SnipOverlay* m_controller = nullptr;
    ScreenShot m_s;                      // this monitor's geo/shot/dpr
    QPoint m_cursor;                     // cursor in this view's local coords
    bool m_haveCursor = false;
    // What the magnifier covered this paint (invalid when it wasn't drawn). Set by
    // drawMagnifier, read by drawKeyTips, which paintEvent runs afterwards; cleared at
    // the top of every paintEvent so a hidden loupe can't leave a stale rect behind.
    QRect m_magRect;
    QLineEdit* m_hintEdit = nullptr;     // open in-place number input, else null
    int m_hintField = -1;                // which of x/y/w/h it edits
    // The press landed on the code chip, so the matching release must be swallowed:
    // it isn't aimed at the region, and letting it reach onRelease would re-commit
    // the selection and trigger a needless re-decode on every chip click.
    bool m_barcodePressed = false;
    // Same swallow-the-release contract for the refresh button, which additionally uses
    // the press/release pair to drive the hold-to-refresh-continuously gesture.
    bool m_refreshPressed = false;
    // Same swallow-the-release contract for the corner-shape chip.
    bool m_cornerPressed = false;
    // Recording mode: paint nothing but the region border + annotations (see
    // setRecordingLive), so the live desktop shows through.
    bool m_recordingLive = false;
    QImage m_scrollPreview;                // scrolling-capture result, letterboxed in the region
};
