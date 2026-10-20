#include "platform/barcode/ZxingBarcodeDecode.h"

#include <QImage>
#include <QtMath>

// ZXing exposes its headers under a ZXing/ prefix when installed (the
// find_package path) but flat in the source tree (the FetchContent path, which is
// this project's default). Both are supported acquisition routes — see the ZXing
// block in CMakeLists.txt.
#if __has_include(<ZXing/ReadBarcode.h>)
#  include <ZXing/ReadBarcode.h>
#else
#  include <ReadBarcode.h>
#endif

namespace platform {

namespace {

// Above this many pixels the image is scaled down before decoding. This runs
// synchronously every time a selection is committed, so it sits directly in the
// latency between releasing the mouse and the chrome appearing.
//
// This was 8M ("roughly a 4K frame"), which turned out to be self-defeating: a
// 3840x2160 capture is 8.29 MP, so it squeaked past the guard and was "downscaled"
// to 3770x2121 — a 1.8% reduction that still paid a full resample. Measured on a
// real 4K capture, all formats: 8M = 56 ms (20 ms of it just the Grayscale8
// conversion), 2M = 17 ms. 2M is therefore faster than the old threshold was with
// QR alone (38 ms), which is what buys the headroom for the wider format set below.
//
// The tradeoff is resolution: at 2M a 4K capture decodes at 1886x1061, so a code
// must survive a ~2.2x shrink. Ordinary selections are far below the threshold and
// are never touched at all — this only bites a full-desktop selection containing a
// physically small code.
constexpr qint64 kMaxDecodePixels = 2'000'000;

} // namespace

QString ZxingBarcodeDecode::decodeFirst(const QImage& image)
{
    if (image.isNull() || image.width() < 8 || image.height() < 8)
        return QString();

    QImage src = image;
    if (qint64(src.width()) * src.height() > kMaxDecodePixels)
        src = src.scaled(src.size() * qSqrt(qreal(kMaxDecodePixels)
                                            / (qreal(src.width()) * src.height())),
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Grayscale8 is ZXing's native luminance layout, so this hands over a tightly
    // packed buffer with no per-pixel conversion inside the reader.
    const QImage gray = src.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull())
        return QString();

    const ZXing::ImageView view(gray.constBits(), gray.width(), gray.height(),
                                ZXing::ImageFormat::Lum, int(gray.bytesPerLine()));

    ZXing::ReaderOptions opts;
    // Every symbology ZXing can read — `Any` is LinearCodes | MatrixCodes, 18
    // formats in the pinned v2.2.1. 2D: QR, Micro QR, rMQR, Aztec, DataMatrix,
    // PDF417, MaxiCode. 1D: EAN-8/13, UPC-A/E, Code128/39/93, ITF, Codabar,
    // DataBar, DataBar Expanded. Measured cost of the wider set is ~1 ms on an
    // ordinary selection; see kMaxDecodePixels for the case that actually costs
    // something.
    //
    // Caveat worth knowing if bogus chips ever appear: tryHarder makes the 1D
    // readers permissive, and unlike QR they have no finder pattern to anchor on,
    // so striped screen content (table rules, underlines, indent guides) can in
    // principle decode as a short Code39/ITF. Left on deliberately — detection
    // matters more than the occasional false read, and the chip is passive.
    opts.setFormats(ZXing::BarcodeFormat::Any);
    opts.setTryHarder(true);      // tolerate imperfect crops / low contrast
    opts.setTryRotate(true);      // a code on screen need not be upright
    opts.setMaxNumberOfSymbols(1);// first code only — the rest is wasted work

    const auto result = ZXing::ReadBarcode(view, opts);
    if (!result.isValid())
        return QString();
    return QString::fromStdString(result.text());
}

} // namespace platform
