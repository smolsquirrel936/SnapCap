#include "core/codec/TgaEncoder.h"

#include <QByteArray>
#include <QFile>

namespace {

void appendLe16(QByteArray& a, int v)
{
    a.append(char(v & 0xFF));
    a.append(char((v >> 8) & 0xFF));
}

} // namespace

namespace TgaEncoder {

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
        return fail(QStringLiteral("TGA cannot store images larger than %1 pixels on a side.")
                        .arg(kMaxSide));

    const bool alpha = img.hasAlphaChannel();
    const QImage src = img.convertToFormat(alpha ? QImage::Format_ARGB32
                                                 : QImage::Format_RGB32);
    if (src.isNull())
        return fail(QStringLiteral("The image could not be converted for writing."));

    const int w = src.width();
    const int h = src.height();
    const int bpp = alpha ? 4 : 3;

    QByteArray out;
    out.reserve(18 + w * h * bpp + 26);

    // --- 18-byte header ---
    out.append(char(0));            // image-ID length: none
    out.append(char(0));            // colour-map type: none
    out.append(char(2));            // image type: uncompressed true-colour
    out.append(5, '\0');            // colour-map spec: unused when type is 0
    appendLe16(out, 0);             // x-origin
    appendLe16(out, 0);             // y-origin
    appendLe16(out, w);
    appendLe16(out, h);
    out.append(char(alpha ? 32 : 24));       // bits per pixel
    // Image descriptor: low nibble = attribute (alpha) bits per pixel. Bit 5 stays
    // clear, i.e. the spec-default bottom-up row order — the widest-compatibility
    // choice, since some readers ignore the top-down flag entirely.
    out.append(char(alpha ? 0x08 : 0x00));

    // --- pixel data, bottom row first, BGR(A) byte order ---
    for (int y = h - 1; y >= 0; --y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = row[x];
            out.append(char(qBlue(p)));
            out.append(char(qGreen(p)));
            out.append(char(qRed(p)));
            if (alpha)
                out.append(char(qAlpha(p)));
        }
    }

    // --- 26-byte TGA 2.0 footer ---
    // A TGA has no magic number at the START of the file, so this trailing signature
    // is what most readers sniff to identify one. Both area offsets are zero (absent).
    out.append(4, '\0');                        // extension-area offset
    out.append(4, '\0');                        // developer-area offset
    out.append("TRUEVISION-XFILE.", 17);
    out.append(char(0));

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

} // namespace TgaEncoder
