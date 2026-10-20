#include "core/codec/PdfEncoder.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QRectF>
#include <QSizeF>

namespace {

// A4 in PostScript points (1 pt = 1/72 in), the unit every PDF coordinate is in.
constexpr qreal kA4W = 595.276;
constexpr qreal kA4H = 841.890;

// Fixed 3 decimals everywhere: enough for sub-micron placement, and it keeps the
// numbers readable when someone opens the PDF in a text editor.
QByteArray num(qreal v)
{
    return QByteArray::number(double(v), 'f', 3);
}

struct Geometry
{
    QSizeF page;   // /MediaBox
    QRectF box;    // where the image sits on it
};

Geometry geometryFor(const QImage& img, PdfEncoder::PageMode mode)
{
    const qreal w = img.width();
    const qreal h = img.height();
    switch (mode) {
    case PdfEncoder::PageMode::Dpi72:
        return { QSizeF(w, h), QRectF(0, 0, w, h) };
    case PdfEncoder::PageMode::Dpi96:
        return { QSizeF(w * 0.75, h * 0.75), QRectF(0, 0, w * 0.75, h * 0.75) };
    case PdfEncoder::PageMode::FitA4:
        break;
    }
    // A4, turned to match the image so a landscape shot is not squeezed onto a
    // portrait page. The image keeps its aspect and is centred, touching two edges.
    const QSizeF page = (w > h) ? QSizeF(kA4H, kA4W) : QSizeF(kA4W, kA4H);
    const qreal s = qMin(page.width() / w, page.height() / h);
    // Clamped to the page: on the axis that fits exactly, `w * s` can land an ulp
    // OVER the page size, which put the image a hair off-page and wrote a "-0.000"
    // offset into the file.
    const QSizeF box(qMin(w * s, page.width()), qMin(h * s, page.height()));
    return { page, QRectF((page.width() - box.width()) / 2,
                          (page.height() - box.height()) / 2,
                          box.width(), box.height()) };
}

// Row-packed RGB bytes. QImage pads every scanline to a 4-byte boundary, so the
// copy has to be per row -- handing constBits() over wholesale would embed the
// padding and shear the picture.
QByteArray rgbBytes(const QImage& img)
{
    const QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    if (rgb.isNull())
        return {};
    const qsizetype rowBytes = qsizetype(rgb.width()) * 3;
    QByteArray raw;
    raw.reserve(rowBytes * rgb.height());
    for (int y = 0; y < rgb.height(); ++y)
        raw.append(reinterpret_cast<const char*>(rgb.constScanLine(y)), rowBytes);
    return raw;
}

// "D:20260816143012+08'00'" — the PDF date syntax.
QByteArray pdfDate(const QDateTime& t)
{
    const int offsetMinutes = t.offsetFromUtc() / 60;
    const int mag = qAbs(offsetMinutes);
    return "D:" + t.toString(QStringLiteral("yyyyMMddHHmmss")).toLatin1()
           + (offsetMinutes < 0 ? '-' : '+')
           + QByteArray::number(mag / 60).rightJustified(2, '0') + '\''
           + QByteArray::number(mag % 60).rightJustified(2, '0') + '\'';
}

} // namespace

namespace PdfEncoder {

PageMode pageModeFromString(const QString& v)
{
    if (v == QLatin1String("dpi72"))
        return PageMode::Dpi72;
    if (v == QLatin1String("a4"))
        return PageMode::FitA4;
    return PageMode::Dpi96;
}

bool write(const QImage& img, const QString& path, PageMode mode, QString* error)
{
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    if (img.isNull())
        return fail(QStringLiteral("The image is empty."));

    const QByteArray raw = rgbBytes(img);
    if (raw.isEmpty())
        return fail(QStringLiteral("The image could not be converted for writing."));

    // /FlateDecode is a zlib (RFC 1950) stream, which is exactly what qCompress
    // produces -- after the 4-byte big-endian original length it prepends, and
    // which is NOT part of the zlib data. Default level: 9 buys little on
    // screenshots and costs seconds at 4K.
    const QByteArray flate = qCompress(raw).mid(4);

    const Geometry g = geometryFor(img, mode);
    const QByteArray content = "q " + num(g.box.width()) + " 0 0 " + num(g.box.height())
                               + ' ' + num(g.box.x()) + ' ' + num(g.box.y())
                               + " cm /Im0 Do Q\n";

    QByteArray out;
    out.reserve(flate.size() + 2048);
    QList<qsizetype> offsets;                  // byte offset of each object, in order

    const auto beginObj = [&out, &offsets](int n) {
        offsets.append(out.size());
        out += QByteArray::number(n) + " 0 obj\n";
    };
    const auto endObj = [&out] { out += "endobj\n"; };
    const auto stream = [&out](const QByteArray& dict, const QByteArray& data) {
        out += "<< " + dict + " /Length " + QByteArray::number(data.size()) + " >>\nstream\n";
        out += data;
        out += "\nendstream\n";
    };

    // The binary comment on line 2 is convention: it marks the file as binary for
    // tools that would otherwise mangle line endings in transit.
    out += "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";

    beginObj(1);
    out += "<< /Type /Catalog /Pages 2 0 R >>\n";
    endObj();

    beginObj(2);
    out += "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n";
    endObj();

    beginObj(3);
    out += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
           + num(g.page.width()) + ' ' + num(g.page.height()) + "]"
           + " /Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 0 R >>\n";
    endObj();

    beginObj(4);
    stream({}, content);
    endObj();

    beginObj(5);
    stream("/Type /XObject /Subtype /Image"
           " /Width " + QByteArray::number(img.width())
           + " /Height " + QByteArray::number(img.height())
           + " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode",
           flate);
    endObj();

    beginObj(6);
    out += "<< /Producer (SnapCap " SNAPCAP_VERSION ") /CreationDate ("
           + pdfDate(QDateTime::currentDateTime()) + ") >>\n";
    endObj();

    // Cross-reference table: object 0 is the free-list head, then one 20-byte entry
    // per object in number order (10-digit offset, 5-digit generation, type, EOL).
    const qsizetype xref = out.size();
    out += "xref\n0 " + QByteArray::number(offsets.size() + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (qsizetype off : offsets)
        out += QByteArray::number(off).rightJustified(10, '0') + " 00000 n \n";

    out += "trailer\n<< /Size " + QByteArray::number(offsets.size() + 1)
           + " /Root 1 0 R /Info 6 0 R >>\nstartxref\n"
           + QByteArray::number(xref) + "\n%%EOF\n";

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

} // namespace PdfEncoder
