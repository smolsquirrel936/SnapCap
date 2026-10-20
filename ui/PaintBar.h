#pragma once

#include <QWidget>
#include <QColor>
#include <QString>
#include <QVector>
#include <QHash>

#include "core/annotation/Annotation.h"   // annotation::ToolId

class QPushButton;
class QSpinBox;
class QButtonGroup;
class QLabel;
class QFrame;
class QComboBox;
class Settings;

// A compact degree slider for redaction. The handle shape encodes the redaction
// type: a ROUND handle for Gaussian blur, a SQUARE handle for mosaic. Drag, click,
// or wheel to change the value. Custom-painted to match the sub-row look.
class RedactSlider : public QWidget {
    Q_OBJECT
public:
    explicit RedactSlider(QWidget* parent = nullptr);
    int value() const { return m_value; }
    void setRange(int lo, int hi);
    void setValue(int v);
    void setSquareHandle(bool square);     // mosaic = square, blur = round
    void setAccent(const QColor& c) { m_accent = c; update(); }   // filled portion + handle
    QSize sizeHint() const override;

signals:
    void valueChanged(int v);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    void setFromX(int x);
    int m_value = 6, m_min = 1, m_max = 20;
    bool m_square = true;                   // default mosaic
    QColor m_accent{0x2d, 0x7d, 0xff};      // set by PaintBar to the UI accent color
};

// The unified annotation toolbar shown over a committed snip
// selection. Two icon rows:
//   Row 1: tool GROUP buttons (shapes / lines / pencil / marker / redact / text /
//          eraser) | undo · redo | cancel · pin · save · copy.
//   Row 2: a per-group panel — the generic panel (variant buttons + pen width +
//          color board) for most tools, or the special REDACT panel (brush-size /
//          rectangle-region mode + blur/mosaic type toggle + degree slider).
// Pure presentation — it raises signals the SnipOverlay controller applies to the
// AnnotationDocument. Icons load from <exe>/icons; labels via i18n keys. Stage 4.
class PaintBar : public QWidget {
    Q_OBJECT
public:
    explicit PaintBar(QWidget* parent = nullptr);

    // Who the bar is editing for. On a **pin** there is no snip to copy/save/pin —
    // the pin's own right-click menu already owns those verbs — so those three buttons
    // are hidden and `cancel` means "close the toolbar" (undo/redo stay). Set once,
    // before the bar is shown; Snip is the default and keeps the full row.
    enum class Host { Snip, Pin };
    void setHost(Host h);
    Host host() const { return m_host; }

    void retranslate();
    void setUndoRedoState(bool canUndo, bool canRedo);

    // OCR mode toggle (row 1). Available in both hosts. It is NOT an annotation tool
    // group — it's mutually exclusive with drawing, so selecting any tool group clears
    // it (and vice-versa). The controller runs recognition + shows the OCR sub-bar on
    // ocrToggled(true). setOcrChecked() lets the controller reflect the button state
    // when it leaves OCR mode by other means (Esc), without re-emitting.
    bool ocrChecked() const;
    void setOcrChecked(bool on);

    // Record mode toggle (row 1, **snip host only**). Deliberately NOT exclusive with
    // the drawing tools — annotating *while* recording is the feature — so selecting a
    // tool leaves this checked and the host stacks the recording bar as a third row.
    // setRecordChecked() reflects state the controller changed itself, without
    // re-emitting; setRecordEnabled(false) greys it out when no recorder backend exists.
    bool recordChecked() const;
    void setRecordChecked(bool on);
    void setRecordEnabled(bool on, const QString& disabledReason = QString());
    // Grey out the redaction group. Used while recording: mosaic/blur sample the image
    // *under* them, and the burn-in layer the recorder composites is transparent there —
    // they would render garbage into the video.
    void setRedactEnabled(bool on, const QString& disabledReason = QString());

    // Scrolling-capture toggle (row 1, **snip host only**, right of OCR). Like the OCR
    // button it is exclusive with drawing — selecting it clears any active tool — but
    // unlike either neighbour it takes the whole session: the host greys the drawing
    // tools out for its duration and they do not come back (the result is far taller
    // than the region, so it cannot be annotated in place).
    // setScrollChecked() un-checks the button when the controller refuses to start a
    // session, without re-emitting.
    void setScrollChecked(bool on);
    // Grey out every drawing tool + undo/redo + the OCR, record AND scroll toggles. A
    // scrolling session takes them at the start and only CANCEL hands them back —
    // Done keeps them off, since the stitched result cannot be annotated in place. The
    // scroll button stays checked while disabled, as the mode indicator. Re-enabling
    // does NOT restore availability the session never took: the caller re-applies that.
    void setToolsEnabledForSession(bool on, const QString& reason = QString());
    // The output block — Copy / Save / Pin / Close. Off for the whole scrolling session,
    // so the entire first-level bar is inert while it runs; Done and Cancel restore it.
    void setOutputEnabled(bool on);

    // The custom ColorDialog (picker-entry block) persists its custom swatches via
    // Settings; the controller supplies the app's instance. Null = no persistence.
    // (The UI accent is read at construction, not here — see m_accent.)
    void setSettings(Settings* s);

    // The 2nd-level toolbar is a SEPARATE floating bar (its own framed row). The
    // controller parents + positions it under this bar; PaintBar owns its content
    // and visibility (shown only while a tool group is selected).
    QWidget* subBar() const { return m_subBar; }
    bool subBarVisible() const { return m_group >= 0; }

    // Live line-width / eraser-size control driven from the editing canvas (mouse
    // wheel over the snip area). Nudges the active tool's matching toolbar control —
    // the width dot/spin box for shapes·lines·pencil·marker, or the eraser dot.
    void wheelAdjustWidth(int steps);

    annotation::ToolId currentTool() const { return m_tool; }
    QColor currentColor() const { return m_color; }
    int penWidth() const;
    int redactStrength() const { return m_redactStrength; }
    bool currentFilled() const { return m_filled; }
    annotation::CapStyle currentStartCap() const { return m_startCap; }
    annotation::CapStyle currentEndCap() const { return m_endCap; }
    bool currentCurved() const { return m_curved; }
    annotation::LineStyle currentLineStyle() const { return m_lineStyle; }
    annotation::LabelStyle currentLabelStyle() const { return m_labelStyle; }
    annotation::NumberStyle currentNumberStyle() const { return m_numberStyle; }

    // Text tool style (read by the overlay to seed a new text box).
    bool currentBold() const { return m_textBold; }
    bool currentItalic() const { return m_textItalic; }
    bool currentHollowText() const { return m_textHollow; }
    QString currentFontFamily() const { return m_fontFamily; }
    qreal currentFontSize() const { return m_fontPx; }
    // Reflect a font size the canvas changed (corner-drag scaling) into the size box
    // without re-emitting fontSizeChanged (avoids a feedback loop).
    void setFontSizeValue(qreal px);
    // Reflect a width / redact strength the canvas changed (selecting or wheel-adjusting a
    // committed shape) into the matching control, without re-emitting the change signal.
    void setPenWidthValue(int w);
    void setRedactStrengthValue(int v);
    // Reflect a numbering style the CANVAS changed — an undo/redo of the retroactive
    // restyle — into the dropdown, without re-emitting numberStyleChanged (which would
    // restyle the badges right back).
    void setNumberStyleValue(annotation::NumberStyle s);

signals:
    void toolSelected(annotation::ToolId tool);
    void colorSelected(const QColor& color);
    void penWidthChanged(int width);
    void fillSelected(bool filled);        // rect/ellipse hollow↔solid toggle
    void startCapChanged(annotation::CapStyle cap);         // line tool: decoration per end
    void endCapChanged(annotation::CapStyle cap);
    void curvedChanged(bool curved);                        // line tool straight↔smooth curve
    void lineStyleChanged(annotation::LineStyle style);     // rect/ellipse/pencil dash pattern
    void labelStyleChanged(annotation::LabelStyle style);   // number-label badge look
    // Number-label numbering glyphs. Emitted ONLY on a real user pick (not when the tool is
    // selected or the setting restored): the hosts apply it to every badge already stamped.
    void numberStyleChanged(annotation::NumberStyle style);
    void boldChanged(bool bold);                            // text tool style toggles
    void italicChanged(bool italic);
    void hollowTextChanged(bool hollow);
    void fontFamilyChanged(const QString& family);
    void fontSizeChanged(qreal px);
    void redactStrengthChanged(int strength);
    void undoRequested();
    void redoRequested();
    void ocrToggled(bool on);              // OCR mode entered/left (row-1 OCR button)
    void recordToggled(bool on);           // record mode entered/left (row-1 record button)
    void scrollToggled(bool on);           // scrolling capture entered/left (row-1 button)
    void copyRequested();
    void saveRequested();
    void pinRequested();
    void cancelRequested();

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;   // wheel on redact buttons

private:
    void pickCustomColor();
    void applyColor(const QColor& c);
    void selectGroup(int groupIndex);
    void deselectTool();                   // clear the active group → read-only (row 2 hidden)
    void selectVariant(annotation::ToolId id);
    // Per-tool pen width, remembered across screenshots/runs (persisted via Settings).
    int  toolWidth(annotation::ToolId id) const;
    void storeToolWidth(annotation::ToolId id, int w);
    // Shapes (rect/ellipse) fill toggle + scroll-to-resize hollow-width dot.
    void selectFill(bool filled);
    void updateFillToggle();               // toggle face/tooltip + dot greyed while filled
    // Dash-pattern dropdown (rect/ellipse/pencil). Remembered per tool, like pen width.
    void showDashCombo(int group);         // (un)show it for a group's current state
    annotation::LineStyle toolLineStyle(annotation::ToolId id) const;
    void storeToolLineStyle(annotation::ToolId id, annotation::LineStyle s);
    void updateHollowIcon();               // redraw the dot to preview the pen width
    // Redact panel helpers.
    void selectRedactMode(bool brush);
    void setRedactType(bool blur);
    void adjustRedactBrush(int step);
    void emitRedactTool();
    void refreshRedactPreview();
    void updateRedactBrushIcon();          // dot that scales with size; number when too big
    // Eraser panel helpers (circle brush + rectangle region).
    void selectEraserMode(bool rect);
    void adjustEraserSize(int step);       // wheel over the circle button resizes it
    void emitEraserTool();
    void updateEraserCircleIcon();         // dot that scales with size; number when too big
    // Marker (highlighter) panel helpers (freehand width dot + rectangle region).
    void selectMarkerMode(bool rect);
    void adjustMarkerWidth(int step);      // wheel over the dot resizes the freehand width
    void emitMarkerTool();
    void updateMarkerDotIcon();            // dot that scales with width; number when too big
    // Text panel helpers (bold/italic/hollow toggles + font family/size + palette).
    void buildTextPanel(class QHBoxLayout* row2);
    void selectTextGroup();                // show the text panel + (re)emit the current style
    void previewFontAt(const QPoint& viewportPos);   // hover specimen for the family list
    void hideFontPreview();
    // Number-label panel helpers (3 badge-look toggles + size box + palette). The badge
    // size IS the digit's font size, so it travels on fontSizeChanged like the text tool.
    void buildLabelPanel(class QHBoxLayout* row2);
    void selectLabelGroup();
    void selectLabelStyle(annotation::LabelStyle s);
    void adjustLabelSize(int steps);       // wheel over the canvas resizes the badge
    void storeLabelPx();                   // persist m_labelPx (survives restarts)
    void updateLabelIcons();               // redraw the 3 variant icons in the current colour
    // Build the shared colour palette (picker-entry block + 2×10 basic grid) into a
    // panel's row layout. Used by both the generic panel and the marker panel.
    void addColorPalette(class QHBoxLayout* into, QWidget* parent);
    void clearOcr();                       // uncheck the OCR button + emit ocrToggled(false)
    QPushButton* makeIconButton(const char* iconName, bool checkable);
    QFrame* makeSeparator();
    static QIcon loadIcon(const char* name);
    // The line mode button's two faces (custom-drawn; there is no icons/curve.svg).
    static QIcon curveIcon();
    static QIcon polylineIcon();
    void updateCurveBtn();                 // sync its icon + tooltip to m_curved
    static QIcon ocrIcon();                // custom-drawn viewfinder-"A" (no icons/ocr.svg)
    static QIcon scrollCaptureIcon();      // custom-drawn page + down arrow (no icons/*.svg)
    static QIcon numberLabelIcon();        // custom-drawn ① badge (no icons/number_label.svg)
    // The picker-entry swatch, drawn as a checkerboard underlay + the colour at its
    // real alpha (so a translucent pick shows its transparency, like the ColorDialog).
    static QPixmap swatchPixmap(const QColor& c, int box);

    // The UI accent (interface/accentColor via m_settings, default #2d7dff) and the bar
    // stylesheet with that accent substituted in for the authored #2d7dff.
    QColor accent() const;
    QString themedBarQss() const;

    struct GroupBtn   { QPushButton* btn; QString tipKey; };
    struct VariantBtn { int group; annotation::ToolId id; QPushButton* btn; QString key; bool hasIcon; };
    QVector<GroupBtn>   m_groupBtns;
    QVector<VariantBtn> m_variantBtns;
    QButtonGroup* m_groupGroup = nullptr;
    QButtonGroup* m_variantGroup = nullptr;
    QFrame* m_variantSep = nullptr;

    QWidget* m_subBar = nullptr;           // the separate 2nd-level toolbar (own frame)
    QWidget* m_genericPanel = nullptr;     // sub-bar contents for normal tools
    QWidget* m_redactPanel = nullptr;      // sub-bar contents for the redact group
    QWidget* m_eraserPanel = nullptr;      // sub-bar contents for the eraser group
    QWidget* m_markerPanel = nullptr;      // sub-bar contents for the marker (highlighter) group
    QWidget* m_textPanel = nullptr;        // sub-bar contents for the text group
    QWidget* m_labelPanel = nullptr;       // sub-bar contents for the number-label group

    // m_hollowBtn is the width dot (shapes, lines, pencil): its size previews the pen
    // width and scrolling over it changes it. The name predates the fill toggle, when
    // it was also the "hollow" half of a hollow/solid radio pair.
    // m_fillBtn (shapes only) is one toggle whose icon shows the current mode --
    // outline or solid, as a square or a circle to match the selected shape.
    QPushButton* m_hollowBtn = nullptr;
    QPushButton* m_fillBtn = nullptr;
    QFrame* m_shapeSep = nullptr;

    // Line-only, in this left-to-right order: start cap · line style · end cap. The line
    // style needs its own combo rather than reusing m_dashCombo below, because it offers
    // two more entries (the tapered styles) and sits in a different slot. Shown only for
    // the lines group.
    QComboBox* m_startCapCombo = nullptr;
    QComboBox* m_lineDashCombo = nullptr;
    QComboBox* m_endCapCombo = nullptr;
    QFrame* m_capSep = nullptr;
    // Line-only: straight-segments ↔ smooth-curve toggle, sitting just left of the
    // width dot. Shown only for the lines group.
    QPushButton* m_curveBtn = nullptr;

    // Rect/ellipse/pencil-only: stroke dash-pattern dropdown, sitting just left of the
    // colour palette. Hidden in solid-fill mode (a filled shape has no stroke to dash).
    QComboBox* m_dashCombo = nullptr;
    QFrame* m_dashSep = nullptr;

    QSpinBox* m_width = nullptr;
    QLabel* m_widthLabel = nullptr;
    QFrame* m_widthSep = nullptr;          // separator after the width spin box
    // Leftmost palette block(s): open ColorDialog + mirror the last-chosen colour. One
    // per palette instance (generic panel + marker panel each have their own).
    QVector<QPushButton*> m_pickerEntries;
    QPushButton* m_undo = nullptr;
    QPushButton* m_redo = nullptr;
    QPushButton* m_ocrBtn = nullptr;       // OCR mode toggle (both hosts)
    QPushButton* m_recordBtn = nullptr;    // record mode toggle (snip host only)
    QPushButton* m_scrollBtn = nullptr;    // scrolling capture toggle (snip host only)
    QPushButton* m_copy = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_pin = nullptr;
    QPushButton* m_cancel = nullptr;

    // Redact controls.
    QButtonGroup* m_redModeGroup = nullptr;
    QPushButton* m_redBrushBtn = nullptr;  // brush-size toggle (point.png + number)
    QPushButton* m_redRectBtn = nullptr;   // rectangle-region toggle
    QPushButton* m_redTypeBtn = nullptr;   // blur/mosaic toggle (live preview icon)
    RedactSlider* m_redSlider = nullptr;
    QLabel* m_redValueLabel = nullptr;

    Settings* m_settings = nullptr;
    Host m_host = Host::Snip;
    QString m_barQss;                      // bar stylesheet template (authored accent #2d7dff)
    QColor m_accent;                       // UI accent (interface/accentColor), read once at ctor
    QHash<int, int> m_toolWidth;           // ToolId → remembered pen width (in-memory cache)
    bool m_suppressWidthStore = false;     // true while loading a tool's width into the spin box
    bool m_suppressCapStore = false;       // true while loading the caps into their combos
    int m_group = -1;
    annotation::ToolId m_tool = annotation::ToolId::None;
    QColor m_color = QColor(0xff, 0x3b, 0x30);
    bool m_filled = false;                 // rect/ellipse: solid vs hollow
    annotation::CapStyle m_startCap = annotation::CapStyle::None;        // line end decorations
    annotation::CapStyle m_endCap = annotation::CapStyle::ArrowSharp;
    bool m_curved = false;                 // line: smooth curve through the vertices
    annotation::LineStyle m_lineStyle = annotation::LineStyle::Solid; // active tool's dash pattern
    QHash<int, int> m_toolLineStyle;       // ToolId → remembered LineStyle (in-memory cache)
    bool m_suppressDashStore = false;      // true while loading a tool's dash into the combo

    bool m_redactBrush = false;            // false = rectangle region (default)
    bool m_redactBlur = false;             // false = mosaic (default)
    int m_redactStrength = 6;
    int m_redBrushSize = 17;

    // Eraser controls.
    QButtonGroup* m_eraseModeGroup = nullptr;
    QPushButton* m_eraseCircleBtn = nullptr;   // circle brush (dot/number + scroll-resize)
    QPushButton* m_eraseRectBtn = nullptr;     // rectangle-region eraser (dotted-rect icon)
    bool m_eraserRect = false;             // false = circle brush (default)
    int m_eraserSize = 24;                 // circle eraser diameter (logical px)

    // Marker (highlighter) controls.
    QButtonGroup* m_markModeGroup = nullptr;
    QPushButton* m_markDotBtn = nullptr;       // freehand highlighter (dot/number + scroll-resize)
    QPushButton* m_markRectBtn = nullptr;      // rectangle highlighter (translucent-rect icon)
    bool m_markerRect = false;             // false = freehand pen (default)
    int m_markerWidth = 4;                 // freehand highlighter base width (logical px)

    // Text controls. The B/I/A icons are custom-drawn (see glyphIcon in the .cpp).
    QPushButton* m_textBoldBtn = nullptr;
    QPushButton* m_textItalicBtn = nullptr;
    QPushButton* m_textHollowBtn = nullptr;
    QComboBox* m_fontFamilyCombo = nullptr;
    QComboBox* m_fontSizeCombo = nullptr;
    QString m_previewedFamily;             // family the hover specimen is currently showing
    bool m_textBold = false;
    bool m_textItalic = false;
    bool m_textHollow = false;
    QString m_fontFamily = QStringLiteral("Consolas");
    qreal m_fontPx = 18.0;
    bool m_suppressFontSize = false;       // true while loading the size box programmatically

    // Number-label controls. The three badge-look buttons are custom-drawn previews that
    // re-render in the current colour (updateLabelIcons), so the toolbar is WYSIWYG too.
    QButtonGroup* m_labelStyleGroup = nullptr;
    QVector<QPushButton*> m_labelBtns;      // index == LabelStyle value
    QComboBox* m_labelSizeCombo = nullptr;
    QComboBox* m_numberCombo = nullptr;     // numbering glyphs (1.2.3 / A.B.C / … )
    annotation::LabelStyle m_labelStyle = annotation::LabelStyle::Filled;
    annotation::NumberStyle m_numberStyle = annotation::NumberStyle::Decimal;
    qreal m_labelPx = 16.0;                 // digit font size; badge diameter = 2x this
    bool m_suppressLabelSize = false;       // true while loading the size box programmatically
    bool m_suppressNumbering = false;       // true while loading the numbering combo ditto
};
