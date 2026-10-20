#include "core/codec/GifEncoder.h"

#include "core/codec/Quantize.h"

#include <QByteArray>
#include <QFile>

#include <algorithm>
#include <vector>

namespace {

void appendLe16(QByteArray& a, int v)
{
    a.append(char(v & 0xFF));
    a.append(char((v >> 8) & 0xFF));
}

// GIF image data is a stream of variable-width LZW codes packed LSB-first, then
// cut into sub-blocks of at most 255 bytes each.
class BitPacker
{
public:
    explicit BitPacker(QByteArray& out) : m_out(out) {}

    void write(int code, int bits)
    {
        m_acc |= quint32(code) << m_nbits;
        m_nbits += bits;
        while (m_nbits >= 8) {
            m_block.append(char(m_acc & 0xFF));
            m_acc >>= 8;
            m_nbits -= 8;
            if (m_block.size() == 255)
                flushBlock();
        }
    }

    void finish()
    {
        if (m_nbits > 0) {                       // pad the final partial byte
            m_block.append(char(m_acc & 0xFF));
            m_acc = 0;
            m_nbits = 0;
            if (m_block.size() == 255)
                flushBlock();
        }
        flushBlock();
        m_out.append(char(0));                   // block terminator
    }

private:
    void flushBlock()
    {
        if (m_block.isEmpty())
            return;
        m_out.append(char(m_block.size()));
        m_out.append(m_block);
        m_block.clear();
    }

    QByteArray& m_out;
    QByteArray  m_block;
    quint32     m_acc = 0;
    int         m_nbits = 0;
};

void lzwCompress(const QList<quint8>& pixels, int minCodeSize, QByteArray& out)
{
    const int clearCode = 1 << minCodeSize;
    const int endCode   = clearCode + 1;

    BitPacker bits(out);
    // (prefix, next-byte) -> code. A flat table beats a hash here, and it is
    // qint16 rather than int because it gets refilled on every clear code.
    std::vector<qint16> dict(size_t(4096) * 256, -1);

    int codeSize = minCodeSize + 1;
    int nextCode = endCode + 1;
    bits.write(clearCode, codeSize);

    if (pixels.isEmpty()) {
        bits.write(endCode, codeSize);
        bits.finish();
        return;
    }

    int prefix = pixels[0];
    for (int i = 1; i < pixels.size(); ++i) {
        const int k = pixels[i];
        const size_t slot = size_t(prefix) * 256 + size_t(k);
        if (dict[slot] >= 0) {
            prefix = dict[slot];
            continue;
        }
        bits.write(prefix, codeSize);

        // Widen (or reset) BEFORE assigning a code that would not fit. The decoder
        // grows its own table on exactly this boundary, so the two must agree.
        if (nextCode == (1 << codeSize)) {
            if (codeSize < 12) {
                ++codeSize;
            } else {
                bits.write(clearCode, codeSize);
                std::fill(dict.begin(), dict.end(), qint16(-1));
                codeSize = minCodeSize + 1;
                nextCode = endCode + 1;
                prefix = k;
                continue;
            }
        }
        dict[slot] = qint16(nextCode++);
        prefix = k;
    }

    bits.write(prefix, codeSize);
    bits.write(endCode, codeSize);
    bits.finish();
}

} // namespace

namespace GifEncoder {

bool write(const QImage& img, const QString& path, QString* error)
{
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    if (img.isNull())
        return fail(QStringLiteral("The image is empty."));
    if (img.width() > kMaxSide || img.height() > kMaxSide)
        return fail(QStringLiteral("GIF cannot store images larger than %1 pixels on a side.")
                        .arg(kMaxSide));

    const Quantize::Indexed q = Quantize::toIndexed(img, 256);
    if (q.palette.isEmpty() || q.pixels.isEmpty())
        return fail(QStringLiteral("The image could not be reduced to a GIF palette."));

    // A GIF colour table holds 2^(n+1) entries, so it is padded to a power of two.
    int n = 0;
    while ((1 << (n + 1)) < q.palette.size() && n < 7)
        ++n;
    const int tableEntries = 1 << (n + 1);
    const int minCodeSize = std::max(2, n + 1);   // the format's floor is 2

    QByteArray out;
    out.reserve(1024 + q.pixels.size() / 2);

    // --- header + logical screen descriptor ---
    out.append("GIF89a", 6);
    appendLe16(out, q.width);
    appendLe16(out, q.height);
    out.append(char(0x80 | (7 << 4) | n));   // global table present, 8-bit colour
    out.append(char(0));                     // background colour index
    out.append(char(0));                     // pixel aspect ratio: unspecified

    // --- global colour table ---
    for (int i = 0; i < tableEntries; ++i) {
        const QRgb c = i < q.palette.size() ? q.palette[i] : qRgb(0, 0, 0);
        out.append(char(qRed(c)));
        out.append(char(qGreen(c)));
        out.append(char(qBlue(c)));
    }

    // --- graphic control extension: only needed to declare transparency ---
    if (q.transparentIndex >= 0) {
        out.append(char(0x21));
        out.append(char(0xF9));
        out.append(char(0x04));
        out.append(char(0x01));              // transparent-colour flag set
        appendLe16(out, 0);                  // frame delay: none
        out.append(char(q.transparentIndex));
        out.append(char(0));                 // block terminator
    }

    // --- image descriptor ---
    out.append(char(0x2C));
    appendLe16(out, 0);                      // left
    appendLe16(out, 0);                      // top
    appendLe16(out, q.width);
    appendLe16(out, q.height);
    out.append(char(0x00));                  // global table, not interlaced

    // --- image data ---
    out.append(char(minCodeSize));
    lzwCompress(q.pixels, minCodeSize, out);

    out.append(char(0x3B));                  // trailer

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return fail(f.errorString());
    if (f.write(out) != out.size())
        return fail(f.errorString());
    if (!f.flush())
        return fail(f.errorString());
    f.close();

    if (error)
        error->clear();
    return true;
}

} // namespace GifEncoder
