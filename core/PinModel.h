#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QTransform>
#include <QVector>
#include <memory>

class QJsonObject;
class AnnotationDocument;

// One pinned floating image's state: the source pixels plus the transform stack
// (rotation, flips, scale, opacity) and the optional source text (for the
// "copy plain text" action when the pin was converted from text). compose()
// applies the transforms in a fixed, deterministic order so output is predictable
// and golden-testable. Pure logic (Qt Gui only) — no widgets/Win32. Stage 5.
class PinModel {
public:
    explicit PinModel(const QImage& source, const QString& sourceText = QString());
    ~PinModel();                     // out-of-line: m_annotations is forward-declared

    // --- transforms (all clamped) ---
    void rotateCW();                 // +90°
    void rotateCCW();                // -90°
    void flipHorizontal();
    void flipVertical();
    void toggleGrayscale();
    void toggleInvert();
    void setScale(qreal s);
    void scaleBy(qreal factor);

    // Cap the composed image's longest side (physical px). 0 = unlimited. Feeds the
    // scale clamp so wheel-zoom / border-drag can't grow the pin past this bound
    // (Preferences → Paste → Display → "Max window size").
    void setMaxLongSide(int px);
    int  maxLongSide() const { return m_maxLongSide; }
    void setOpacity(qreal o);
    void adjustOpacity(qreal delta);
    void reset();                    // rotation 0, no flips/color ops, scale 1, opacity 1

    // Bilinear vs. nearest-neighbour filtering when compose() scales the image
    // (Preferences → Paste → Behavior → "Smooth scaling"). Default on.
    void setSmoothScaling(bool on) { m_smoothScaling = on; }
    bool smoothScaling() const { return m_smoothScaling; }

    // The same flag, set from this pin's own Zoom ▸ Smoothing menu item. Marks the pin
    // as overridden, which pins it to the user's choice: a later change to the global
    // paste/behavior/smoothScaling default no longer restyles this pin, and the choice
    // survives a session restore (only an overridden flag is serialized).
    void overrideSmoothScaling(bool on) { m_smoothScaling = on; m_smoothOverridden = true; }
    bool smoothScalingOverridden() const { return m_smoothOverridden; }

    int    rotation() const { return m_rotation; }   // degrees, multiple of 90
    bool   flippedH() const { return m_flipH; }
    bool   flippedV() const { return m_flipV; }
    bool   grayscale() const { return m_grayscale; }
    bool   inverted()  const { return m_inverted; }
    qreal  scale()   const { return m_scale; }
    qreal  opacity() const { return m_opacity; }

    // Replace the underlying image (Ctrl+V), resetting transforms to fit. Clears the
    // source path: after a replace the pixels no longer match the file they came from.
    void setSource(const QImage& source, const QString& sourceText = QString());
    QImage source() const { return m_source; }
    QString sourceText() const { return m_sourceText; }
    bool hasText() const { return !m_sourceText.isEmpty(); }

    // The file this pin's pixels came from / were last written to — set by the pin
    // menu's "Save image as..." and "Replace by file...", and what "View in folder"
    // reveals. Empty = the pin has no file behind it (the usual case for a snip or a
    // clipboard paste), which greys that menu item out.
    QString sourcePath() const { return m_sourcePath; }
    void setSourcePath(const QString& path) { m_sourcePath = path; }

    // --- animation (GIF & friends; frames decoded by core/codec/AnimatedImage) ---
    // The frames replace the source one at a time: advanceFrame() swaps m_source only, so
    // the transform stack, annotations and sourcePath all survive and every frame goes
    // through the same compose(). Ignored for fewer than 2 frames. Frames are NOT
    // serialized — a restored pin re-reads them from sourcePath().
    void setAnimation(const QVector<QImage>& frames, const QVector<int>& delaysMs);
    bool isAnimated() const { return m_frames.size() > 1; }
    int  frameCount() const { return int(m_frames.size()); }
    int  currentFrame() const { return m_frame; }
    int  currentDelayMs() const;     // display duration of the current frame (0 if still)
    void advanceFrame();             // wrap to the next frame; no-op for a still

    // Source devicePixelRatio — how many source px map to one logical px. When >0 the
    // pin's logical size is fixed at source-px / dpr on EVERY monitor (set from the
    // capture DPR for a snip so a pin never rescales to the display's scaling factor on
    // a mixed-DPI layout). 0 = unset: the window falls back to its current screen's dpr
    // (the behavior for clipboard / restored images, which carry no intrinsic dpr).
    qreal sourceDpr() const { return m_sourceDpr; }
    void  setSourceDpr(qreal d) { m_sourceDpr = (d > 0.0 ? d : 0.0); }

    // This pin's annotations, in SOURCE-image pixels. They belong to the image:
    // compose() bakes them onto the source before the transform stack, so they
    // rotate/flip/scale with it (and land in every export, which all go through
    // compose()). Never null; setSource() resets it (new pixels → the old geometry is
    // meaningless). Serialized separately by SessionStore (PinSnapshot::annotations),
    // not by toJson(). Stage 7 (pin toolbar).
    AnnotationDocument* annotations() const { return m_annotations.get(); }

    // The source with the color ops (grayscale/invert) applied. They run BEFORE the
    // annotations bake on purpose, so annotations keep their own colors — a red arrow
    // stays red over a greyed photo. This is the base pixels as displayed, so it (not
    // source()) is what an eraser draft restores. Returns the plain source (no copy)
    // when no color op is on.
    QImage processedSource() const;

    // processedSource() with the committed annotations baked in — what compose() then
    // rotates/flips/scales, and what a live redaction draft samples. Returns
    // processedSource() when there are no annotations.
    QImage annotatedSource() const;

    // color ops → annotations → rotate → flip → scale (opacity is applied by the window,
    // not baked in).
    QImage compose() const;
    QSize  composedSize() const;     // cheap size of compose() without rendering
    QSize  baseSize() const { return m_source.size(); }

    // Maps a point in SOURCE-image px to its place in compose()'s output px — the exact
    // twin of compose()'s rotate → flip → scale, as a transform. The pin editor uses it
    // to paint annotations/drafts over the composed image and (inverted) to turn mouse
    // input back into source px. Uses the unrounded scale(), so it can differ from
    // compose()'s integer pixel rounding by under a pixel.
    QTransform sourceToComposed() const;

    // grouping (Stage 5 manager bookkeeping; UI deferred).
    int  group() const { return m_group; }
    void setGroup(int g) { m_group = g; }

    // Serialize the transform/group state (NOT the image — SessionStore stores the
    // pixels as a sidecar PNG). Round-trips losslessly through fromJson(). Stage 6.
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);   // clamps the same way the setters do

private:
    QImage m_source;
    QVector<QImage> m_frames;         // animation frames (empty = a still); see setAnimation
    QVector<int> m_delaysMs;          // parallel to m_frames
    int m_frame = 0;                  // index into m_frames of the pixels in m_source
    QString m_sourceText;
    QString m_sourcePath;             // backing file (see sourcePath); empty = none
    int   m_rotation = 0;
    bool  m_flipH = false;
    bool  m_flipV = false;
    bool  m_grayscale = false;
    bool  m_inverted = false;
    qreal m_scale = 1.0;
    qreal m_opacity = 1.0;
    qreal m_sourceDpr = 0.0;          // 0 = unset (use the display's dpr)
    int   m_group = 0;
    int   m_maxLongSide = 0;          // 0 = unlimited (see setMaxLongSide)
    bool  m_smoothScaling = true;     // bilinear (true) vs. nearest-neighbour scaling
    bool  m_smoothOverridden = false; // m_smoothScaling came from this pin's own menu
    std::unique_ptr<AnnotationDocument> m_annotations;   // source-px annotations (never null)
};
