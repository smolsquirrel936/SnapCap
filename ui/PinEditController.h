#pragma once

#include <QObject>
#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QHash>
#include <QVector>
#include <memory>

#include "core/annotation/Annotation.h"   // annotation::ToolId, Style
#include "ui/OcrTextLayer.h"              // shared OCR selection/paint layer (source-px)

class PinWindow;
class PinModel;
class PaintBar;
class OcrBar;
class TextBoxEditor;
class Settings;
class QPainter;
class QWidget;
class QTimer;
namespace platform { class IOcr; }

// The annotation toolbar + drawing state machine for ONE pin (Space, or the pin menu's
// "Show toolbar"). The pin-side counterpart of the drafting glue inside SnipOverlay —
// deliberately a separate controller rather than a shared one, because the snip path is
// welded to frozen screens, a SelectionModel and the mixed-DPI capture-px space, none of
// which exist here. What IS shared is the real machinery: core/AnnotationDocument (owned
// by the PinModel) and ui/PaintBar (hosted with Host::Pin, which hides the snip-only
// pin/save/copy buttons).
//
// Coordinates: annotations live in SOURCE-image px (PinModel::annotations()), so every
// mapping goes through PinWindow::sourceToWidget() and its inverse. Committed annotations
// are painted by PinModel::compose() (baked onto the source before the transform stack);
// only the live draft is drawn on top, by paintDraft().
//
// Lifetime: the controller owns only the toolbar + the in-place text editor, never the
// document — so closing the toolbar keeps the pin's annotations AND its undo history.
// Only one may be open at a time (see PinWindow::s_editing). Stage 7 (pin toolbar).
class PinEditController : public QObject {
    Q_OBJECT
public:
    PinEditController(PinWindow* pin, Settings* settings);
    ~PinEditController() override;

    // Input, in the pin's widget-local (logical) coords. Each returns true when it
    // consumed the event — the pin falls back to its normal move/resize/menu behavior
    // when they don't (i.e. whenever no tool is selected).
    bool onPress(const QPoint& local, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    bool onMove(const QPoint& local, Qt::KeyboardModifiers mods);
    bool onRelease(const QPoint& local);
    bool onWheel(int steps);
    // Consumed? With the number-label tool every click is a discrete action, and Qt sends
    // a double-click INSTEAD of the second press — so this forwards it as an ordinary
    // click (a fast +/− tap would otherwise be dropped) and keeps it from reaching the
    // pin's own "double-click closes" rule.
    bool onDoubleClick(const QPoint& local);                 // tool active → nudge the pen/eraser width
    bool onKey(int key, Qt::KeyboardModifiers mods);

    // True while a drawing tool is selected. With no tool the toolbar is just "open" and
    // the pin still drags/resizes normally (mirrors the snip overlay's read-only mode).
    bool toolActive() const { return m_tool != annotation::ToolId::None; }
    bool drawing() const { return m_drawing || m_linePhase != LinePhase::Idle; }

    // OCR text-selection mode (toolbar OCR button): the pin becomes a read-only text
    // surface — grey word plates + drag-to-select over the whole pin. Mutually exclusive
    // with the drawing tools.
    bool ocrActive() const { return m_ocrActive; }

    // The live draft, over the already-composed image. `p` is in widget coords; this
    // applies the source→widget transform itself.
    void paintDraft(QPainter& p) const;
    // Cursor for a widget point while a tool is active (crosshair, or hidden where the
    // canvas draws its own brush preview). Qt::ArrowCursor = "not mine".
    Qt::CursorShape cursorFor(const QPoint& local) const;

    void reposition();                       // re-anchor the bars to the pin's frame
    void setBarsVisible(bool on);            // hidden during a drag/draw

    // Bake an open text box before the toolbar is torn down, so closing it doesn't
    // silently eat what was typed. Call while the pin is still alive — the destructor
    // deliberately does NOT (it runs when the pin itself is going away).
    void finishEditing();

signals:
    void closeRequested();                   // cancel button / Esc → the pin tears us down

private:
    void wireBar();
    void setTool(annotation::ToolId t);

    // ---- OCR mode ------------------------------------------------------------------
    void toggleOcr(bool on);                 // enter/leave OCR (recognizes on enter)
    void startOcrRecognition();              // OCR the whole pin off the UI thread
    void onOcrReady(const platform::OcrResult& r);   // posted back to the UI thread
    void copyOcrText();                      // selection (or all) → clipboard as text
    void startDraft(const QPoint& src);
    void updateDraft(const QPoint& src, Qt::KeyboardModifiers mods);
    void commitDraft();
    void syncUndoRedo();
    void refresh();                          // recompose + repaint the pin

    // Style with pen/font/redact sizes converted from on-screen logical px into SOURCE
    // px, so a stroke drawn at width N looks N px wide on screen right now (and then
    // scales with the image, like the pixels it sits on).
    annotation::Style sourceStyle() const;
    qreal sourcePerScreen() const;           // source px per screen-logical px

    QPoint toSource(const QPoint& local) const;

    // ---- Line tool: build → control → finalize (mirrors SnipOverlay's state machine) --
    bool isLineTool() const { return m_tool == annotation::ToolId::Line; }
    // Shift snaps the segment ending at `src` to 45° steps, measured from `from`.
    static QPoint lineSnap(const QPoint& from, const QPoint& src, Qt::KeyboardModifiers mods);
    void handleLinePress(const QPoint& src, Qt::KeyboardModifiers mods);
    void handleLineMove(const QPoint& src, Qt::KeyboardModifiers mods);
    void handleLineRelease(const QPoint& src, Qt::KeyboardModifiers mods);
    bool handleLineSecondary();              // right-click: advance/finalize; consumed?
    void rebuildLineDraft(const QPoint* floating);
    int  lineControlHit(const QPoint& src) const;
    void resetLineState();

    // ---- Text tool (place-only: pin text is NOT re-editable once committed) ----------
    void placeText(const QPoint& local);
    void commitTextEditor();                 // bake the open box into the document
    void cancelTextEditor();                 // discard it
    bool textEditing() const { return m_textEditor != nullptr; }
    // The rotation to STORE so the text reads at `screenDeg` on screen, given the pin's
    // current orientation — derived from the live transform rather than case analysis,
    // so it cannot drift from compose().
    qreal sourceRotationFor(qreal screenDeg) const;
    bool  mirroredForPin() const;            // pin flips the text? (odd number of mirrors)

    // ---- Number label (mirrors SnipOverlay's interaction model) ----------------------
    // Click stamps the next number; clicking a committed badge renumbers it in place
    // (digits go straight to the controller, no editor widget), drag moves it, wheel
    // resizes it. The badge under edit is pulled out of the document into m_draft and
    // re-added on commit, so undo stays coherent.
    annotation::Annotation* labelAt(const QPoint& src) const;
    annotation::Annotation* editedLabel() const;
    bool handleLabelPress(const QPoint& src);
    void handleLabelRelease();
    void placeLabel(const QPoint& src);
    void beginLabelEdit(annotation::Annotation* a);
    void commitLabelEdit();
    void cancelLabelEdit();
    bool handleLabelEditKey(int key);
    // Step the edited badge's number by ±1, swapping with whichever badge holds the
    // target so the other badges keep their relative order. Clamped to their range.
    void stepLabel(int delta);
    // Wheel-resizing a committed badge is a burst of events with no release, so the size
    // is applied live and recorded as ONE undo step when the burst goes idle (or when a
    // click/tool change/undo would make a late flush confusing).
    void beginLabelResize(int id);
    void flushLabelResize();
    bool labelCanStep(int delta) const;
    QVector<QRect> labelStepButtons() const;   // [0] = plus, [1] = minus (source px)
    QRect labelDeleteButton() const;           // × , centred above the outline (source px)
    // Delete the badge being edited (× / Del). Badges above it drop by one so numbering
    // stays contiguous — that, plus this session's renumbering, as ONE undo step.
    void deleteLabel();
    int labelCount() const;                  // badges currently in the document
    // Step the counter by however many badges an undo/redo added or removed, so Ctrl+Z on a
    // stamp also takes the number back. Call with labelCount() sampled BEFORE the undo/redo.
    void syncNextLabel(int labelsBefore);
    // Adopt the numbering style the badges now carry (an undo/redo can revert the
    // retroactive restyle), reflecting it into the toolbar without re-emitting.
    void syncNumberStyleFromDoc();
    // Numbering glyphs. Retroactive, unlike the other style setters: the badges form one
    // sequence, so this restyles the ones already stamped too (as one undo step).
    void setNumberStyle(annotation::NumberStyle s);
    void refreshLabelCursor();               // re-pick the cursor without a mouse move
    void restartLabelCaret();                // solid caret + restart the blink while typing
    bool labelTool() const { return m_tool == annotation::ToolId::NumberLabel; }

    PinWindow* m_pin = nullptr;
    PinModel* m_model = nullptr;
    Settings* m_settings = nullptr;

    PaintBar* m_bar = nullptr;
    QWidget*  m_subBar = nullptr;            // reparented out of the bar → we delete it

    // OCR state. m_ocr is the app-wide backend (PinWindow::ocr(), may be null); m_ocrBar
    // is the 2nd-level toolbar (own top-level window, like m_subBar); m_ocrLayer holds the
    // recognized text + selection in SOURCE-image px (mapped to widget via sourceToWidget).
    platform::IOcr* m_ocr = nullptr;
    OcrBar*     m_ocrBar = nullptr;
    OcrTextLayer m_ocrLayer;
    bool        m_ocrActive = false;
    QString     m_ocrLang;

    annotation::ToolId m_tool = annotation::ToolId::None;
    annotation::Style m_style;               // in SCREEN-logical units (as the bar reports)
    std::unique_ptr<annotation::Annotation> m_draft;
    bool m_drawing = false;
    QVector<QPoint> m_draftPts;              // source px
    QPoint m_draftAnchor;
    // The source pixels a redaction/eraser draft samples, snapshotted when the stroke
    // starts (the committed annotations can't change mid-drag): base = the CLEAN source
    // so an eraser restores real pixels, redactSource = source + committed annotations.
    QImage m_draftClean;
    QImage m_draftComposite;

    enum class LinePhase { Idle, Building, Controlling };
    LinePhase m_linePhase = LinePhase::Idle;
    QVector<QPoint> m_lineVerts;             // source px
    int m_dragVert = -1;
    QPoint m_linePressPos;
    bool m_lineDragArmed = false;
    bool m_lineDragging = false;

    // Number-label state (see the helpers above). Geometry here is source px; m_lastLocal
    // is widget-local, used to draw the WYSIWYG badge preview under the cursor.
    // The badge being renumbered STAYS in the document and is mutated live for display;
    // m_labelBefore snapshots every badge's number so commit can push ONE undoable
    // setTexts covering the edited badge and anything it swapped with.
    int m_nextLabel = 1;                     // number the next click stamps
    int m_labelEditId = 0;                   // badge being renumbered (0 = none)
    QHash<int, QString> m_labelBefore;       // id → number when the edit began
    QString m_labelEditOrig;                 // edited badge's number before the edit
    bool m_labelEditFresh = true;            // first key replaces the whole number
    int m_labelPressId = 0;                  // badge armed on press for click-vs-drag
    QPoint m_labelPressSrc;
    bool m_labelMoved = false;
    QPoint m_labelDragTotal;                 // drag distance so far — applied live and
                                             // recorded as ONE undo step on release
    QVector<QPoint> m_labelHoverOutline;     // badge under the cursor (source px)
    QPoint m_lastLocal;
    bool m_haveLocal = false;
    QTimer* m_labelCaret = nullptr;          // blinks the renumber caret
    bool m_labelCaretOn = true;
    int m_labelResizeId = 0;                 // badge mid wheel-resize (0 = none)
    qreal m_labelResizeBefore = 0;           // its fontPx when the burst started
    QTimer* m_labelResizeFlush = nullptr;    // fires once the wheel goes quiet

    QPointer<TextBoxEditor> m_textEditor;
};
