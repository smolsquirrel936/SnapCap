#include "core/render/Raster.h"
#include "core/render/GpuBlur.h"

#include <QtMath>
#include <vector>
#include <cmath>

namespace render {

QImage mosaic(const QImage& src, int block)
{
    if (src.isNull() || block <= 1)
        return src;

    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();

    for (int by = 0; by < h; by += block) {
        const int y1 = qMin(by + block, h);
        for (int bx = 0; bx < w; bx += block) {
            const int x1 = qMin(bx + block, w);
            quint64 r = 0, g = 0, b = 0, a = 0;
            int n = 0;
            for (int y = by; y < y1; ++y) {
                const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = bx; x < x1; ++x) {
                    const QRgb px = line[x];
                    r += qRed(px); g += qGreen(px); b += qBlue(px); a += qAlpha(px);
                    ++n;
                }
            }
            if (n == 0) continue;
            const QRgb avg = qRgba(int(r / n), int(g / n), int(b / n), int(a / n));
            for (int y = by; y < y1; ++y) {
                QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
                for (int x = bx; x < x1; ++x)
                    line[x] = avg;
            }
        }
    }
    return img;
}

namespace {

// One separable pass. `horizontal` picks the axis; clamps at edges.
QImage blurPass(const QImage& in, const std::vector<float>& k, bool horizontal)
{
    const int w = in.width();
    const int h = in.height();
    const int radius = int(k.size()) / 2;
    QImage out(w, h, QImage::Format_ARGB32);

    for (int y = 0; y < h; ++y) {
        const QRgb* srcLine = reinterpret_cast<const QRgb*>(in.constScanLine(y));
        QRgb* dstLine = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int t = -radius; t <= radius; ++t) {
                int sx = x, sy = y;
                if (horizontal) sx = qBound(0, x + t, w - 1);
                else            sy = qBound(0, y + t, h - 1);
                const QRgb px = horizontal
                    ? srcLine[sx]
                    : reinterpret_cast<const QRgb*>(in.constScanLine(sy))[x];
                const float wgt = k[t + radius];
                r += qRed(px) * wgt; g += qGreen(px) * wgt;
                b += qBlue(px) * wgt; a += qAlpha(px) * wgt;
            }
            dstLine[x] = qRgba(int(std::lround(r)), int(std::lround(g)),
                               int(std::lround(b)), int(std::lround(a)));
        }
    }
    return out;
}

} // namespace

QImage gaussianBlur(const QImage& src, int radius)
{
    if (src.isNull() || radius <= 0)
        return src;

    const double sigma = qMax(0.5, radius / 2.0);
    std::vector<float> kernel(2 * radius + 1);
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float v = float(std::exp(-(i * i) / (2.0 * sigma * sigma)));
        kernel[i + radius] = v;
        sum += v;
    }
    for (float& v : kernel) v /= sum;

    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    img = blurPass(img, kernel, true);
    img = blurPass(img, kernel, false);
    return img;
}

// --- box blur ---------------------------------------------------------------

namespace {

// One box pass along one axis, via a running sum, so cost is independent of `r`.
// Raw base pointer + element stride keeps scanLine() out of the inner loop:
// `step` walks along the blur axis, `line` between lines/columns.
void boxPass(const QImage& in, QImage& out, int r, bool horizontal)
{
    const int n = 2 * r + 1;
    const int outer = horizontal ? in.height() : in.width();
    const int inner = horizontal ? in.width() : in.height();
    const int srcRow = int(in.bytesPerLine() / 4);
    const int dstRow = int(out.bytesPerLine() / 4);
    const QRgb* srcBase = reinterpret_cast<const QRgb*>(in.constBits());
    QRgb* dstBase = reinterpret_cast<QRgb*>(out.bits());
    const int srcStep = horizontal ? 1 : srcRow;
    const int dstStep = horizontal ? 1 : dstRow;
    const int srcLine = horizontal ? srcRow : 1;
    const int dstLine = horizontal ? dstRow : 1;

    for (int o = 0; o < outer; ++o) {
        const QRgb* s = srcBase + qsizetype(o) * srcLine;
        QRgb* d = dstBase + qsizetype(o) * dstLine;
        // Edges clamp: the window is pre-filled with the first pixel repeated,
        // and steps past either end reuse the first/last pixel.
        const QRgb first = s[0], last = s[qsizetype(inner - 1) * srcStep];

        int sr = 0, sg = 0, sb = 0, sa = 0;
        for (int t = -r; t <= r; ++t) {
            const QRgb px = (t < 0) ? first : s[qsizetype(qMin(t, inner - 1)) * srcStep];
            sr += qRed(px); sg += qGreen(px); sb += qBlue(px); sa += qAlpha(px);
        }
        // Round, don't truncate. Truncation loses ~0.5 per pass in one direction,
        // and with six passes (3 iterations x 2 axes) that compounds into a visible
        // systematic darkening -- measured 4 levels against the GPU, which rounds
        // when it writes RGBA8. All sums are non-negative, so +n/2 is round-half-up.
        const int half = n / 2;
        for (int i = 0; i < inner; ++i) {
            d[qsizetype(i) * dstStep] = qRgba((sr + half) / n, (sg + half) / n,
                                              (sb + half) / n, (sa + half) / n);
            const int ri = i - r, ai = i + r + 1;
            const QRgb rem = (ri < 0) ? first : s[qsizetype(ri) * srcStep];
            const QRgb add = (ai > inner - 1) ? last : s[qsizetype(ai) * srcStep];
            sr += qRed(add)   - qRed(rem);
            sg += qGreen(add) - qGreen(rem);
            sb += qBlue(add)  - qBlue(rem);
            sa += qAlpha(add) - qAlpha(rem);
        }
    }
}

} // namespace

std::array<int, 3> boxRadii(int radius)
{
    // Kutskir's method: the box widths whose triple convolution best matches a
    // Gaussian of this sigma. `m` of the three passes use the smaller width.
    const double sigma = qMax(0.5, radius / 2.0);
    const int n = 3;
    const double wIdeal = std::sqrt(12.0 * sigma * sigma / n + 1.0);
    int wl = int(std::floor(wIdeal));
    if (wl % 2 == 0) --wl;
    const int wu = wl + 2;
    const double mIdeal = (12.0 * sigma * sigma - double(n) * wl * wl - 4.0 * n * wl - 3.0 * n)
                          / (-4.0 * wl - 4.0);
    const int m = int(std::lround(mIdeal));

    std::array<int, 3> radii{};
    for (int i = 0; i < n; ++i)
        radii[size_t(i)] = qMax(1, ((i < m ? wl : wu) - 1) / 2);
    return radii;
}

QImage boxBlur(const QImage& src, int radius)
{
    if (src.isNull() || radius <= 0)
        return src;

    QImage a = src.convertToFormat(QImage::Format_ARGB32);
    QImage b(a.width(), a.height(), QImage::Format_ARGB32);
    for (const int r : boxRadii(radius)) {
        boxPass(a, b, r, true);
        boxPass(b, a, r, false);
    }
    return a;
}

// --- blur dispatch ----------------------------------------------------------

namespace {
BlurMode g_blurMode = BlurMode::Cpu;   // see the header: GUILESS tests reach here
}

void     setBlurMode(BlurMode mode) { g_blurMode = mode; }
BlurMode blurMode()                 { return g_blurMode; }

QImage blur(const QImage& src, int radius)
{
    if (src.isNull() || radius <= 0)
        return src;

    if (g_blurMode == BlurMode::Auto) {
        // Null = unavailable or failed; gpuBoxBlur has already logged why (once).
        const QImage out = gpuBoxBlur(src, radius);
        if (!out.isNull())
            return out;
    }
    return boxBlur(src, radius);
}

} // namespace render
