#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>

// The scrolling-capture preview strip: a narrow always-on-top window beside the capture
// region showing the image stitched so far, so you can see the result growing as you
// scroll. Frameless, focus-less, and EXCLUDED FROM CAPTURE — which is what lets it be
// placed overlapping the region when there is no room on either side (a fullscreen
// selection) without appearing in the grabs.
//
// Fixed scale, viewport follows the live frame: the strip does not shrink as the
// result grows — it shows the newest end, and scrolling back scrolls the strip back,
// with an indicator marking where the live region sits in the whole image. The window
// itself is sized to the canvas by SnipOverlay::scrollStripGeometry() until it reaches
// the region's length, so nothing here has to fill space the capture has not reached.
// The indicator is drawn from the second frame on — on the first there is nothing to
// mark, the live region being the whole canvas. An end with image beyond it also gets a
// chevron, so a strip showing a slice is never read as the whole capture.
class ScrollStripWindow : public QWidget {
    Q_OBJECT
public:
    // Logical size ACROSS the strip — its width when the capture grows downward, its
    // height when it grows sideways. The scale follows from it: canvasWidth -> this.
    static constexpr int kThickness = 120;

    explicit ScrollStripWindow(QWidget* parent = nullptr);

    void setAccent(const QColor& c) { m_accent = c; update(); }
    // Lay the strip out along the capture axis. A horizontal session stitches ROTATED
    // frames, so the canvas below is still a tall image whichever axis is in
    // play — this only decides how it is drawn back.
    void setHorizontal(bool on) { m_horizontal = on; update(); }
    // The stitched image so far, plus where the live frame sits in it (capture px).
    // Both are in canvas space, i.e. rotated for a horizontal session.
    void setCanvas(const QImage& canvas, int liveOffset, int liveHeight);
    // Red state while the last frame could not be matched.
    void setWarning(bool on);
    // Keep the window out of every screen grab. False when the OS is too old.
    bool setExcludedFromCapture(bool on);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QImage m_canvas;
    int    m_liveOffset = 0;
    int    m_liveHeight = 0;
    bool   m_warning = false;
    bool   m_horizontal = false;
    QColor m_accent{0x2d, 0x7d, 0xff};
};
