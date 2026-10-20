#pragma once

#include <QWidget>
#include <QColor>
#include <QRect>
#include <QString>
#include <QFont>
#include <QTransform>

class QTextEdit;

// The in-place text editing box shown while the Text tool places/edits a string.
// A frameless container hosting a (transparent) QTextEdit for live typing — caret,
// selection and IME come for free — framed by editing chrome the container paints
// itself: a dashed border, four corner grips that SCALE the font, and a top stalk
// with a rotate handle and a close (×) button. Hovering a control shows a cursor
// matching its function (diagonal resize arrows, a rotate glyph, a pointer). The
// chrome shows only while the box is active; committing (Esc / clicking outside /
// right-click) bakes the text into the AnnotationDocument as a TextShape, while the
// close (×) button discards it.
//
// Rotation is fully WYSIWYG: the whole box (border, handles AND the text) renders
// rotated live about the box centre — exactly as the committed TextShape — while the
// underlying upright QTextEdit, made transparent + click-through, just catches keys.
// The box height auto-grows to fit the wrapped/multi-line text; scrolling the wheel
// scales the font and the box about the box centre.
class TextBoxEditor : public QWidget {
    Q_OBJECT
public:
    explicit TextBoxEditor(QWidget* parent = nullptr);

    // Style, pushed live from the toolbar while the box is active.
    void setTextColor(const QColor& c);
    void setFontFamily(const QString& family);
    void setFontPixelSize(qreal px);
    void setBold(bool b);
    void setItalic(bool b);
    void setHollow(bool h);

    // The content box (the chrome/handle box), in PARENT-local coordinates.
    void setContentBox(const QRect& boxParent);
    QRect contentBox() const { return m_box; }
    // Re-open at a committed shape: position/size the box from the INNER text rect
    // (parent-local) the TextShape stored — the inverse of textRect(). Pairs with
    // setText()/style setters/setRotationDeg() to restore an editor from a TextShape.
    void setTextRect(const QRect& innerParent);
    void setRotationDeg(qreal deg);
    // The inner text area (where glyphs actually lay out), in PARENT-local coords —
    // this is what the committed TextShape uses, so preview and result coincide.
    QRect textRect() const;

    QString text() const;
    void setText(const QString& t);
    void focusEditor();
    void wheelScale(int steps);             // canvas wheel: scale font + box about centre

    qreal fontPixelSize() const { return m_fontPx; }
    qreal rotationDeg() const { return m_rotation; }

signals:
    void committed();                       // Esc / click-outside / right-click → bake
    void canceled();                        // close (×) button → discard
    void fontSizeChanged(qreal px);         // a corner-drag / wheel scaled the font
    void rotationChanged(qreal deg);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;             // scroll = scale font + box
    bool eventFilter(QObject* o, QEvent* e) override;   // Esc / wheel / right-click on the QTextEdit

private:
    enum class Grip { None, TL, TR, BL, BR, Move, Rotate, Close };

    // --- geometry (centred model: the container is a square centred on the box) ----
    int reqRadius() const;                  // container half-size that fits any rotation
    QRect contentRectLocal() const;         // content box in THIS widget's coords
    QRect textRectLocal() const;            // inner text area in THIS widget's coords
    QPoint cornerLocal(Grip g) const;
    QPoint rotateCenterLocal() const;
    QPoint closeCenterLocal() const;
    QTransform rotationXform() const;       // rotate about the content centre
    QPoint toUnrotated(const QPoint& local) const;  // map a local point into the box frame
    Grip gripAt(const QPoint& unrotatedLocal) const;
    void updateMask();                      // clip paint + hit-area to the rotated box

    // --- rendering / sizing --------------------------------------------------------
    QFont previewFont() const;
    void applyFont();
    void applyEditPalette();                // text colour (transparent while rotated)
    void updateRotationVisuals();           // toggle live-render vs. upright QTextEdit
    // WYSIWYG text paint. haloOnly = just the white outline behind the upright
    // QTextEdit's glyphs (so hollow text previews while editing); otherwise the full
    // text (used when rotated, where the QTextEdit is hidden).
    void renderText(QPainter& p, bool haloOnly) const;
    QSize autoContentSize() const;          // box size that fits the un-wrapped text
    void autoFitOnText();                   // refit to the text, top-left anchored (grow →/↓)
    void resizePinned(const QSize& ns, const QPointF& fixedParent, const QPoint& sign);
    void scaleAboutCenter(qreal factor);    // wheel: scale font + box about the centre

    QTextEdit* m_edit = nullptr;
    QRect m_box;                            // content box (parent coords, un-rotated)
    QColor m_accent;                        // UI accent color (interface/accentColor), chrome
    int m_boxLineWidth = 1;                  // dashed box border width = ceil(snip borderWidth / 3)
    QColor m_color = QColor(0xff, 0x3b, 0x30);
    QString m_family = QStringLiteral("Consolas");
    qreal m_fontPx = 18.0;
    bool m_bold = false;
    bool m_italic = false;
    bool m_hollow = false;
    qreal m_rotation = 0.0;
    bool m_selfRender = false;               // true while rotated: paint text ourselves

    // Active drag.
    Grip m_drag = Grip::None;
    QRect m_boxStart;
    qreal m_fontStart = 18.0;
    QPointF m_resizeFixed;                    // resize: pinned (rotated) opposite corner
    QPointF m_resizeDiag;                     // resize: fixed→grabbed corner vector at press
    QPoint m_resizeSign;                      // resize: sign of the pinned corner's offset
    QPoint m_moveStartMouse;                 // move: press point (parent coords)
    QPoint m_moveStartTopLeft;
    QPoint m_rotCenter;                      // rotate: box center (parent coords)
    qreal m_rotStart = 0.0;
    qreal m_rotMouseStart = 0.0;
};
