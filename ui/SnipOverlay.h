#pragma once

#include <QObject>
#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>
#include <QImage>
#include <functional>
#include <memory>

#include "core/ScreenComposite.h"          // ScreenShot
#include "core/ScreenLayout.h"             // ScreenLayout (mixed-DPI capture space)
#include "core/ColorPicker.h"              // ColorPicker::Format
#include "core/annotation/Annotation.h"    // annotation::ToolId, Style
#include "core/codec/PdfEncoder.h"         // PdfEncoder::PageMode (.pdf page sizing)
#include "ui/OcrTextLayer.h"               // OCR selectable-text layer (shared)
#include "core/scroll/ScrollStitcher.h"    // held by value in a unique_ptr: needs the full type

namespace platform { class IElementDetect; class IScreenCapture; class IClipboard; class IKeyboardFilter; class IOcr; class IBarcodeDecode; class IRecorder; class IWheelWatch; }
class ActionRegistry;
class SelectionModel;
class SnipScreenView;
class AnnotationDocument;
class PaintBar;
class OcrBar;
class RecordBar;
class ScrollCaptureBar;
class ScrollStripWindow;
class TextBoxEditor;
class Settings;
class QPainter;
class QTimer;

// Multi-monitor snip controller. On start() it freezes every screen and shows a
// SnipScreenView per monitor, coordinating one global SelectionModel so a drag
// can span monitors. Finishing composites the selection across screens and
// copies/saves it, then self-deletes. Keeps the original `new SnipOverlay();
// overlay->start();` entry point. See claude-plan/Multi-Monitor-Capture-Plan.md.
class SnipOverlay : public QObject {
    Q_OBJECT
public:
    // Auto-region detection mode (Tab cycles through these).
    enum class DetectMode { Off, Window, Element };

    explicit SnipOverlay(platform::IElementDetect* detect = nullptr,
                         platform::IScreenCapture* capture = nullptr,
                         QObject* parent = nullptr);

    // Configured by the app from settings before start().
    void setDetectMode(DetectMode m) { m_detectMode = m; }
    void setSettings(Settings* s) { m_appSettings = s; }   // PaintBar colours + magnifier config
    void setActions(ActionRegistry* a) { m_actions = a; }  // source of the key-tips panel labels
    void setClipboard(platform::IClipboard* c) { m_clipboard = c; }  // Output→Clipboard extra formats
    void setKeyFilter(platform::IKeyboardFilter* f) { m_keyFilter = f; }  // snip/keys/* LL-hook policies
    void setOcr(platform::IOcr* o) { m_ocr = o; }                         // OCR backend (may be null)
    void setBarcodeDecode(platform::IBarcodeDecode* q) { m_barcode = q; }                // barcode backend (may be null)
    void setWindowTitle(const QString& t) { m_windowTitle = t; }     // foreground title for the %title% token
    // Toast sink: (title, body). Fired after a quick-save when output/quickSave/showNotification is on.
    void setNotifySink(std::function<void(QString, QString)> f) { m_notifySink = std::move(f); }
    // One-shot capture (the SnipCopy global hotkey): the first confirmed region goes
    // straight to the clipboard and the overlay closes — no annotate phase, no toolbar.
    void setOneShotCopy(bool on) { m_oneShotCopy = on; }
    // Colour-pick-only (the PickColor global hotkey): no region drag — the loupe follows
    // the cursor over the undimmed frozen screen, and a left-click (like C) copies the
    // pixel colour and closes. Shift still cycles the readout format.
    void setColorPickOnly(bool on) { m_colorPickOnly = on; }
    bool colorPickOnly() const { return m_colorPickOnly; }
    // Barcode-scan-only (the ScanBarcode global hotkey): a normal dimmed snip whose first
    // confirmed region is decoded — a found barcode/QR payload is copied and the overlay
    // closes; nothing found fires the notify toast and closes. No annotate phase.
    void setBarcodeScanOnly(bool on) { m_barcodeScanOnly = on; }
    // Quick-snip (the hold-modifier + drag gesture): the dragged region runs the chosen
    // extra action(s) — copy and/or pin — with no annotate phase and no toolbar, then the
    // overlay closes. At least one of copy/pin is always true here.
    void setQuickAction(bool copy, bool pin) { m_quickMode = true; m_quickCopy = copy; m_quickPin = pin; }
    // Screen-recording backend (may be null: the toolbar's record button is then
    // disabled). The overlay owns the whole recording session — it already resolves
    // output paths from Settings and owns the toast sink, exactly as quick-save does.
    void setRecorder(platform::IRecorder* r) { m_recorder = r; }
    // Mouse-wheel watch (may be null: the scrolling-capture button is then disabled).
    // A session makes the views click-through, so Qt never delivers the wheel — this
    // global hook is the only way to know the user scrolled.
    void setWheelWatch(platform::IWheelWatch* w) { m_wheelWatch = w; }
    // Pre-set the selection (capture px — the same units the size readout shows, so a
    // custom snip asking for 800x600 exports an 800x600 image whatever the monitor's
    // scaling); start() commits it and opens the annotate phase on it.
    void setPresetArea(const QRect& captureRect) { m_presetArea = captureRect; }

    void start();                                  // show overlays on all monitors
    void startQuickDrag(const QPoint& startCapturePx);  // hold-modifier gesture: open mid-drag
    void finishQuickDrag(const QPoint& releaseCapturePx);  // gesture button-up: extend + run action
    void cancelQuickDrag();                              // gesture click (no drag): discard, unshown

    // Queried by the per-screen views (global logical coords).
    QRect selection() const;
    bool selectionValid() const;
    QSize outputPixelSize() const;                 // pixel size of the composited result
    // Apply a value typed into the position/size readout above the region (capture px).
    // field: 0=x, 1=y, 2=w, 3=h. Editing w/h keeps the top-left corner where it is.
    void setSelectionField(int field, int value);

    // code hint. The committed selection is scanned on every commit (drag/resize/
    // move release and a typed size field); a decoded payload adds a third box to the
    // position/size readout row. Empty means "no code in the region" — the normal
    // case, and the signal the views use to skip the box entirely.
    QString barcodePayload() const { return m_barcodePayload; }
    bool barcodeCopied() const { return m_barcodeCopied; }     // brief post-click "Copied" state
    void copyBarcodePayload();                            // left-click: clipboard, overlay stays
    void openBarcodePayload();                            // Ctrl+left-click: browser, overlay closes

    // Auto-detected candidate region shown while hovering (before a drag).
    bool hasSuggestion() const;
    QRect suggestion() const { return m_suggested; }

    // Color readout state, used by the magnifier and the C-copy action. The
    // enabled format set + display flags come from the Interface preferences
    // (interface/magnifier/*), applied in start() via applyMagnifierSettings().
    ColorPicker::Format colorFormat() const {
        return m_enabledFormats.value(m_formatIdx, ColorPicker::Format::Hex);
    }
    // The magnifier lists every enabled format at once; formatIndex() marks the
    // row Shift has selected, which is the one colorFormat()/C copies.
    const QVector<ColorPicker::Format>& enabledFormats() const { return m_enabledFormats; }
    int formatIndex() const { return m_formatIdx; }
    bool uppercaseHex() const { return m_uppercaseHex; }
    bool noHash() const { return m_noHash; }
    bool usageTips() const { return m_usageTips; }
    // Loupe appearance: a per-source-pixel grid, the ( x , y ) readout line, and an
    // overall size percentage (100 = the base 15x12px loupe).
    bool gridLines() const { return m_gridLines; }
    bool showCoordinates() const { return m_showCoords; }
    int  magPercent() const { return m_magPercent; }
    void reportHoverColor(const QColor& c) { m_hoverColor = c; }

    // Snip display config (from snip/display/*, applied in start() via applySnipSettings()),
    // read by the per-screen views to render the selection chrome.
    int borderWidth() const { return m_borderWidth; }
    QColor maskColor() const { return m_maskColor; }
    QColor anchorColor() const { return m_anchorColor; }              // anchor fill
    QColor anchorBorderColor() const { return m_anchorBorderColor; }  // anchor border/edge
    QColor accentColor() const { return m_accentColor; }   // app accent (interface/accentColor), for aux lines
    bool showAnchors() const { return m_showAnchors; }
    bool crossLines() const { return m_crossLines; }
    // Auxiliary guide lines: Alt toggles them on/off, unless the feature is disabled.
    bool showAuxLines() const { return m_auxLinesOn && !m_noAuxLines; }
    void toggleAuxLines();                           // flip the aux-line toggle + persist it (G)
    void onAltReleased();                            // raw Alt key-up → drop the quick-mark preview

    // On-overlay key-binding tips panel (snip/display/keyTips). A hint
    // list drawn in a screen corner by the primary-screen view; independent of the
    // magnifier's inline usage-tips. One row = key "chips" + a translated description.
    // Rows are context-sensitive (Selecting / Read-only / Annotating); single-action
    // keys come from the keymap (ActionRegistry), multi-key clusters stay static.
    struct TipRow {
        QStringList keys;                            // chips, e.g. {"Ctrl","A"} or {"W","A","S","D"}
        QString     label;                           // already-translated description
        QString     sep;                             // drawn as plain text between the chips
                                                     // ("/" , "or") when the keys are separate
                                                     // actions rather than one combination
    };
    bool keyTipsEnabled() const { return m_keyTips; }
    QVector<TipRow> tipRows() const;                 // rows for the current scenario

    // Rounded corners (snip/display/roundedCorners + cornerRounded + cornerRadius).
    // Off by default: with the Preferences box unticked nothing shows and the region
    // stays a plain rectangle. When on, the readout row grows a chip — click it to
    // toggle right-angle/rounded, scroll it to change the bending level. The level is
    // kept in LOGICAL px (like pen widths) and scaled to capture px at use time, so a
    // corner looks the same size on any monitor.
    static constexpr int kMaxCornerRadius = 100;     // logical-px ceiling for the wheel
    bool cornerOptionEnabled() const { return m_cornerOption; }
    bool cornerRounded() const { return m_cornerRounded; }
    int  cornerRadiusLogical() const { return m_cornerRadius; }   // the number in the chip
    // The radius to actually draw/export with: 0 unless the option is on AND rounded
    // mode is picked, else the logical level scaled to capture px and clamped to what
    // the current selection can hold.
    int  effectiveCornerRadius() const;
    void toggleCornerMode();                         // chip click — persists
    void stepCornerRadius(int notches);              // chip wheel — persists

    // Captured-cursor preview. The freeze-time cursor is drawn on the frozen screen
    // (dimmed outside the selection, bright inside) and baked into exports; ` / !
    // toggles it live during a snip. The views read these to render it.
    bool captureCursorActive() const;               // toggle on AND a cursor was grabbed
    QImage capturedCursorImage() const { return m_cursorImage; }
    QRectF capturedCursorRectCapture() const;       // capture-px rect of the freeze-time cursor

    // Input from views (already in global logical coords).
    void onPress(const QPoint& g);
    void onMove(const QPoint& g);
    void onRelease(const QPoint& g);
    void onWheel(int steps);                       // resize the circle eraser (canvas scroll)
    // True while hovering in Element mode with nothing selected yet — the state in
    // which the wheel steps through the element hierarchy instead of resizing a tool.
    bool elementHierarchyActive() const;
    void onElementLevelWheel(int steps);            // +1 = outward to the parent element
    void onDoubleClick();
    void onMiddleClick();                          // configurable middle-click action
    void onAbort();
    void onKey(int key, Qt::KeyboardModifiers mods);
    void onKeyRelease(int key);                    // only F5 (refresh) cares about key-up

    // Refresh screenshot: re-grab the frozen desktop under the overlay. A tap refreshes
    // once; holding (past kRefreshHoldMs) keeps refreshing until the hold ends, so the
    // view only "locks" again on release. Two independent sources — the F5 key and the
    // readout-row button — are tracked separately so releasing one doesn't cancel the
    // other. The selection, annotations and undo stack all survive a refresh; only the
    // background pixels (and what is derived from them) change.
    enum class RefreshSource { Key, Button };
    void beginRefresh(RefreshSource s);
    void endRefresh(RefreshSource s);
    bool refreshing() const;                       // continuous refresh running? (button look)
    // Spin angle (degrees, counter-clockwise) for the readout row's refresh icon: it
    // turns only while the repeating grab is actually running, so the spin is the
    // "continuous refresh" tell. 0 whenever the loop is idle. Derived from elapsed time,
    // not a tick counter, so the animation stays even when a grab runs long.
    qreal refreshSpin() const;

    // Toolbar actions. `thenQuit` controls whether the overlay closes after the action
    // (the configurable double/middle/Enter actions may keep it open).
    void copy(bool thenQuit = true, bool asFile = false);   // asFile: Ctrl+Alt+C copy-as-file
    void save(bool thenQuit = true);
    void quickSave(bool thenQuit = true);          // silent save to Pictures, no dialog

    // Pin the current selection (Ctrl+T / middle-click) — handed to the app, which
    // owns pin creation. No-op if no sink is set or the selection is invalid.
    // The sink receives the flattened image plus the capture geometry (global-logical
    // coords) so the app can place the pin exactly where it was captured.
    void setPinSink(std::function<void(const QImage&, const QRect&)> sink) { m_pinSink = std::move(sink); }
    void onPin(bool thenQuit = true);

    // A past snip's full-screen freeze (per monitor) + its finalized capture area,
    // used by the `,`/`.` replay to restore that whole desktop for a re-snip.
    struct HistoryFrame {
        QList<ScreenShot> shots;        // per-monitor freeze (empty = nothing to restore)
        QRect             selection;    // finalized capture area (global logical)
    };

    // History (Stage 6). The sink records a successful snip (copy/save/pin) so the
    // app's History ring persists it — the cropped image plus the full-screen freeze
    // (`shots`) it was cut from and the selection rect, so `,`/`.` can later restore
    // that past desktop. `thumbs` returns the recorded thumbnails (newest first) shown
    // in the strip; `pick` returns the cropped snip at an index (copied when chosen);
    // `restore` returns the full-screen freeze + area at an index for the replay.
    void setHistorySink(std::function<void(const QImage&, const QList<ScreenShot>&, const QRect&)> sink)
        { m_historySink = std::move(sink); }
    void setHistoryThumbs(std::function<QVector<QImage>()> f) { m_historyThumbs = std::move(f); }
    void setHistoryPick(std::function<QImage(int)> f) { m_historyPick = std::move(f); }
    void setHistoryRestore(std::function<HistoryFrame(int)> f) { m_historyRestore = std::move(f); }

    // In-snip history strip (Stage 6). A thumbnail row of recent snips, opened by the
    // first `,`/`.`, navigated with `,`/`.` or the mouse; Enter or a click copies the
    // highlighted snip and closes, Esc (or right-click) closes just the strip. All
    // geometry is global-logical; the views read StripLayout to render + hit-test it.
    struct StripLayout {
        QRect panel;                    // panel background (invalid when closed)
        QVector<QRect>  thumbRects;     // one rect per visible thumbnail
        QVector<QImage> thumbs;         // visible thumbnails (aligned with thumbRects)
        int start = 0;                  // absolute history index of thumbRects[0]
        int selectedSlot = -1;          // index into thumbRects of the highlighted thumb
    };
    bool historyStripOpen() const { return m_stripOpen; }
    StripLayout historyStripLayout() const;
    // Called by a view on a left-click: true if the click hit the (open) strip — picks
    // the clicked thumbnail if any, and consumes the click either way.
    bool handleStripClick(const QPoint& g);

    // Area history (Stage 6). The sink records a successful snip's selection rect so
    // the app's AreaHistory ring remembers it. The nav returns the older (dir<0) /
    // newer (dir>0) recorded area for `R`/`Shift+R` reuse (null at the ends when loop
    // is off); a returned rect is restored into the live selection and the overlay
    // stays open.
    void setAreaSink(std::function<void(const QRect&)> sink) { m_areaSink = std::move(sink); }
    void setAreaNav(std::function<QRect(int)> nav) { m_areaNav = std::move(nav); }

    // Read-only mode: the selection is committed but no annotation tool is active.
    // The region can't be re-drawn — only dragged whole (size preserved) or resized
    // via the 8 edge/corner grips. Used by the views to draw the grips + set cursors.
    bool isReadOnly() const { return m_editing && m_tool == annotation::ToolId::None; }

    // Cursor shape for a global-logical point: resize arrows over a grip, a move
    // cursor inside the region (read-only), else the crosshair.
    Qt::CursorShape cursorAt(const QPoint& g) const;

    // True while an annotation tool is active. The loupe/colour picker only shows
    // when no tool is selected (region-choosing or read-only), not while drawing.
    bool toolSelected() const { return m_tool != annotation::ToolId::None; }

    // True while the overlay must own the mouse instead of the app being recorded: an
    // armed tool or an open text box needs the clicks. Drives both the WS_EX_TRANSPARENT
    // toggle (applyRecordingInput) and the hit-test veil the views paint over the region
    // while recording (SnipScreenView::paintEvent) — the two must agree.
    bool recordingInputArmed() const;

    // True while the capture area itself is draggable (scrolling capture's Move mode).
    // The views read it for the drag cursor, and to keep their wheel handler out of the
    // way — Move mode is the only time wheel events reach Qt during a scrolling session.
    bool scrollMoveActive() const { return m_scrollMoving; }

    // Active tool + its pen width/color, read by the views to draw the eraser/pen cursor.
    annotation::ToolId activeTool() const { return m_tool; }
    int activePenWidth() const { return m_style.penWidth; }
    QColor activeColor() const { return m_style.color; }

    // The rotated corner polygon (global logical) of the committed text box currently
    // hovered with the Text tool — drawn as a dashed selection outline. Empty = none.
    QVector<QPoint> textHoverOutline() const { return m_textHoverOutline; }

    // On-canvas non-text shape editing. The hovered shape's shape-matched dashed outline
    // (capture px), and the selected shape's control points, for the views to render.
    QPainterPath shapeHoverPath() const;
    struct ShapeHandles {
        bool active = false;
        QVector<QPoint> box;       // oriented frame corners TL,TR,BR,BL (capture px)
        QVector<QPoint> grips;     // 8 grip centres: 4 corners + 4 edge midpoints (capture px)
        QPoint rotateHandle;       // spin handle centre (capture px)
        QPoint rotateAnchor;       // top-edge midpoint the rotate stalk grows from (capture px)
        QPoint deleteHandle;       // delete (×) button centre (capture px)
        QPoint deleteAnchor;       // top-right corner the delete stalk grows from (capture px)
        qreal  rotationDeg = 0.0;  // shape rotation, so the view can orient the glyphs
    };
    ShapeHandles shapeHandles() const;
    // While a selected shape is actively being rotated: the degree text + where to place its
    // pill (capture px, outward past the spin handle). Inactive otherwise — mirrors the Text
    // tool's live rotation readout.
    struct ShapeRotateReadout { bool active = false; QString text; QPoint pos; };
    ShapeRotateReadout shapeRotateReadout() const;

    // Quick-mark: the UI element under the cursor while Alt is held with a rect-family
    // tool, in capture px. Drawn as a dashed preview of what Alt+right-click would fill.
    // Null rect = inactive. NOT clipped to the selection — see refreshQuickMark().
    QRect quickMarkRect() const { return m_qmRect; }

    // The live (logical-sized) style, so a view can preview the number-label badge under
    // the cursor at exactly the size/colour/look a click would stamp.
    const annotation::Style& activeStyle() const { return m_style; }
    // The number the next click stamps — what the cursor preview shows. Empty when a
    // click would NOT stamp: while a badge is being renumbered, and while hovering a
    // committed badge (that click edits it, so previewing a new badge would lie). The
    // system cursor comes back in both cases — see cursorAt().
    QString nextLabelNumber() const
    {
        if (labelEditing() || !m_haveCursor || labelAt(m_lastCursor))
            return {};
        for (const QRect& r : labelStepButtons())   // chrome, not canvas: a click here steps
            if (r.contains(m_lastCursor))
                return {};
        if (labelDeleteButton().contains(m_lastCursor))
            return {};
        return QString::number(m_nextLabel);
    }
    // Draw the renumber caret this frame? (blink phase, only while editing)
    bool labelCaretVisible() const { return labelEditing() && m_labelCaretOn; }
    // The badge currently being renumbered (still in the document), or null.
    const annotation::Annotation* editedLabel() const;
    // The +/− step buttons for that badge, in capture px: [0] = plus (above centre),
    // [1] = minus (below). Sized from the monitor scale so they stay a fixed on-screen
    // size regardless of badge size. Empty when not editing.
    QVector<QRect> labelStepButtons() const;
    // The × (delete) button, centred ABOVE the badge's outline so it reads as separate
    // from the steppers. Null rect when not editing.
    QRect labelDeleteButton() const;
    // Whether ±1 is available: numbering is kept inside the existing badges' range, so
    // the highest badge can't step up and the lowest can't step down.
    bool labelCanStep(int delta) const;

    // OCR mode: recognized text greys out in place and drag-selects. Active only after
    // the row-1 OCR button is toggled on and a result has arrived. The per-screen views
    // call drawOcrLayer() to paint the grey/accent word boxes (mapping capture px →
    // their local coords); input arrives through the normal onPress/onMove/onRelease.
    bool ocrActive() const { return m_ocrActive; }
    void drawOcrLayer(QPainter& p, SnipScreenView* view) const;

    // Annotation: editing becomes available once a selection is committed. The
    // PaintBar drives tool/color/width; views read the document + draft to render.
    bool isEditing() const { return m_editing; }
    AnnotationDocument* document() const { return m_doc; }
    const annotation::Annotation* draft() const { return m_draft.get(); }
    void setTool(annotation::ToolId t);
    void setColor(const QColor& c);
    void setPenWidth(int w);          // also restyles the selected shape (scaled to capture px)
    void setRedactStrength(int s);    // also restyles the selected shape (scaled to capture px)
    void setFilled(bool f);           // also restyles the selected shape
    void setLineStyle(annotation::LineStyle s);   // also restyles the selected shape
    // The line tool's three style controls. All three also restyle the selected shape.
    void setStartCap(annotation::CapStyle c);
    void setEndCap(annotation::CapStyle c);
    void setCurved(bool c);           // line tool: straight segments ↔ smooth curve
    // Text tool style — updates m_style and (if a box is being edited) the live editor.
    void setTextBold(bool b);
    void setTextItalic(bool b);
    void setTextHollow(bool h);
    void setTextFamily(const QString& family);
    void setTextFontPx(qreal px);

    // Line tool: while a multi-segment line is being placed or its control points are
    // being adjusted, the views draw the draft + (in the control phase) the handles.
    bool lineEditing() const { return m_linePhase != LinePhase::Idle; }
    QVector<QPoint> lineControlPoints() const;
    void undoAnnotation();
    void redoAnnotation();
    void clearAnnotations();

private:
    // Which part of a committed region a point hits (for move / 8-way resize).
    enum class Handle { None, Move, Left, Right, Top, Bottom,
                        TopLeft, TopRight, BottomLeft, BottomRight };
    Handle handleAt(const QPoint& g) const;        // read-only mode hit-test (global logical)
    void applyResize(const QPoint& g, Qt::KeyboardModifiers mods);  // drag a grip → reshape the selection
    // Shift (keep ratio) / Ctrl (square) lock for a corner-grip drag; identity otherwise.
    QPoint constrainCorner(const QPoint& g, Qt::KeyboardModifiers mods) const;

    void updateAllViews();
    void applyMagnifierSettings();                  // pull interface/magnifier/* from m_appSettings
    void applySnipSettings();                       // pull snip/display|behavior|keys/* from m_appSettings
    void runSnipAction(const QString& action, bool thenQuit);   // copy/save/quicksave/pin/nothing
    void hideAllToolbars();                        // hide the PaintBar (during drag/draw)
    void updateEditChrome();                       // (re)place the PaintBar over the selection
    void beginEditing();
    void nudgeCursor(int dx, int dy);              // W/A/S/D: move the OS cursor 1 logical px
    void startDraft(const QPoint& g);
    void updateDraft(const QPoint& g, Qt::KeyboardModifiers mods);
    void commitDraft();

    // ---- Quick-mark (Alt+hover previews a UI element, Alt+right-click fills it) ------
    // Maps the active tool to the tool a quick-mark actually creates, or None when the
    // tool has no rectangular reading. The rect-drag family maps to itself; the freehand
    // redaction modes map to their rect twin, since a quick-mark is always a rectangle.
    // Unlike the selection phase this ignores m_detectMode entirely: Alt is the
    // deliberate gesture, so detection runs even when the user cycled detection Off.
    static annotation::ToolId quickMarkToolFor(annotation::ToolId t);
    bool quickMarkTool() const { return quickMarkToolFor(m_tool) != annotation::ToolId::None; }
    void refreshQuickMark(bool force = false);   // (re)resolve m_qmRect from the cursor
    void clearQuickMark();                       // drop the preview + repaint if it was up
    bool applyQuickMark();                       // Alt+right-click: fill the element; consumed?

    // ---- Line tool: 3-phase build → control → finalize state machine -------------
    // Phase 1 (Building): left-click places vertices, cursor previews the next; a
    //   right-click ends it. Phase 2 (Controlling): vertices show as draggable
    //   control points; a second right-click finalizes (commits to the document).
    bool isLineTool() const { return m_tool == annotation::ToolId::Line; }
    void handleLinePress(const QPoint& g, Qt::KeyboardModifiers mods);
    void handleLineMove(const QPoint& g, Qt::KeyboardModifiers mods);
    void handleLineRelease(const QPoint& g, Qt::KeyboardModifiers mods);
    // Shift snaps the segment ending at `g` to 45° steps, measured from `from`.
    static QPoint lineSnap(const QPoint& from, const QPoint& g, Qt::KeyboardModifiers mods);
    bool handleLineSecondary();                    // right-click: advance/finalize; consumed?
    void rebuildLineDraft(const QPoint* floating); // draft geom = placed verts (+ cursor)
    int lineControlHit(const QPoint& g) const;     // index of the control point under g, or -1
    void resetLineState();
    void placeText(const QPoint& g);            // open an in-place text box at g
    void wireTextEditor();                      // connect the active editor's signals
    annotation::Annotation* textAt(const QPoint& g) const;   // topmost committed text under g
    void reopenText(annotation::Annotation* a); // pull a committed text back into an editor
    void commitTextEditor();                    // bake the active text box into the document
    void cancelTextEditor();                    // discard the active text box (no bake)
    bool textEditing() const { return m_textEditor != nullptr; }
    void syncPaintBarState();

    // ---- Number label (auto-numbering badge) ---------------------------------------
    // Click stamps the next number; clicking an existing badge renumbers it in place
    // (digits typed straight into the overlay — no editor widget), dragging moves it,
    // and the wheel resizes it via the toolbar's size box.
    void setLabelStyle(annotation::LabelStyle s);
    // Numbering glyphs. Unlike every other style setter this one is retroactive: the
    // badges form one sequence, so it restyles the ones already stamped too (one undo step).
    void setNumberStyle(annotation::NumberStyle s);
    annotation::Annotation* labelAt(const QPoint& g) const;  // topmost badge under g
    int labelCount() const;                                  // badges currently in the document
    // Step the counter by however many badges an undo/redo added or removed, so Ctrl+Z on a
    // stamp also takes the number back. Call with labelCount() sampled BEFORE the undo/redo.
    void syncNextLabel(int labelsBefore);
    // Adopt the numbering style the badges now carry (an undo/redo can revert the
    // retroactive restyle), reflecting it into the toolbar without re-emitting.
    void syncNumberStyleFromDoc();
    void placeLabel(const QPoint& g);                        // stamp the next number at g
    void handleLabelPress(const QPoint& g);
    void handleLabelRelease();
    void beginLabelEdit(annotation::Annotation* a);          // start renumbering a badge
    void commitLabelEdit();                                  // push the renumber as one undo step
    void cancelLabelEdit();                                  // restore the original numbering
    bool handleLabelEditKey(int key);                        // digits/Backspace/Enter/Esc
    bool labelEditing() const { return m_labelEditId > 0; }
    // Step the edited badge's number by ±1, swapping with whichever badge holds the
    // target — that keeps every other badge's relative order intact.
    void stepLabel(int delta);
    // Wheel-resizing a committed badge is a burst of events with no release, so the size
    // is applied live and recorded as ONE undo step when the burst goes idle (or when
    // anything else happens that would make a late flush confusing — a click, a tool
    // change, an undo). Same "apply live, record once" contract as a drag.
    void beginLabelResize(int id);
    void flushLabelResize();
    // Delete the badge being edited (× button / Del). Badges numbered above it drop by
    // one so the numbering stays contiguous — all of it, plus any renumbering done in
    // this edit session, as ONE undo step.
    void deleteLabel();
    void setLabelCaretOn(bool on);                           // blink phase → repaint
    void restartLabelCaret();                                // solid caret while typing

    // ---- Scrolling capture ---------------------------------------------------------
    // Row-1 button (right of OCR) → the region goes live and click-through, the drawing
    // tools grey out for good, and a 2nd-level bar + preview strip appear. Each wheel
    // event restarts an idle timer; when it fires the region is grabbed and stitched.
    // Done re-freezes and hands the tall image to row 1's Copy/Save/Pin; Cancel/Esc
    // discards it and returns to ordinary annotation on a freshly grabbed region (the
    // next Esc closes the capture).
    //
    // The axis (scroll/direction, flipped from the 2nd-level bar) is handled by a
    // TRANSPOSE: a horizontal session rotates each grabbed frame 90° clockwise into the
    // very same vertical ScrollStitcher and rotates the canvas back at Done. Everything
    // between — matching, the strip's marker, the height cap, Move mode's overlap clamp
    // — therefore works in one orientation only. Measured, the transpose is the cheap
    // path, not the lazy one: rotating a 1600×1200 frame costs ~2.7 ms once, while a
    // column-major matcher would pay strided access (10× a row scan) on every scored
    // offset — ~30 ms a frame — and lose the per-row memcmp short-circuit entirely.
    void startScrollCapture();              // PaintBar::scrollToggled(true)
    void setScrollDirection(bool horizontal);   // ScrollCaptureBar::directionToggled
    void grabScrollFrame();                 // wheel went idle: grab + stitch + repaint
    void finishScrollCapture();             // Done: stop, re-freeze, show the result
    void cancelScrollCapture();             // Cancel / Esc: discard, re-grab, annotate
    void updateScrollStrip();               // re-place + repaint the preview strip
    // Throw away everything on one side of the live region. The region itself
    // survives, so the session carries on from where it is; scrolling back into the cut
    // ground re-captures it the ordinary way, which is the only undo there is.
    void trimScrollCapture(scroll::ScrollStitcher::Side side);
    // Size readout + per-side trim availability — everything on the 2nd-level bar that is
    // a function of the canvas alone, so a frame and a trim can both just call it.
    void syncScrollBarState();
    // Where the preview strip goes, in global logical coords: beside the region for a
    // vertical capture, above/below it for a horizontal one. Pure — updateEditChrome
    // asks it too, so the toolbars can step out of the strip's way.
    QRect scrollStripGeometry() const;
    // Height of the horizontal strip where it sits directly under the selection, which
    // is the one placement the toolbars have to make room for. 0 otherwise.
    int scrollStripBelowHeight() const;
    // The region's extent ALONG the capture axis — the height of a frame as the stitcher
    // sees it, which for a horizontal session is the region's width.
    int scrollFrameLength() const
    { return m_scrollHorizontal ? m_scrollRegion.width() : m_scrollRegion.height(); }

    // Auto-scroll: SnapCap drives the wheel instead of the user. Injected wheels reach
    // whatever is under the POINTER, so starting parks it in the region's centre; moving
    // it back out is one of the ways to stop (alongside the button, taking the wheel
    // yourself, and reaching the end). Each step's size is tuned from how far the page
    // actually moved last time, targeting ~50% of the region so overlap is never lost.
    // Move mode: drag the capture area itself. It exists for content the region never
    // covered — start capturing halfway down a page and the part ABOVE the region is
    // unreachable, because the page cannot scroll up past its own top. Moving the region
    // up reaches it. To the stitcher this is indistinguishable from scrolling: with a
    // static page, shifting the region up N px shifts the frame's content by the same N,
    // so it prepends through the ordinary path with no matcher changes.
    void setScrollMoveMode(bool on);        // ScrollCaptureBar::moveModeToggled / Esc
    // Drag of the region ALONG THE CAPTURE AXIS only, clamped to keep overlap with what
    // is captured. Returns false when the press was not ours to take.
    bool beginScrollMove(const QPoint& gCapture);
    void applyScrollMove(const QPoint& gCapture);
    void endScrollMove();

    void startAutoScroll();
    void stopAutoScroll(const QString& messageKey);   // empty key = no message
    void autoScrollStep();                  // park the pointer, inject, let the grab follow
    void afterAutoScrollFrame(int grewBy);  // adapt the step size, or stop at the end
    void autoScrollBackOff();               // overshot: undo the step, halve it, carry on
    bool scrollCaptureActive() const { return m_scrolling; }
    // True once a session has produced an image — the point from which the output
    // buttons act on the stitched result instead of the frozen region.
    bool hasScrollResult() const { return !m_scrollResult.isNull(); }

    // ---- Live region (shared) ------------------------------------------------------
    // Drop the desktop freeze over the selection so the LIVE window shows through, and
    // park the toolbars as top-level windows so they stay clickable over views that are
    // about to go click-through. This is the common half of every mode that hands the
    // mouse back to the app underneath; each mode then layers its own policy on top —
    // see enterRecordingLive() and startScrollCapture().
    void enterLiveRegion();
    // Put the freeze and the clicks back. The bars deliberately stay top-level: nothing
    // needs them re-parented (they are placed correctly either way) and staying floating
    // keeps their exclude-from-capture affinity. Only scrolling capture uses this — a
    // recording ends by closing the overlay.
    void exitLiveRegion();

    // ---- Screen recording ----------------------------------------------------------
    // The row-1 record button opens the RecordBar (2nd row, or 3rd when a tool sub-bar
    // is up). Record → optional countdown → the region goes LIVE and click-through and
    // the recorder starts; Stop finalizes the file and closes the overlay.
    void toggleRecordBar(bool on);          // PaintBar::recordToggled
    void requestRecordStart();              // RecordBar Record: countdown, or start now
    void startRecordingNow();               // countdown finished (or none): open the recorder
    void stopRecording();                   // RecordBar Stop
    void enterRecordingLive();              // enterLiveRegion() + the recording-only policy
    // Click-through follows the armed tool: a tool needs the clicks to draw with, no
    // tool means the app being recorded should get them. No-op unless recording.
    void applyRecordingInput();
    // Re-render the annotation layer and hand it to the recorder to burn into frames.
    // Flattened over a TRANSPARENT base, so it is the annotations alone.
    void pushRecordingOverlay();
    bool recordingActive() const;           // counting down or recording
    // Park the toolbars as top-level windows while recording, so the overlay itself can
    // be click-through without disabling the buttons. (Masking a hole in the overlay was
    // rejected: a mask cannot paint, so drawn annotations would vanish whenever no tool
    // is armed.) floatBars(false) only clears the flag — nothing re-parents the bars
    // back, and nothing needs to; see exitLiveRegion().
    void floatBars(bool floating);
    // Hide whichever floated bars are currently up, and return them so the caller can put
    // exactly those back. Needed around any dialog raised mid-session: hiding the views
    // takes their CHILD bars with them, which is all a normal snip has, but a floated bar
    // is a top-level always-on-top window and would otherwise cover the dialog. Returns
    // empty (and does nothing) while the bars are still children.
    QVector<QWidget*> hideFloatingBars();

    // ---- OCR mode (see ocrActive()) ------------------------------------------------
    void toggleOcr(bool on);                     // enter/leave OCR mode (PaintBar::ocrToggled)
    void startOcrRecognition();                  // recognize the committed selection on a worker
    void onOcrReady(const platform::OcrResult& r); // worker result → layer (main thread)
    void handleOcrPress(const QPoint& g);        // begin text selection (global logical in)
    void handleOcrMove(const QPoint& g);         // extend the selection
    void handleOcrRelease();                     // end the drag
    void copyOcrText();                          // copy selected text (or all if none)
    QString defaultOcrLang() const;              // UI-locale → installed traineddata, else eng
    QPoint captureToOcrImage(const QPoint& gCapture) const; // global capture px → recognized-image px
    // A copy of m_style with pen/font/redact sizes scaled from logical (PaintBar) into
    // capture px for the monitor at `anchor`, so a stroke keeps its on-screen size while
    // annotation geometry lives in capture space. Used when creating any annotation.
    annotation::Style captureStyle(const QPoint& anchor) const;
    // ---- code hint --------------------------------------------------------------------
    // Re-decode the committed selection and repaint if the payload changed. Called
    // from every place the region settles (the m_sel->commit() sites). Cheap to call
    // with no backend or no selection — it just clears the payload.
    void rescanBarcode();

    // ---- refresh screenshot (see beginRefresh) -------------------------------------
    void grabScreens();          // (re)freeze every monitor into m_shots + m_layout
    void refreshFreeze();        // grabScreens() with our overlays made invisible + repaint
    void stopRefreshLoop();      // end the continuous refresh, leaving the last frame frozen

    QImage compositeBase(const QRect& selCapture) const;  // stitch selection + (optionally) the freeze-time cursor
    // What Copy / Save / Pin act on: normally the flattened, corner-rounded selection,
    // but the stitched image once a scrolling session has produced one (its annotation
    // document is empty by then — the tools are off for the session).
    QImage finalImage();
    void refreshDetection(bool force = false);     // re-query the candidate at m_lastCursor
    void copyColor();
    void finish();

    SelectionModel* m_sel = nullptr;
    QList<ScreenShot> m_shots;
    ScreenLayout m_layout;                         // per-monitor logical<->capture-px map
    QList<SnipScreenView*> m_views;

    platform::IElementDetect* m_detect = nullptr;
    platform::IScreenCapture* m_capture = nullptr; // kept for re-grabs (may be null in tests)
    platform::IClipboard* m_clipboard = nullptr;   // Output→Clipboard backend (may be null in tests)
    platform::IKeyboardFilter* m_keyFilter = nullptr;  // snip-time LL keyboard hook (may be null in tests)
    platform::IOcr* m_ocr = nullptr;               // OCR backend (may be null: button then no-ops)
    platform::IBarcodeDecode* m_barcode = nullptr;           // barcode backend (may be null: hint never appears)
    QString m_barcodePayload;                           // decoded payload ("" = none in the region)
    bool    m_barcodeCopied = false;                    // "Copied" feedback shown until m_barcodeCopiedTimer fires
    QTimer* m_barcodeCopiedTimer = nullptr;             // one-shot, clears m_barcodeCopied

    Settings* m_appSettings = nullptr;             // app Settings, forwarded to PaintBar
    ActionRegistry* m_actions = nullptr;           // key-tips label source (may be null in tests)
    QString m_windowTitle;                         // foreground window title captured at snip start
    DetectMode m_detectMode = DetectMode::Window;
    QRect m_suggested;                             // global logical candidate rect
    QPoint m_lastCursor;                           // last hover pos (global logical)
    bool m_haveCursor = false;

    // Element-hierarchy walk (Element mode). Hovering only ever resolves m_leaf (one
    // cheap provider hit test); m_chain is filled lazily on the first wheel step
    // because the ancestor walk costs a round trip per level. The level survives
    // cursor drift and resets only when m_leaf itself changes — holding a mouse
    // perfectly still while scrolling isn't reasonable to ask.
    QRect m_leaf;                                  // innermost element under the cursor
    QList<QRect> m_chain;                          // global logical, innermost first
    int m_level = 0;                               // index into m_chain

    // Magnifier readout config (from interface/magnifier/*, see applyMagnifierSettings).
    QVector<ColorPicker::Format> m_enabledFormats{ColorPicker::Format::Rgb,
                                                  ColorPicker::Format::Hex};
    int m_formatIdx = 1;                            // index into m_enabledFormats (Hex by default)
    bool m_uppercaseHex = false;
    bool m_noHash = false;
    bool m_usageTips = true;
    bool m_gridLines = true;
    bool m_showCoords = true;
    int  m_magPercent = 100;                        // 50..200, scales loupe + readout panel
    QColor m_hoverColor;

    // Snip preferences (from snip/*, applied in start() via applySnipSettings()).
    int m_borderWidth = 3;
    QColor m_maskColor{QStringLiteral("#464646")};
    QColor m_anchorColor{Qt::white};
    QColor m_anchorBorderColor{QStringLiteral("#2d7dff")};
    QColor m_accentColor{QStringLiteral("#2d7dff")};
    bool m_showAnchors = true;
    bool m_crossLines = false;
    bool m_keyTips = true;                            // snip/display/keyTips → corner hint panel
    bool m_cornerOption = false;                      // snip/display/roundedCorners → show the chip
    bool m_cornerRounded = false;                     // snip/display/cornerRounded → arcs vs right angles
    int  m_cornerRadius = 12;                         // snip/display/cornerRadius, LOGICAL px
    bool m_noAuxLines = false;
    bool m_hide1x1 = true;
    int  m_clickSlopPx = 30;                          // snip/behavior/clickSlopPx, LOGICAL px (0 = gate off)
    int  m_clickWindowMs = 300;                       // snip/behavior/clickWindowMs
    bool m_auxLinesOn = false;                       // G toggle state → show aux lines
    bool m_quitOnFocusLoss = false;
    bool m_suspendFocusQuit = false;                 // set around our own modal dialogs
    bool m_disableScroll = false;
    bool m_confirmEsc = false;
    bool m_ignoreSimCopy = true;                     // swallow injected Ctrl+C (via m_keyFilter)
    bool m_ignoreWinLeft = true;                     // suppress lone Left-Win Start menu
    bool m_ignoreWinRight = false;                   // suppress lone Right-Win Start menu
    bool m_oneShotCopy = false;                      // SnipCopy hotkey: confirm → copy + close
    bool m_quickMode = false;                        // hold-modifier gesture: drag → copy/pin + close
    bool m_quickCopy = false;                        // quick-snip: copy the region
    bool m_quickPin = false;                          // quick-snip: pin the region
    bool m_quickSeeded = false;                       // hold-modifier gesture: drag seeded from press
                                                      // (suppresses Ctrl→square, whose modifier is held)
    bool m_quickReleased = false;                     // gesture release handled once (hook or Qt-native)
    bool m_colorPickOnly = false;                    // PickColor hotkey: loupe only, click/C copies
    bool m_barcodeScanOnly = false;                  // ScanBarcode hotkey: decode region → copy + close
    QRect m_presetArea;                              // custom-snip area (capture px)
    QString m_doubleClickAction{QStringLiteral("copy")};   bool m_doubleClickQuit = true;
    QString m_middleClickAction{QStringLiteral("pin")};    bool m_middleClickQuit = true;
    QString m_enterAction{QStringLiteral("copy")};         bool m_enterQuit = true;

    // Cursor capture (snip/captureCursor; snip/behavior/disableCursorCapture greys it out).
    // The cursor is snapshotted at freeze (constructor) and composited into the exported
    // image only, at its freeze-moment position — see compositeBase().
    bool   m_captureCursor = false;
    bool   m_disableCursorCapture = false;
    bool   m_cursorCaptureOn = false;                // live effective state (` / ! toggles it)
    bool   m_cursorVisible = false;
    QImage m_cursorImage;                            // ARGB32 cursor pixels (device res)
    QPoint m_cursorHotspot;                          // hotspot in m_cursorImage pixels
    QPoint m_cursorPos;                              // global logical, freeze moment
    qreal  m_cursorDpr = 1.0;                        // dpr of the screen under the cursor

    // Refresh-screenshot state (see beginRefresh). m_refreshHeld is a bitmask of the
    // RefreshSource values currently held down.
    int m_refreshHeld = 0;
    // True once every view is excluded from screen capture, so a re-grab needs no hiding
    // at all (no blink). False on an OS without it → refreshFreeze() hides the overlays.
    bool m_captureExcluded = false;
    QTimer* m_refreshHold = nullptr;               // single-shot: a held press turns continuous
    QTimer* m_refreshRepeat = nullptr;             // fires while the hold lasts
    QTimer* m_refreshSpin = nullptr;               // animation ticks for the icon's spin
    QElapsedTimer m_refreshClock;                  // restarted when the loop starts (icon spin)

    QElapsedTimer m_detectClock;                   // throttles detection queries

    // Quick-mark preview state. Kept apart from m_suggested/m_leaf on purpose: those
    // drive the selection-phase chrome and must stay dark while annotating.
    QRect m_qmRect;                                // element under the cursor, capture px
    QElapsedTimer m_qmClock;                       // throttles the quick-mark hit tests

    // Annotation editing state.
    AnnotationDocument* m_doc = nullptr;
    PaintBar* m_paintBar = nullptr;
    bool m_editing = false;                         // selection committed → can draw
    bool m_paintBarHidden = false;                  // Space toggles visibility
    annotation::ToolId m_tool = annotation::ToolId::None;
    annotation::Style m_style;
    std::unique_ptr<annotation::Annotation> m_draft; // shape being drawn
    bool m_drawing = false;

    // OCR mode state. m_ocrSelCapture is the capture-px selection the current result's
    // word boxes are relative to (fixed at recognition time).
    // Recording state. m_recordBar is created lazily by the row-1 record button; the
    // session runs entirely inside this overlay, which therefore stays alive for the
    // whole recording (that is also what blocks a second snip mid-recording).
    platform::IRecorder* m_recorder = nullptr;   // may be null: button disabled
    RecordBar* m_recordBar = nullptr;

    // Scrolling-capture state. Like recording, the session runs entirely inside this
    // overlay. m_scrollResult is empty until Done: from then on it is what the output
    // buttons act on (see finalImage()).
    platform::IWheelWatch* m_wheelWatch = nullptr;   // may be null: button disabled
    ScrollCaptureBar* m_scrollBar = nullptr;
    ScrollStripWindow* m_scrollStrip = nullptr;
    std::unique_ptr<scroll::ScrollStitcher> m_stitcher;
    QTimer* m_scrollIdle = nullptr;              // wheel-idle debounce
    bool  m_scrolling = false;                   // session running (live + hooked)
    bool  m_scrollHorizontal = false;            // capture axis: frames are transposed
    QRect m_scrollRegion;                        // capture px, fixed when the session starts
    QImage m_scrollResult;                       // the stitched image, set by Done
    bool  m_scrollMoving = false;                // Move mode armed (region is draggable)
    bool  m_moveDragging = false;                // a move drag is in flight
    int   m_moveGrabDy = 0;                      // press point's offset from the region's
                                                 // leading edge, along the capture axis
    int   m_moveStartTop = 0;                    // that edge when the drag began
    int   m_moveStartLive = 0;                   // liveOffset then — the prediction's base
    int   m_movePredictedLive = 0;               // where the live frame sits mid-drag
    bool  m_autoScroll = false;                  // driving the wheel ourselves
    int   m_autoNotches = 1;                     // current step size; adapts after each frame
    int   m_autoStalls = 0;                      // consecutive steps that added nothing
    int   m_autoLastSent = 0;                    // notches of the step in flight, to undo it
    int   m_autoBackoffs = 0;                    // consecutive overshoots
    QTimer* m_countdownTimer = nullptr;          // 1 Hz while counting down
    int m_countdownLeft = 0;
    bool m_recording = false;                    // recorder running (or paused)
    bool m_barsFloating = false;                 // toolbars re-parented to top level
    QRect m_recordRegion;                        // capture px, fixed when recording starts

    OcrBar* m_ocrBar = nullptr;
    OcrTextLayer m_ocrLayer;
    bool m_ocrActive = false;
    QRect m_ocrSelCapture;
    QString m_ocrLang;

    // In-place text editing (Text tool). The editor is parented to its host view.
    TextBoxEditor* m_textEditor = nullptr;
    SnipScreenView* m_textHost = nullptr;
    // Hover highlight for the click-to-place tools: the committed Text box OR number
    // badge under the cursor (capture px). Only one such tool is ever active.
    QVector<QPoint> m_textHoverOutline;

    // ---- On-canvas non-text shape editing (Rect/Ellipse/Mosaic/Blur/MarkerRect) --------
    // Works regardless of the active tool: hovering a committed shape shows a dashed
    // outline, clicking selects it (8 grips + a spin handle), and it can then be moved,
    // resized (opposite grip pinned) or rotated. All geometry is capture px.
    static bool isSelectableShape(annotation::ToolId t);
    annotation::Annotation* shapeById(int id) const;         // committed shape with this id, or null
    annotation::Annotation* selectedShape() const;           // selected AND a selectable shape, else null
    annotation::Annotation* selectableShapeAt(const QPoint& g) const;  // topmost selectable under g
    // Fill-aware pick used for hover/select: a hollow Rect/Ellipse is only "hit" near its
    // outline (not the empty interior); filled shapes + mosaic/blur/marker hit anywhere.
    bool shapeHoverHit(const annotation::Annotation* a, const QPoint& g) const;
    void selectShape(int id);
    void deselectShape();
    void deleteSelectedShape();
    // The 8 grip centres + the rotate & delete handles/anchors of a shape's oriented box
    // (capture px). The delete (×) button sits above the top-right corner, the spin handle
    // above the top-edge midpoint — matching the Text tool's chrome.
    void shapeControlPoints(const annotation::Annotation* a, QVector<QPoint>& grips,
                            QPoint& rotateHandle, QPoint& rotateAnchor,
                            QPoint& deleteHandle, QPoint& deleteAnchor) const;
    qreal shapeGrab(const QPoint& center) const;             // grip hit radius (capture px)
    bool beginShapeEdit(const QPoint& g);                    // press on the selection's chrome
    // Wheel width/strength on the selected shape, applied live and banked as ONE undo step
    // when the scroll goes idle (same "apply live, record once" contract as the label wheel).
    void beginShapeStyle(int id);
    void flushShapeStyle();
    // Reflect a selected shape's pen width / redact strength (capture px → logical) into
    // m_style + the PaintBar control, so the toolbar shows the shape's current value.
    void syncPaintBarToShape(const annotation::Annotation* a);
    void applyShapeResize(const QPoint& g, Qt::KeyboardModifiers mods);
    void applyShapeRotate(const QPoint& g, Qt::KeyboardModifiers mods);
    void commitShapeEdit();                                  // record the drag as one undo step

    int m_shapeSel = 0;                  // selected shape id (0 = none)
    int m_shapeHover = 0;                // hovered shape id for the dashed outline (0 = none)
    enum class ShapeEdit { None, Moving, Resizing, Rotating };
    ShapeEdit m_shapeEdit = ShapeEdit::None;
    int m_shapeGrip = -1;                // grip index (0..7) while Resizing
    QVector<QPoint> m_shapeBeforeGeom;   // geometry at gesture start (reshapeApplied)
    qreal m_shapeBeforeRot = 0.0;
    QPoint m_shapeMoveLast;              // last cursor pos during a Move drag
    QPoint m_shapeMoveTotal;             // accumulated Move delta (moveApplied)
    int m_shapeStyleId = 0;              // shape mid wheel width/strength burst (0 = none)
    annotation::Style m_shapeStyleBefore;// its style when the burst began (restyleApplied)
    QTimer* m_shapeStyleFlush = nullptr; // fires once the wheel goes quiet → bank one step

    // Number-label state. The badge being renumbered STAYS in the document and is mutated
    // live for display; `m_labelBefore` snapshots every badge's number when the edit
    // starts, and commit pushes one `AnnotationDocument::setTexts` covering the edited
    // badge *and* any it swapped with — so a renumber is a single Ctrl+Z.
    int m_nextLabel = 1;                         // number the next click will stamp
    int m_labelEditId = 0;                       // badge being renumbered (0 = none)
    QHash<int, QString> m_labelBefore;           // id → number when the edit began
    QString m_labelEditOrig;                     // edited badge's number before the edit
    bool m_labelEditFresh = true;                // no key typed yet → the first one replaces
                                                 // the whole number (select-all-on-focus)
    int m_labelPressId = 0;                      // badge armed on press for click-vs-drag
    QPoint m_labelPressPos;                      // press position (capture px)
    bool m_labelMoved = false;                   // press moved past the drag threshold
    QPoint m_labelDragTotal;                     // drag distance so far — applied live and
                                                 // recorded as ONE undo step on release
    QTimer* m_labelCaret = nullptr;              // blinks the renumber caret
    bool m_labelCaretOn = true;
    int m_labelResizeId = 0;                     // badge mid wheel-resize (0 = none)
    qreal m_labelResizeBefore = 0;               // its fontPx when the burst started
    QTimer* m_labelResizeFlush = nullptr;        // fires once the wheel goes quiet
    QVector<QPoint> m_draftPts;
    QPoint m_draftAnchor;

    // Line tool state machine (see handleLine* above).
    enum class LinePhase { Idle, Building, Controlling };
    LinePhase m_linePhase = LinePhase::Idle;
    QVector<QPoint> m_lineVerts;                     // placed vertices (global logical)
    int m_dragVert = -1;                             // control point being dragged, or -1
    // First-gesture drag detection: a press-drag-release (vs. click) makes a single
    // segment and jumps straight to the control phase, skipping multi-segment building.
    QPoint m_linePressPos;
    bool m_lineDragArmed = false;
    bool m_lineDragging = false;

    // Anti-misclick gate on the fresh-selection drag (see core/ClickSlop.h). The press is
    // held as a click — the auto-detected region stays highlighted and SelectionModel is
    // untouched — until it travels past the slop ladder, at which point the drag begins
    // anchored back at the press point. Only armed when there is a suggestion to fall back
    // to; with detection Off m_pressSlopPx is 0 and the first move drags, as it always did.
    bool m_pressActive = false;
    QPoint m_pressPos;                               // press point (capture px)
    QElapsedTimer m_pressClock;
    int m_pressSlopPx = 0;                           // m_clickSlopPx in capture px (0 = no gate)

    // Read-only move-drag (drag the committed region whole, preserving its size).
    bool m_movingSel = false;
    QPoint m_moveStart;                              // press point (global logical)
    QPoint m_moveOrigin;                            // selection top-left at press

    // Read-only 8-way resize (drag an edge/corner grip).
    bool m_resizing = false;
    Handle m_activeHandle = Handle::None;
    QRect m_resizeOrig;                             // selection rect at grip press

    std::function<void(const QImage&, const QRect&)> m_pinSink; // pin a snip (+ capture geometry)
    std::function<void(const QImage&, const QList<ScreenShot>&, const QRect&)> m_historySink; // record a snip
    std::function<QVector<QImage>()> m_historyThumbs; // history-strip thumbnails (newest first)
    std::function<QImage(int)> m_historyPick;         // cropped snip at an index (picked → clipboard)
    std::function<HistoryFrame(int)> m_historyRestore;// full-screen freeze + area at an index (`,`/`.`)
    std::function<void(const QRect&)> m_areaSink;     // record a successful snip's area
    std::function<QRect(int)> m_areaNav;              // `R`/`Shift+R` reuse-area source
    std::function<void(QString, QString)> m_notifySink; // quick-save toast (title, body)
    void recordHistory(const QImage& img);
    void reuseArea(int dir);

    // Output preferences (from output/*, read from m_appSettings at action time).
    QMap<QString, QString> filenameSpecials() const;        // {"title": foreground window title}
    int outputQuality() const;                              // output/imageQuality (-1 default)
    bool removeAlphaOnSave() const;                          // output/auto_alpha_channel_removal
    PdfEncoder::PageMode pdfPageMode() const;                // output/pdf/pageSize (.pdf saves only)
    // Copy a snip image via the clipboard backend, applying the Output→Clipboard
    // extra-format options (dibScreenshot / copyAsFile); falls back to Qt if no backend.
    void copyImage(const QImage& img, bool asFile = false);
    void autoSaveIfEnabled(const QImage& img);              // output/autoSave: silent copy per snip
    bool m_autoSaved = false;                               // guards one auto-save per snip

    // History strip state (see StripLayout above).
    bool m_stripOpen = false;
    int  m_stripIndex = 0;                            // highlighted entry (0 = newest)
    QVector<QImage> m_stripThumbs;                    // snapshot taken when the strip opens
    QList<ScreenShot> m_liveShots;                    // the live freeze, stashed on first replay
    bool m_liveStashed = false;                       // whether m_liveShots holds the live freeze
    bool m_historyActive = false;                     // showing a restored past desktop (enables re-snip drag)
    void historyStripKey(int older);                  // `,` (older>0) / `.` (older<0)
    void openHistoryStrip();
    void closeHistoryStrip();
    void pickHistoryStrip();                          // copy the highlighted snip + finish
    void applyHistoryFrame(int index);                // restore an entry's full screen + area
    void restoreLiveBackground();                     // step-past-newest → back to the live freeze
};
