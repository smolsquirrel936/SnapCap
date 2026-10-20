#include "ui/ScrollStripWindow.h"

#include <QPainter>
#include <QtGlobal>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

// Thickness of the "there is more this way" band at an overflowing end, logical px.
static constexpr int kChevronBand = 14;

ScrollStripWindow::ScrollStripWindow(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint
                      | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);   // never steals a scroll
    setFocusPolicy(Qt::NoFocus);
}

bool ScrollStripWindow::setExcludedFromCapture(bool on)
{
#ifdef Q_OS_WIN
    // Windows 10 2004+; older SDK headers may not declare it. Verified by probe (Win11
    // 26200) to hide the window from the GDI BitBlt path this feature grabs with, plain
    // and layered windows alike: the grab came out pixel-identical to the window never
    // having existed — not black. That is what makes the overlapping-the-region
    // placement safe. Re-check it if the grab backend changes (DXGI, Stage 7 Phase D);
    // the remedy would be hide → grab → show, for that placement only.
#  ifndef WDA_EXCLUDEFROMCAPTURE
#    define WDA_EXCLUDEFROMCAPTURE 0x00000011
#  endif
    createWinId();
    return SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()),
                                    on ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE) != FALSE;
#else
    Q_UNUSED(on);
    return false;
#endif
}

void ScrollStripWindow::setCanvas(const QImage& canvas, int liveOffset, int liveHeight)
{
    m_canvas = canvas;
    m_liveOffset = liveOffset;
    m_liveHeight = liveHeight;
    update();
}

void ScrollStripWindow::setWarning(bool on)
{
    if (m_warning == on)
        return;
    m_warning = on;
    update();
}

void ScrollStripWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1e, 0x1e, 0x1e));

    if (!m_canvas.isNull() && m_canvas.width() > 0) {
        // Everything below works in CANVAS space — across the strip is the canvas's
        // width, along it is the growth axis — and a horizontal strip simply rotates
        // that space back at the end. Rotating the painter rather than the image is
        // what keeps this cheap: only the visible slice is ever transformed, never the
        // whole (possibly 30000 px) canvas.
        const int across = m_horizontal ? height() : width();
        const int along  = m_horizontal ? width()  : height();
        // Fixed scale: the canvas width always maps across the strip, whatever the
        // result grows to.
        const double scale = double(across) / m_canvas.width();
        const int visible = qMax(1, int(along / scale));      // canvas rows that fit

        // Follow the live frame, clamped to the image: appending pins the view to the
        // newest end, prepending to the oldest, and scrolling back walks it between.
        const int liveCenter = m_liveOffset + m_liveHeight / 2;
        int top = liveCenter - visible / 2;
        top = qBound(0, top, qMax(0, m_canvas.height() - visible));

        p.save();
        if (m_horizontal) {
            // Canvas (x across, y along) -> widget (x along, y across), so the capture
            // reads left to right and keeps the orientation the frames were rotated out
            // of. Mirrors the -90° the result itself is rotated by at Done.
            p.translate(0, across);
            p.rotate(-90);
        }
        const QRect src(0, top, m_canvas.width(), qMin(visible, m_canvas.height() - top));
        const QRectF dst(0, 0, across, src.height() * scale);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(dst, m_canvas, src);

        // Where the live region sits in what is shown. The only state worth suppressing is
        // the FIRST frame, where the live region is the whole canvas and the marker would
        // just trace the window's own border. From the second frame on it always marks a
        // real sub-range — including while the strip is still growing, which is most of a
        // short capture — so it is drawn there too.
        const QRectF live(0, (m_liveOffset - top) * scale,
                          across, m_liveHeight * scale);
        const bool liveIsWholeCanvas = m_liveOffset <= 0 && m_liveHeight >= m_canvas.height();
        if (!liveIsWholeCanvas && live.bottom() > 0 && live.top() < along) {
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QPen(m_accent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(live.adjusted(1, 1, -1, -1));
        }

        // An end with image beyond it gets a chevron, so a strip showing a slice is never
        // mistaken for the whole capture and it is obvious which way the rest lies. Each
        // is independent: both while the viewport sits in the middle, neither while the
        // whole canvas still fits (the growing phase pins top to 0 and shows every row).
        // Drawn in canvas space with everything else, so a horizontal session's arrows
        // come out pointing left and right without a second case.
        const int shownRows = src.height();
        auto chevron = [&](double bandTop, bool pointBack) {
            p.fillRect(QRectF(0, bandTop, across, kChevronBand), QColor(0, 0, 0, 140));
            const double cx = across / 2.0;
            const double cy = bandTop + kChevronBand / 2.0;
            const double w = 5.0, h = 3.0;                 // half-width, half-height
            QPolygonF v;
            if (pointBack)
                v << QPointF(cx - w, cy + h) << QPointF(cx, cy - h) << QPointF(cx + w, cy + h);
            else
                v << QPointF(cx - w, cy - h) << QPointF(cx, cy + h) << QPointF(cx + w, cy - h);
            // White on its own dark band rather than the accent on the image: the strip
            // shows arbitrary page content underneath, so the marker has to bring its own
            // contrast instead of borrowing the screenshot's.
            p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(v);
        };
        // Measured in DRAWN px, not in rows. The window is sized to `canvasHeight * scale`
        // rounded to whole px and `visible` truncates on the way back, so during the
        // growing phase the row counts disagree by one or two with not a pixel of image
        // actually off the end — comparing them directly put a chevron on a strip that was
        // showing everything (user-reported 2026-08-09). An end overflows when what it
        // hides would be big enough to see.
        constexpr double kHiddenPx = 2.0;
        p.setRenderHint(QPainter::Antialiasing, true);
        if (top * scale >= kHiddenPx)
            chevron(0, /*pointBack*/ true);
        if ((m_canvas.height() - top - shownRows) * scale >= kHiddenPx)
            chevron(shownRows * scale - kChevronBand, /*pointBack*/ false);
        p.restore();
    }

    // Border last, over the content: red while the last frame could not be placed.
    p.setPen(QPen(m_warning ? QColor(0xc0, 0x39, 0x2b) : QColor(0x80, 0x80, 0x80),
                  m_warning ? 3 : 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(1, 1, -2, -2));
}
