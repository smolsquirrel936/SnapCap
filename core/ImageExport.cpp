#include "core/ImageExport.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QImageWriter>
#include <QPainter>

#include "core/codec/GifEncoder.h"
#include "core/codec/PdfEncoder.h"
#include "core/codec/TgaEncoder.h"

namespace {

// Extensions SnapCap encodes itself because Qt ships no usable writer for them (its
// TGA and GIF plugins are read-only; PDF is not an image format at all, and Qt's
// QPdfWriter re-encodes the image as JPEG). Keeping the list in ONE place is what stops
// supportedSaveFormats() and toFile()'s dispatch from drifting apart — a format
// advertised but not routed would fail silently, which is the bug this whole
// change exists to prevent.
const QStringList& ownFormats()
{
    static const QStringList k{ QStringLiteral("tga"), QStringLiteral("gif"),
                                QStringLiteral("pdf") };
    return k;
}

// Extensions whose encoder cannot carry an alpha channel. Writing a transparent
// image to one of these lands the see-through pixels on BLACK, which is how a
// rounded-corner snip saved as JPG would come out framed in black corners — so
// they are composited over white here instead. BMP is on the list deliberately:
// Qt can emit a 32-bit BMP, but plenty of viewers read the alpha byte as padding
// and show the same black, and a predictable white is the better deliverable.
// PDF is on it by decision rather than by limitation: the format can carry an alpha
// mask, but a page is paper, so a rounded-corner snip gets white corners there like
// it would in any other document.
// Our own TGA and GIF encoders DO carry transparency, so they stay off it.
const QStringList& noAlphaFormats()
{
    static const QStringList k{
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"),
        QStringLiteral("jfif"), QStringLiteral("bmp"), QStringLiteral("pdf"),
        QStringLiteral("pbm"), QStringLiteral("pgm"), QStringLiteral("ppm"),
    };
    return k;
}

} // namespace

namespace ImageExport {

QStringList supportedSaveFormats()
{
    QStringList out;
    const QList<QByteArray> fmts = QImageWriter::supportedImageFormats();
    out.reserve(fmts.size() + ownFormats().size());
    for (const QByteArray& f : fmts)
        out << QString::fromLatin1(f).toLower();
    out += ownFormats();
    out.removeDuplicates();
    out.sort();
    return out;
}

bool icoWouldDownscale(const QImage& img, const QString& path)
{
    if (img.isNull() || QFileInfo(path).suffix().toLower() != QLatin1String("ico"))
        return false;
    return img.width() > kIcoMaxSide || img.height() > kIcoMaxSide;
}

bool toClipboard(const QImage& img)
{
    if (img.isNull())
        return false;
    QClipboard* cb = QGuiApplication::clipboard();
    if (!cb)
        return false;
    cb->setImage(img);
    return true;
}

bool isFullyOpaque(const QImage& img)
{
    if (img.isNull() || !img.hasAlphaChannel())
        return true;
    const QImage a = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < a.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        for (int x = 0; x < a.width(); ++x)
            if (qAlpha(row[x]) < 255)
                return false;
    }
    return true;
}

QImage flattenOnto(const QImage& img, const QColor& bg)
{
    if (img.isNull())
        return img;
    QImage out(img.size(), QImage::Format_RGB32);
    out.setDevicePixelRatio(img.devicePixelRatio());
    out.fill(bg);
    QPainter p(&out);
    // Device-pixel rects on both sides: the plain drawImage(0,0,…) overload would
    // scale by the source's devicePixelRatio (pin images carry one).
    p.drawImage(out.rect(), img, img.rect());
    p.end();
    return out;
}

bool toFile(const QImage& img, const QString& path, int quality, bool removeAlphaIfOpaque,
            QString* error, PdfEncoder::PageMode pdfPage)
{
    // `error` carries the raw technical reason (usually QImageWriter's). The framing
    // sentence shown to the user is the UI layer's job — core stays presentation-free.
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    if (img.isNull())
        return fail(QStringLiteral("The image is empty."));
    if (path.isEmpty())
        return fail(QStringLiteral("No file name was given."));

    QImage out = img;
    if (removeAlphaIfOpaque && out.hasAlphaChannel() && isFullyOpaque(out))
        out = out.convertToFormat(QImage::Format_RGB32);   // drop the redundant alpha

    // Our own encoders come first: Qt would otherwise reject these extensions outright.
    const QString ext = QFileInfo(path).suffix().toLower();

    if (out.hasAlphaChannel() && noAlphaFormats().contains(ext))
        out = flattenOnto(out, Qt::white);
    if (ext == QLatin1String("tga")) {
        if (!TgaEncoder::write(out, path, error))
            return false;
        if (error)
            error->clear();
        return true;
    }
    if (ext == QLatin1String("gif")) {
        if (!GifEncoder::write(out, path, error))
            return false;
        if (error)
            error->clear();
        return true;
    }
    if (ext == QLatin1String("pdf")) {
        // `quality` has nothing to act on: the image is embedded losslessly.
        if (!PdfEncoder::write(out, path, pdfPage, error))
            return false;
        if (error)
            error->clear();
        return true;
    }

    QImageWriter writer(path);          // format inferred from extension
    if (quality >= 0)
        writer.setQuality(quality);     // -1 keeps the format's default
    if (!writer.write(out))
        return fail(writer.errorString());
    if (error)
        error->clear();
    return true;
}

} // namespace ImageExport
