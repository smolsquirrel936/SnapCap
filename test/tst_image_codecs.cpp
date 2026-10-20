#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QImage>
#include <QRegularExpression>

#include "core/ImageExport.h"
#include "core/codec/AnimatedImage.h"
#include "core/codec/GifEncoder.h"
#include "core/codec/PdfEncoder.h"
#include "core/codec/Quantize.h"
#include "core/codec/TgaEncoder.h"

// The encoders SnapCap ships itself, for formats Qt can write no other way
// (Features.md §5). TGA cannot be round-tripped — Qt has no TGA *reader* here
// either — so these assert the produced bytes directly against the spec.
class TstImageCodecs : public QObject
{
    Q_OBJECT
private slots:
    void tgaWrites32BitWhenImageHasAlpha();
    void tgaWrites24BitWhenOpaque();
    void tgaStoresRowsBottomUp();
    void tgaRejectsNullImage();
    void tgaIsRoutedByImageExport();
    void tgaIsAdvertisedAsSupported();

    void quantizeKeepsSmallPalettesExact();
    void quantizeFallsBackForManyColours();
    void quantizeReservesATransparentSlot();
    void gifRoundTripsLosslesslyUnder256Colours();
    void gifQuantizesLargerImagesCloseEnough();
    void gifRoundTripsTransparency();
    void gifSurvivesDictionaryResets();
    void gifIsRoutedByImageExport();

    void animLoadsEveryFrameWithDelays();
    void animRejectsStillImages();
    void animRejectsMissingFile();

    void pdfIsAValidSinglePageDocument();
    void pdfEmbedsThePixelsLosslessly();
    void pdfPageBoxMatchesTheMode();
    void pdfFitsAndCentresOnA4();
    void pdfRejectsNullImage();
    void pdfIsRoutedByImageExport();

private:
    QTemporaryDir m_dir;

    // A hand-built 2-frame GIF89a: 1x1 px, a 2-colour global table (red, green), frame 0
    // red at 20cs and frame 1 green at 0cs (the legacy "as fast as possible" delay the
    // loader clamps). Qt has no animated-GIF *writer*, so the fixture is written out here
    // byte by byte.
    static QByteArray animatedGifBytes()
    {
        static const quint8 kBytes[] = {
            0x47,0x49,0x46,0x38,0x39,0x61,                     // "GIF89a"
            0x01,0x00, 0x01,0x00, 0xF0, 0x00, 0x00,            // 1x1, global table of 2
            0xFF,0x00,0x00,  0x00,0xFF,0x00,                   // red, green
            0x21,0xFF,0x0B,0x4E,0x45,0x54,0x53,0x43,0x41,0x50,0x45,
            0x32,0x2E,0x30,0x03,0x01,0x00,0x00,0x00,           // NETSCAPE2.0: loop forever
            0x21,0xF9,0x04,0x00,0x14,0x00,0x00,0x00,           // frame 0: delay 20cs
            0x2C,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00, // image descriptor
            0x02,0x02,0x44,0x01,0x00,                          // LZW: one pixel, index 0
            0x21,0xF9,0x04,0x00,0x00,0x00,0x00,0x00,           // frame 1: delay 0
            0x2C,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00,
            0x02,0x02,0x4C,0x01,0x00,                          // LZW: one pixel, index 1
            0x3B,                                              // trailer
        };
        return QByteArray(reinterpret_cast<const char*>(kBytes), int(sizeof(kBytes)));
    }

    static bool writeBytes(const QString& path, const QByteArray& bytes)
    {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    }

    // Distinct colours per corner so row/column order is unambiguous.
    static QImage sample(bool withAlpha)
    {
        QImage img(3, 2, withAlpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
        img.fill(Qt::black);
        img.setPixelColor(0, 0, QColor(10, 20, 30, withAlpha ? 40 : 255));    // top-left
        img.setPixelColor(0, 1, QColor(50, 60, 70, 255));                     // bottom-left
        return img;
    }

    static QByteArray readAll(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    static quint8 at(const QByteArray& b, int i) { return quint8(b.at(i)); }
};

void TstImageCodecs::tgaWrites32BitWhenImageHasAlpha()
{
    const QString path = m_dir.filePath(QStringLiteral("alpha.tga"));
    QVERIFY(TgaEncoder::write(sample(true), path));
    const QByteArray b = readAll(path);

    // 18-byte header + 3*2 pixels * 4 bytes + 26-byte footer.
    QCOMPARE(b.size(), 18 + 3 * 2 * 4 + 26);
    QCOMPARE(at(b, 0), quint8(0));    // no image ID
    QCOMPARE(at(b, 1), quint8(0));    // no colour map
    QCOMPARE(at(b, 2), quint8(2));    // uncompressed true-colour
    QCOMPARE(at(b, 12) | (at(b, 13) << 8), 3);      // width, little-endian
    QCOMPARE(at(b, 14) | (at(b, 15) << 8), 2);      // height, little-endian
    QCOMPARE(at(b, 16), quint8(32));  // bits per pixel
    QCOMPARE(at(b, 17), quint8(0x08)); // 8 attribute (alpha) bits, bottom-up

    // TGA has no leading magic number, so the trailing signature is the identifier.
    QCOMPARE(b.mid(b.size() - 18, 17), QByteArray("TRUEVISION-XFILE."));
}

void TstImageCodecs::tgaWrites24BitWhenOpaque()
{
    const QString path = m_dir.filePath(QStringLiteral("opaque.tga"));
    QVERIFY(TgaEncoder::write(sample(false), path));
    const QByteArray b = readAll(path);

    QCOMPARE(b.size(), 18 + 3 * 2 * 3 + 26);
    QCOMPARE(at(b, 16), quint8(24));   // no alpha channel -> 24bpp
    QCOMPARE(at(b, 17), quint8(0x00)); // no attribute bits
}

void TstImageCodecs::tgaStoresRowsBottomUp()
{
    const QString path = m_dir.filePath(QStringLiteral("order.tga"));
    QVERIFY(TgaEncoder::write(sample(true), path));
    const QByteArray b = readAll(path);

    // The first pixel written is the BOTTOM-left one, stored as B,G,R,A.
    QCOMPARE(at(b, 18 + 0), quint8(70));    // blue
    QCOMPARE(at(b, 18 + 1), quint8(60));    // green
    QCOMPARE(at(b, 18 + 2), quint8(50));    // red
    QCOMPARE(at(b, 18 + 3), quint8(255));   // alpha

    // The top-left pixel is on the LAST row of data (row 1 of 2, 3 px per row).
    const int topRow = 18 + 3 * 4;
    QCOMPARE(at(b, topRow + 0), quint8(30));   // blue
    QCOMPARE(at(b, topRow + 1), quint8(20));   // green
    QCOMPARE(at(b, topRow + 2), quint8(10));   // red
    QCOMPARE(at(b, topRow + 3), quint8(40));   // alpha survives unpremultiplied
}

void TstImageCodecs::tgaRejectsNullImage()
{
    QString err;
    QVERIFY(!TgaEncoder::write(QImage(), m_dir.filePath(QStringLiteral("null.tga")), &err));
    QVERIFY(!err.isEmpty());

    // An unwritable path must fail with a reason, not crash or half-write.
    err.clear();
    QVERIFY(!TgaEncoder::write(sample(true),
                               m_dir.filePath(QStringLiteral("no/such/dir/x.tga")), &err));
    QVERIFY(!err.isEmpty());
}

void TstImageCodecs::tgaIsRoutedByImageExport()
{
    // toFile() must dispatch .tga to our encoder instead of handing it to Qt,
    // which would fail outright.
    const QString path = m_dir.filePath(QStringLiteral("routed.tga"));
    QString err = QStringLiteral("stale");
    QVERIFY(ImageExport::toFile(sample(true), path, -1, false, &err));
    QVERIFY(err.isEmpty());
    QVERIFY(QFile::exists(path));
    QCOMPARE(readAll(path).mid(2, 1), QByteArray(1, char(2)));   // real TGA header

    // removeAlphaIfOpaque still composes: an opaque image lands in the 24bpp form.
    const QString op = m_dir.filePath(QStringLiteral("routed_opaque.tga"));
    QImage opaque(3, 2, QImage::Format_ARGB32);
    opaque.fill(Qt::red);
    QVERIFY(ImageExport::toFile(opaque, op, -1, /*removeAlphaIfOpaque=*/true));
    QCOMPARE(at(readAll(op), 16), quint8(24));
}

void TstImageCodecs::tgaIsAdvertisedAsSupported()
{
    // The Save dialog builds its list from this, so the advertisement and the
    // routing above have to agree.
    QVERIFY(ImageExport::supportedSaveFormats().contains(QStringLiteral("tga")));
}

// --- GIF ---------------------------------------------------------------------
// Unlike TGA, GIF *can* be round-tripped: Qt's qgif plugin reads (it just cannot
// write), so these decode the real file rather than inspecting bytes.

namespace {

// Exactly `n` distinct opaque colours, laid out deterministically.
QImage distinctColours(int n)
{
    QImage img(n, 1, QImage::Format_ARGB32);
    for (int i = 0; i < n; ++i)
        img.setPixelColor(i, 0, QColor(i % 256, (i * 7) % 256, (i * 13) % 256));
    return img;
}

// A smooth gradient — far more than 256 colours, so it must be quantized.
QImage gradient(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixelColor(x, y, QColor((x * 255) / (w - 1), (y * 255) / (h - 1),
                                           ((x + y) * 255) / (w + h - 2)));
    return img;
}

} // namespace

void TstImageCodecs::quantizeKeepsSmallPalettesExact()
{
    // The whole point of the exact path: a UI/text screenshot within budget must
    // not be dithered at all.
    const Quantize::Indexed q = Quantize::toIndexed(distinctColours(256));
    QVERIFY(q.exact);
    QCOMPARE(q.palette.size(), 256);
    QCOMPARE(q.transparentIndex, -1);
    QCOMPARE(q.pixels.size(), 256);
}

void TstImageCodecs::quantizeFallsBackForManyColours()
{
    const Quantize::Indexed q = Quantize::toIndexed(gradient(64, 64));
    QVERIFY(!q.exact);
    QVERIFY(q.palette.size() <= 256);
    QVERIFY(q.palette.size() > 1);              // it really did build a palette
    QCOMPARE(q.pixels.size(), 64 * 64);
    for (quint8 idx : q.pixels)
        QVERIFY(int(idx) < q.palette.size());   // no out-of-range index
}

void TstImageCodecs::quantizeReservesATransparentSlot()
{
    QImage img = distinctColours(8);
    img.setPixelColor(0, 0, QColor(0, 0, 0, 0));        // one fully transparent pixel
    const Quantize::Indexed q = Quantize::toIndexed(img);
    QVERIFY(q.transparentIndex >= 0);
    QCOMPARE(q.pixels[0], quint8(q.transparentIndex));
    QVERIFY(q.palette.size() <= 256);
}

void TstImageCodecs::gifRoundTripsLosslesslyUnder256Colours()
{
    const QImage src = distinctColours(256);
    const QString path = m_dir.filePath(QStringLiteral("exact.gif"));
    QVERIFY(GifEncoder::write(src, path));

    const QImage back = QImage(path).convertToFormat(QImage::Format_ARGB32);
    QVERIFY(!back.isNull());
    QCOMPARE(back.size(), src.size());
    // Every one of the 256 colours must survive bit-exactly.
    for (int i = 0; i < 256; ++i)
        QCOMPARE(back.pixelColor(i, 0), src.pixelColor(i, 0));
}

void TstImageCodecs::gifQuantizesLargerImagesCloseEnough()
{
    const QImage src = gradient(64, 64);
    const QString path = m_dir.filePath(QStringLiteral("grad.gif"));
    QVERIFY(GifEncoder::write(src, path));

    const QImage back = QImage(path).convertToFormat(QImage::Format_ARGB32);
    QVERIFY(!back.isNull());
    QCOMPARE(back.size(), src.size());

    // Lossy by necessity, but dithering should keep the MEAN error small. A broken
    // palette or a desynchronised LZW stream would blow well past this.
    qint64 total = 0;
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            const QColor a = src.pixelColor(x, y);
            const QColor b = back.pixelColor(x, y);
            total += qAbs(a.red() - b.red()) + qAbs(a.green() - b.green())
                   + qAbs(a.blue() - b.blue());
        }
    }
    const double meanPerChannel = double(total) / (src.width() * src.height() * 3);
    QVERIFY2(meanPerChannel < 8.0,
             qPrintable(QStringLiteral("mean channel error %1").arg(meanPerChannel)));
}

void TstImageCodecs::gifRoundTripsTransparency()
{
    QImage src = distinctColours(16);
    src.setPixelColor(0, 0, QColor(0, 0, 0, 0));
    src.setPixelColor(3, 0, QColor(0, 0, 0, 10));      // below threshold -> transparent
    const QString path = m_dir.filePath(QStringLiteral("alpha.gif"));
    QVERIFY(GifEncoder::write(src, path));

    const QImage back = QImage(path).convertToFormat(QImage::Format_ARGB32);
    QVERIFY(!back.isNull());
    QCOMPARE(qAlpha(back.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(back.pixel(3, 0)), 0);
    QCOMPARE(qAlpha(back.pixel(5, 0)), 255);           // opaque pixels stay opaque
    QCOMPARE(back.pixelColor(5, 0).rgb(), src.pixelColor(5, 0).rgb());
}

void TstImageCodecs::gifSurvivesDictionaryResets()
{
    // The LZW dictionary fills at 4096 codes, and only then does the encoder emit a
    // clear code and rebuild — the subtlest branch in the format, and one the small
    // fixtures above never reach. Deterministic noise over a large area maximises
    // dictionary churn, forcing many resets. If the widen/reset boundary disagreed
    // with the decoder by even one code, the stream would desynchronise and the
    // tail of the image would decode as garbage.
    const int w = 512, h = 512;
    QImage src(w, h, QImage::Format_ARGB32);
    quint32 seed = 12345;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            seed = seed * 1103515245u + 12345u;          // reproducible LCG
            src.setPixelColor(x, y, QColor((seed >> 16) & 0xFF, (seed >> 8) & 0xFF,
                                           seed & 0xFF));
        }
    }

    const QString path = m_dir.filePath(QStringLiteral("noise.gif"));
    QVERIFY(GifEncoder::write(src, path));

    const QImage back = QImage(path).convertToFormat(QImage::Format_ARGB32);
    QVERIFY(!back.isNull());
    QCOMPARE(back.size(), src.size());

    // A desynchronised stream shows up as a corrupt TAIL, so check the last row
    // specifically rather than trusting an average over the whole image.
    qint64 lastRow = 0;
    for (int x = 0; x < w; ++x) {
        const QColor a = src.pixelColor(x, h - 1);
        const QColor b = back.pixelColor(x, h - 1);
        lastRow += qAbs(a.red() - b.red()) + qAbs(a.green() - b.green())
                 + qAbs(a.blue() - b.blue());
    }
    const double meanLastRow = double(lastRow) / (w * 3);
    QVERIFY2(meanLastRow < 24.0,
             qPrintable(QStringLiteral("last-row mean error %1").arg(meanLastRow)));
}

void TstImageCodecs::gifIsRoutedByImageExport()
{
    const QString path = m_dir.filePath(QStringLiteral("routed.gif"));
    QString err = QStringLiteral("stale");
    QVERIFY(ImageExport::toFile(distinctColours(16), path, -1, false, &err));
    QVERIFY(err.isEmpty());
    QCOMPARE(readAll(path).left(6), QByteArray("GIF89a"));
    QVERIFY(ImageExport::supportedSaveFormats().contains(QStringLiteral("gif")));
}

// --- AnimatedImage (the read side: GIF playback on a pin) --------------------

void TstImageCodecs::animLoadsEveryFrameWithDelays()
{
    const QString path = m_dir.filePath(QStringLiteral("two.gif"));
    QVERIFY(writeBytes(path, animatedGifBytes()));

    const anim::Animation a = anim::load(path);
    QCOMPARE(a.status, anim::Animation::Status::Loaded);
    QCOMPARE(a.frames.size(), 2);
    QCOMPARE(a.delaysMs.size(), 2);
    QCOMPARE(a.frames.at(0).pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(a.frames.at(1).pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(a.frames.at(0).format(), QImage::Format_ARGB32);
    QCOMPARE(a.delaysMs.at(0), 200);        // 20cs, as authored
    QCOMPARE(a.delaysMs.at(1), 100);        // 0 → the legacy floor, not a strobe
}

void TstImageCodecs::animRejectsStillImages()
{
    const QString path = m_dir.filePath(QStringLiteral("still.png"));
    QVERIFY(sample(true).save(path));
    const anim::Animation a = anim::load(path);
    QCOMPARE(a.status, anim::Animation::Status::NotAnimated);
    QVERIFY(a.frames.isEmpty());
}

void TstImageCodecs::animRejectsMissingFile()
{
    const anim::Animation a = anim::load(m_dir.filePath(QStringLiteral("nope.gif")));
    QCOMPARE(a.status, anim::Animation::Status::Unreadable);
}

// --- PDF ---------------------------------------------------------------------
// Like TGA there is no reader to round-trip through, but unlike TGA the payload
// is compressed: these inflate the embedded stream and compare it to the source
// pixels, which is the whole reason we encode PDF ourselves instead of using
// QPdfWriter (it re-encodes as JPEG — see core/codec/PdfEncoder.h).

namespace {

// A width whose RGB888 rows (5*3 = 15 bytes) do NOT land on the 4-byte boundary
// QImage pads scanlines to — so this fixture catches an encoder that hands over
// constBits() wholesale and embeds the padding.
QImage unpaddedRows()
{
    QImage img(5, 3, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixelColor(x, y, QColor(x * 40 + 5, y * 60 + 7, (x + y) * 20 + 3));
    return img;
}

// The exact bytes the encoder should embed: row-packed RGB, no padding.
QByteArray packedRgb(const QImage& src)
{
    const QImage rgb = src.convertToFormat(QImage::Format_RGB888);
    QByteArray raw;
    for (int y = 0; y < rgb.height(); ++y)
        raw.append(reinterpret_cast<const char*>(rgb.constScanLine(y)),
                   qsizetype(rgb.width()) * 3);
    return raw;
}

// The image XObject's stream, sliced by its declared /Length (not by scanning for
// "endstream", which compressed bytes could contain by chance).
QByteArray imageStream(const QByteArray& pdf)
{
    const qsizetype obj = pdf.indexOf("/Subtype /Image");
    if (obj < 0)
        return {};
    const qsizetype dictEnd = pdf.indexOf(">>", obj);
    const QRegularExpressionMatch m =
        QRegularExpression(QStringLiteral("/Length (\\d+)"))
            .match(QString::fromLatin1(pdf.mid(obj, dictEnd - obj)));
    if (!m.hasMatch())
        return {};
    const qsizetype start = pdf.indexOf("stream\n", dictEnd) + 7;
    return pdf.mid(start, m.captured(1).toLongLong());
}

// qCompress's output minus its 4-byte length header is what we embed, so put the
// header back to inflate it — proof the stream really is plain zlib/FlateDecode.
QByteArray inflate(const QByteArray& z, qsizetype rawSize)
{
    QByteArray withHeader(4, '\0');
    for (int i = 0; i < 4; ++i)
        withHeader[i] = char((rawSize >> (8 * (3 - i))) & 0xFF);
    return qUncompress(withHeader + z);
}

// The four numbers of whichever construct `pattern` captures. Returns an empty
// list on no match — callers check the size first, so a malformed file fails the
// comparison instead of crashing the run on an out-of-range index. The captures
// allow a leading '-': a negative here is a bug to be REPORTED, not skipped.
QList<double> fourNumbers(const QByteArray& pdf, const QString& pattern)
{
    const QRegularExpressionMatch m = QRegularExpression(pattern).match(QString::fromLatin1(pdf));
    QList<double> v;
    for (int i = 1; i <= 4 && m.hasMatch(); ++i)
        v << m.captured(i).toDouble();
    return v;
}

// The four /MediaBox numbers.
QList<double> mediaBox(const QByteArray& pdf)
{
    return fourNumbers(pdf, QStringLiteral("/MediaBox \\[(-?[\\d.]+) (-?[\\d.]+) (-?[\\d.]+) (-?[\\d.]+)\\]"));
}

// The content stream's "w 0 0 h x y cm" placement of the image.
QList<double> imageBox(const QByteArray& pdf)
{
    return fourNumbers(pdf, QStringLiteral("q (-?[\\d.]+) 0 0 (-?[\\d.]+) (-?[\\d.]+) (-?[\\d.]+) cm"));
}

} // namespace

void TstImageCodecs::pdfIsAValidSinglePageDocument()
{
    const QString path = m_dir.filePath(QStringLiteral("one.pdf"));
    QVERIFY(PdfEncoder::write(unpaddedRows(), path, PdfEncoder::PageMode::Dpi96));
    const QByteArray b = readAll(path);

    QVERIFY(b.startsWith("%PDF-1.4\n"));
    QVERIFY(b.endsWith("%%EOF\n"));
    QCOMPARE(b.count("/Type /Page "), 1);          // exactly one page object
    QVERIFY(b.contains("/Count 1"));
    QVERIFY(b.contains("/Filter /FlateDecode"));   // lossless, never /DCTDecode
    QVERIFY(!b.contains("/DCTDecode"));

    // startxref must point at the xref table, or viewers reject the file outright.
    const QRegularExpressionMatch m =
        QRegularExpression(QStringLiteral("startxref\\n(\\d+)")).match(QString::fromLatin1(b));
    QVERIFY(m.hasMatch());
    QCOMPARE(b.mid(m.captured(1).toLongLong(), 5), QByteArray("xref\n"));
}

void TstImageCodecs::pdfEmbedsThePixelsLosslessly()
{
    // The decisive test: inflate what we wrote and compare byte for byte.
    const QImage src = unpaddedRows();
    const QString path = m_dir.filePath(QStringLiteral("lossless.pdf"));
    QVERIFY(PdfEncoder::write(src, path, PdfEncoder::PageMode::Dpi96));

    const QByteArray expected = packedRgb(src);
    const QByteArray got = inflate(imageStream(readAll(path)), expected.size());
    QCOMPARE(got.size(), expected.size());
    QCOMPARE(got, expected);
}

void TstImageCodecs::pdfPageBoxMatchesTheMode()
{
    const QImage src = unpaddedRows();               // 5 x 3 px
    const QString p72 = m_dir.filePath(QStringLiteral("m72.pdf"));
    const QString p96 = m_dir.filePath(QStringLiteral("m96.pdf"));
    QVERIFY(PdfEncoder::write(src, p72, PdfEncoder::PageMode::Dpi72));
    QVERIFY(PdfEncoder::write(src, p96, PdfEncoder::PageMode::Dpi96));

    QCOMPARE(mediaBox(readAll(p72)).size(), 4);
    QCOMPARE(mediaBox(readAll(p72)), (QList<double>{0, 0, 5, 3}));          // 1 px = 1 pt
    QCOMPARE(mediaBox(readAll(p96)), (QList<double>{0, 0, 3.75, 2.25}));    // px * 72/96

    // The image fills the page in both — no margin, no letterbox.
    QCOMPARE(imageBox(readAll(p72)), (QList<double>{5, 3, 0, 0}));

    // An unknown config value must land on the default rather than break saving.
    QCOMPARE(PdfEncoder::pageModeFromString(QStringLiteral("nonsense")),
             PdfEncoder::PageMode::Dpi96);
    QCOMPARE(PdfEncoder::pageModeFromString(QStringLiteral("dpi72")),
             PdfEncoder::PageMode::Dpi72);
    QCOMPARE(PdfEncoder::pageModeFromString(QStringLiteral("a4")),
             PdfEncoder::PageMode::FitA4);
}

void TstImageCodecs::pdfFitsAndCentresOnA4()
{
    const QString wide = m_dir.filePath(QStringLiteral("a4_wide.pdf"));
    const QString tall = m_dir.filePath(QStringLiteral("a4_tall.pdf"));
    QVERIFY(PdfEncoder::write(QImage(400, 200, QImage::Format_RGB32), wide,
                              PdfEncoder::PageMode::FitA4));
    QVERIFY(PdfEncoder::write(QImage(200, 400, QImage::Format_RGB32), tall,
                              PdfEncoder::PageMode::FitA4));

    // A4 turns to match the image instead of squeezing a landscape shot onto a
    // portrait page.
    const QList<double> w = mediaBox(readAll(wide));
    const QList<double> t = mediaBox(readAll(tall));
    QCOMPARE(w.size(), 4);
    QCOMPARE(t.size(), 4);
    QCOMPARE(w.at(2), 841.890);
    QCOMPARE(w.at(3), 595.276);
    QCOMPARE(t.at(2), 595.276);
    QCOMPARE(t.at(3), 841.890);

    // Aspect preserved, centred, touching the two edges it can reach.
    const QList<double> box = imageBox(readAll(wide));
    QCOMPARE(box.size(), 4);
    QVERIFY(qAbs(box.at(0) / box.at(1) - 2.0) < 0.001);              // 400:200
    QCOMPARE(box.at(2), 0.0);                                        // fills the width
    QVERIFY(qAbs(box.at(3) - (w.at(3) - box.at(1)) / 2) < 0.001);    // centred vertically
}

void TstImageCodecs::pdfRejectsNullImage()
{
    QString err;
    QVERIFY(!PdfEncoder::write(QImage(), m_dir.filePath(QStringLiteral("null.pdf")),
                               PdfEncoder::PageMode::Dpi96, &err));
    QVERIFY(!err.isEmpty());

    err.clear();
    QVERIFY(!PdfEncoder::write(unpaddedRows(),
                               m_dir.filePath(QStringLiteral("no/such/dir/x.pdf")),
                               PdfEncoder::PageMode::Dpi96, &err));
    QVERIFY(!err.isEmpty());
}

void TstImageCodecs::pdfIsRoutedByImageExport()
{
    const QString path = m_dir.filePath(QStringLiteral("routed.pdf"));
    QString err = QStringLiteral("stale");
    QVERIFY(ImageExport::toFile(unpaddedRows(), path, -1, false, &err));
    QVERIFY(err.isEmpty());
    QVERIFY(readAll(path).startsWith("%PDF-"));
    QVERIFY(ImageExport::supportedSaveFormats().contains(QStringLiteral("pdf")));

    // A page is paper: transparency is flattened onto WHITE, not left black.
    QImage clear(2, 1, QImage::Format_ARGB32);
    clear.fill(QColor(0, 0, 0, 0));
    const QString flat = m_dir.filePath(QStringLiteral("flat.pdf"));
    QVERIFY(ImageExport::toFile(clear, flat, -1, false, &err));
    const QByteArray px = inflate(imageStream(readAll(flat)), 2 * 1 * 3);
    QCOMPARE(px, QByteArray(6, char(0xFF)));

    // The page mode reaches the encoder through toFile's trailing argument.
    const QString a4 = m_dir.filePath(QStringLiteral("routed_a4.pdf"));
    QVERIFY(ImageExport::toFile(unpaddedRows(), a4, -1, false, &err,
                                PdfEncoder::PageMode::FitA4));
    QCOMPARE(mediaBox(readAll(a4)).at(2), 841.890);   // landscape fixture → landscape A4
}

QTEST_GUILESS_MAIN(TstImageCodecs)
#include "tst_image_codecs.moc"
