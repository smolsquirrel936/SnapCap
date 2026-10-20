#include "core/codec/Quantize.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>
#include <vector>

namespace {

// 5 bits per channel -> 32768 histogram buckets. Bounding the histogram this way
// keeps median cut fast and its memory flat regardless of image size; the lost
// precision is well below what dithering re-introduces. The EXACT path never
// touches this, so lossless images stay lossless.
inline int histKey(int r, int g, int b)
{
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}

// One occupied histogram cell. Sums are of the true 8-bit colours, so a box's
// representative is the real weighted average rather than a bucket centre.
struct Bucket
{
    int    count = 0;
    qint64 sr = 0, sg = 0, sb = 0;
    quint8 r = 0, g = 0, b = 0;      // count-weighted average, for sorting/ranges
};

struct Box
{
    int    begin = 0, end = 0;       // half-open range into the bucket vector
    qint64 count = 0;                // pixels represented
};

int widestChannel(const std::vector<Bucket>& v, const Box& box, int& range)
{
    int mn[3] = { 255, 255, 255 };
    int mx[3] = { 0, 0, 0 };
    for (int i = box.begin; i < box.end; ++i) {
        const int c[3] = { v[i].r, v[i].g, v[i].b };
        for (int k = 0; k < 3; ++k) {
            mn[k] = std::min(mn[k], c[k]);
            mx[k] = std::max(mx[k], c[k]);
        }
    }
    int axis = 0;
    range = mx[0] - mn[0];
    for (int k = 1; k < 3; ++k) {
        if (mx[k] - mn[k] > range) {
            range = mx[k] - mn[k];
            axis = k;
        }
    }
    return axis;
}

QRgb representative(const std::vector<Bucket>& v, const Box& box)
{
    qint64 sr = 0, sg = 0, sb = 0, n = 0;
    for (int i = box.begin; i < box.end; ++i) {
        sr += v[i].sr;
        sg += v[i].sg;
        sb += v[i].sb;
        n  += v[i].count;
    }
    if (n == 0)
        return qRgb(0, 0, 0);
    return qRgb(int(sr / n), int(sg / n), int(sb / n));
}

// Nearest palette entry by squared RGB distance, memoised on the 5-bit key.
// Without the cache this is palette-size comparisons per pixel — 500M+ for a
// full-screen shot — which would make saving a GIF feel hung.
class Nearest
{
public:
    Nearest(const QList<QRgb>& palette, int skipIndex)
        : m_pal(palette), m_skip(skipIndex), m_cache(1 << 15, -1) {}

    int find(int r, int g, int b)
    {
        r = std::clamp(r, 0, 255);
        g = std::clamp(g, 0, 255);
        b = std::clamp(b, 0, 255);
        const int key = histKey(r, g, b);
        if (m_cache[key] >= 0)
            return m_cache[key];

        int best = 0;
        qint64 bestDist = std::numeric_limits<qint64>::max();
        for (int i = 0; i < m_pal.size(); ++i) {
            if (i == m_skip)
                continue;                       // the transparency slot holds no colour
            const qint64 dr = r - qRed(m_pal[i]);
            const qint64 dg = g - qGreen(m_pal[i]);
            const qint64 db = b - qBlue(m_pal[i]);
            const qint64 d = dr * dr + dg * dg + db * db;
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        m_cache[key] = qint16(best);
        return best;
    }

private:
    const QList<QRgb>& m_pal;
    int m_skip;
    std::vector<qint16> m_cache;
};

} // namespace

namespace Quantize {

Indexed toIndexed(const QImage& img, int maxColors)
{
    Indexed out;
    if (img.isNull() || maxColors < 2)
        return out;
    maxColors = std::min(maxColors, 256);

    const QImage src = img.convertToFormat(QImage::Format_ARGB32);
    if (src.isNull())
        return out;
    const int w = src.width();
    const int h = src.height();
    out.width = w;
    out.height = h;
    out.pixels.resize(w * h);

    // --- pass 1: is the image already within budget? ---
    // Bail out as soon as it clearly is not, so a photo does not pay for a set of
    // a million colours before we decide to quantize it anyway.
    QSet<QRgb> uniq;
    bool hasTransparent = false;
    bool overBudget = false;
    for (int y = 0; y < h && !overBudget; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = row[x];
            if (qAlpha(p) < kAlphaThreshold) {
                hasTransparent = true;
                continue;
            }
            uniq.insert(p & 0x00FFFFFF);
            if (uniq.size() > maxColors) {
                overBudget = true;
                break;
            }
        }
    }

    const int reserved = hasTransparent ? 1 : 0;

    // --- exact path: every colour survives untouched ---
    if (!overBudget && uniq.size() <= maxColors - reserved) {
        QList<QRgb> palette(uniq.begin(), uniq.end());
        std::sort(palette.begin(), palette.end());   // deterministic output
        QHash<QRgb, quint8> index;
        index.reserve(palette.size());
        for (int i = 0; i < palette.size(); ++i)
            index.insert(palette[i], quint8(i));

        if (hasTransparent) {
            out.transparentIndex = palette.size();
            palette.append(qRgb(0, 0, 0));           // placeholder; never displayed
        }
        for (int y = 0; y < h; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb p = row[x];
                out.pixels[y * w + x] = qAlpha(p) < kAlphaThreshold
                    ? quint8(out.transparentIndex)
                    : index.value(p & 0x00FFFFFF);
            }
        }
        out.palette = palette;
        out.exact = true;
        return out;
    }

    // --- quantized path: median cut, then Floyd-Steinberg ---
    std::vector<Bucket> hist(1 << 15);
    hasTransparent = false;
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = row[x];
            if (qAlpha(p) < kAlphaThreshold) {
                hasTransparent = true;
                continue;
            }
            Bucket& c = hist[histKey(qRed(p), qGreen(p), qBlue(p))];
            ++c.count;
            c.sr += qRed(p);
            c.sg += qGreen(p);
            c.sb += qBlue(p);
        }
    }

    std::vector<Bucket> buckets;
    buckets.reserve(1024);
    qint64 total = 0;
    for (Bucket& c : hist) {
        if (c.count == 0)
            continue;
        c.r = quint8(c.sr / c.count);
        c.g = quint8(c.sg / c.count);
        c.b = quint8(c.sb / c.count);
        total += c.count;
        buckets.push_back(c);
    }

    const int target = std::max(1, maxColors - (hasTransparent ? 1 : 0));
    std::vector<Box> boxes;
    boxes.push_back({ 0, int(buckets.size()), total });

    while (int(boxes.size()) < target) {
        // Split the box with the most pixels weighted by how spread out it is —
        // splitting a big flat area buys nothing, splitting a big varied one does.
        int pick = -1, pickAxis = 0;
        qint64 bestScore = -1;
        for (int i = 0; i < int(boxes.size()); ++i) {
            if (boxes[i].end - boxes[i].begin < 2)
                continue;
            int range = 0;
            const int axis = widestChannel(buckets, boxes[i], range);
            if (range == 0)
                continue;
            const qint64 score = boxes[i].count * range;
            if (score > bestScore) {
                bestScore = score;
                pick = i;
                pickAxis = axis;
            }
        }
        if (pick < 0)
            break;                       // nothing left worth splitting

        const Box b = boxes[pick];
        std::sort(buckets.begin() + b.begin, buckets.begin() + b.end,
                  [pickAxis](const Bucket& x, const Bucket& y) {
                      const int xc[3] = { x.r, x.g, x.b };
                      const int yc[3] = { y.r, y.g, y.b };
                      return xc[pickAxis] < yc[pickAxis];
                  });

        const qint64 half = b.count / 2;
        qint64 acc = 0;
        int split = b.begin + 1;
        for (int i = b.begin; i < b.end - 1; ++i) {
            acc += buckets[i].count;
            split = i + 1;
            if (acc >= half)
                break;
        }
        qint64 lower = 0;
        for (int i = b.begin; i < split; ++i)
            lower += buckets[i].count;

        boxes[pick] = { b.begin, split, lower };
        boxes.push_back({ split, b.end, b.count - lower });
    }

    QList<QRgb> palette;
    palette.reserve(int(boxes.size()) + 1);
    for (const Box& b : boxes)
        palette.append(representative(buckets, b));
    if (palette.isEmpty())
        palette.append(qRgb(0, 0, 0));               // fully transparent image
    if (hasTransparent) {
        out.transparentIndex = palette.size();
        palette.append(qRgb(0, 0, 0));
    }

    Nearest nearest(palette, out.transparentIndex);

    // Floyd-Steinberg over two running error rows.
    std::vector<int> curr(size_t(w) * 3, 0), next(size_t(w) * 3, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        std::fill(next.begin(), next.end(), 0);
        for (int x = 0; x < w; ++x) {
            const QRgb p = row[x];
            if (qAlpha(p) < kAlphaThreshold) {
                out.pixels[y * w + x] = quint8(out.transparentIndex);
                continue;                            // no colour, so no error to spread
            }
            const int r = std::clamp(qRed(p)   + curr[x * 3 + 0], 0, 255);
            const int g = std::clamp(qGreen(p) + curr[x * 3 + 1], 0, 255);
            const int b = std::clamp(qBlue(p)  + curr[x * 3 + 2], 0, 255);

            const int idx = nearest.find(r, g, b);
            out.pixels[y * w + x] = quint8(idx);

            const int er = r - qRed(palette[idx]);
            const int eg = g - qGreen(palette[idx]);
            const int eb = b - qBlue(palette[idx]);
            const int e[3] = { er, eg, eb };
            for (int k = 0; k < 3; ++k) {
                if (x + 1 < w)
                    curr[(x + 1) * 3 + k] += e[k] * 7 / 16;
                if (x > 0)
                    next[(x - 1) * 3 + k] += e[k] * 3 / 16;
                next[x * 3 + k] += e[k] * 5 / 16;
                if (x + 1 < w)
                    next[(x + 1) * 3 + k] += e[k] * 1 / 16;
            }
        }
        curr.swap(next);
    }

    out.palette = palette;
    out.exact = false;
    return out;
}

} // namespace Quantize
