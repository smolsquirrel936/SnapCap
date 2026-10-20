#include "core/ColorPicker.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace ColorPicker {

namespace {
// Round a 0-255 channel to a 0-100 percentage.
int toPercent(int v255) { return qRound(v255 * 100.0 / 255.0); }
}  // namespace

QString format(const QColor& c, Format f, bool uppercaseHex, bool noHash)
{
    switch (f) {
        case Format::Rgb:
            return QStringLiteral("%1, %2, %3").arg(c.red()).arg(c.green()).arg(c.blue());
        case Format::Hsv: {
            int h, s, v;
            c.getHsv(&h, &s, &v);
            if (h < 0) h = 0;                       // achromatic -> hue reported as -1
            return QStringLiteral("%1, %2%, %3%").arg(h).arg(toPercent(s)).arg(toPercent(v));
        }
        case Format::Hsl: {
            int h, s, l;
            c.getHsl(&h, &s, &l);
            if (h < 0) h = 0;
            return QStringLiteral("%1, %2%, %3%").arg(h).arg(toPercent(s)).arg(toPercent(l));
        }
        case Format::Hsi: {
            // Textbook HSI: I is the plain RGB mean, S is how far the color sits
            // from the gray axis, H is the arccos form (undefined for gray -> 0).
            const double r = c.red() / 255.0, g = c.green() / 255.0, b = c.blue() / 255.0;
            const double sum = r + g + b;
            const double i = sum / 3.0;
            const double s = sum <= 0.0 ? 0.0 : 1.0 - 3.0 * std::min({r, g, b}) / sum;
            const double denom = std::sqrt((r - g) * (r - g) + (r - b) * (g - b));
            double h = 0.0;
            if (denom > 0.0) {
                const double t = qBound(-1.0, 0.5 * ((r - g) + (r - b)) / denom, 1.0);
                h = qRadiansToDegrees(std::acos(t));
                if (b > g) h = 360.0 - h;
            }
            return QStringLiteral("%1, %2%, %3%")
                .arg(qRound(h) % 360).arg(qRound(s * 100.0)).arg(qRound(i * 100.0));
        }
        case Format::Cmyk: {
            int cy, m, y, k;
            c.getCmyk(&cy, &m, &y, &k);
            return QStringLiteral("%1%, %2%, %3%, %4%")
                .arg(toPercent(cy)).arg(toPercent(m)).arg(toPercent(y)).arg(toPercent(k));
        }
        case Format::Dec:
            return QString::number((c.red() << 16) | (c.green() << 8) | c.blue());
        case Format::Hex:
        default: {
            // QColor::name() -> "#rrggbb" (lowercase).
            QString hex = c.name(QColor::HexRgb);
            if (noHash) hex.remove(0, 1);
            return uppercaseHex ? hex.toUpper() : hex;
        }
    }
}

Format nextFormat(Format f)
{
    switch (f) {
        case Format::Hex:  return Format::Rgb;
        case Format::Rgb:  return Format::Hsv;
        case Format::Hsv:  return Format::Hsl;
        case Format::Hsl:  return Format::Hsi;
        case Format::Hsi:  return Format::Cmyk;
        case Format::Cmyk: return Format::Dec;
        case Format::Dec:
        default:           return Format::Hex;
    }
}

QString codeFor(Format f)
{
    switch (f) {
        case Format::Rgb:  return QStringLiteral("rgb");
        case Format::Hsv:  return QStringLiteral("hsv");
        case Format::Hsl:  return QStringLiteral("hsl");
        case Format::Hsi:  return QStringLiteral("hsi");
        case Format::Cmyk: return QStringLiteral("cmyk");
        case Format::Dec:  return QStringLiteral("dec");
        case Format::Hex:
        default:           return QStringLiteral("hex");
    }
}

QString labelFor(Format f)
{
    switch (f) {
        case Format::Rgb:  return QStringLiteral("RGB");
        case Format::Hsv:  return QStringLiteral("HSV");
        case Format::Hsl:  return QStringLiteral("HSL");
        case Format::Hsi:  return QStringLiteral("HSI");
        case Format::Cmyk: return QStringLiteral("CMYK");
        case Format::Dec:  return QStringLiteral("Dec");
        case Format::Hex:
        default:           return QStringLiteral("HEX");
    }
}

QVector<Format> parseFormats(const QString& csv)
{
    static const Format kAll[] = {Format::Hex, Format::Rgb, Format::Hsv, Format::Hsl,
                                  Format::Hsi, Format::Cmyk, Format::Dec};
    QVector<Format> out;
    const QStringList codes = csv.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& raw : codes) {
        const QString code = raw.trimmed().toLower();
        for (Format f : kAll) {
            if (codeFor(f) == code && !out.contains(f)) {
                out.append(f);
                break;
            }
        }
    }
    return out;
}

QColor sampleAt(const QImage& deviceImage, const QPoint& devicePx)
{
    if (deviceImage.isNull())
        return {};
    const int x = qBound(0, devicePx.x(), deviceImage.width() - 1);
    const int y = qBound(0, devicePx.y(), deviceImage.height() - 1);
    return deviceImage.pixelColor(x, y);
}

bool copyToClipboard(const QString& text)
{
    QClipboard* cb = QGuiApplication::clipboard();
    if (!cb)
        return false;
    cb->setText(text);
    return true;
}

QVector<QColor> basicPalette()
{
    // The exact 48-color "basic colors" grid (8 columns x 6 rows) used by Qt's
    // QColorDialog — R,G in {0x00,0x55,0xaa,0xff}, B in {0x00,0x7f,0xff} — so this
    // selector matches the dialog it replaces pixel-for-pixel.
    static const QRgb kRows[6][8] = {
        {0xff000000, 0xffaa0000, 0xff005500, 0xffaa5500, 0xff00aa00, 0xffaaaa00, 0xff00ff00, 0xffaaff00},
        {0xff00007f, 0xffaa007f, 0xff00557f, 0xffaa557f, 0xff00aa7f, 0xffaaaa7f, 0xff00ff7f, 0xffaaff7f},
        {0xff0000ff, 0xffaa00ff, 0xff0055ff, 0xffaa55ff, 0xff00aaff, 0xffaaaaff, 0xff00ffff, 0xffaaffff},
        {0xff550000, 0xffff0000, 0xff555500, 0xffff5500, 0xff55aa00, 0xffffaa00, 0xff55ff00, 0xffffff00},
        {0xff55007f, 0xffff007f, 0xff55557f, 0xffff557f, 0xff55aa7f, 0xffffaa7f, 0xff55ff7f, 0xffffff7f},
        {0xff5500ff, 0xffff00ff, 0xff5555ff, 0xffff55ff, 0xff55aaff, 0xffffaaff, 0xff55ffff, 0xffffffff},
    };
    QVector<QColor> out;
    out.reserve(48);
    for (const auto& row : kRows)
        for (QRgb c : row)
            out.append(QColor::fromRgb(c));
    return out;
}

QColor parseHtml(const QString& s)
{
    QString h = s.trimmed();
    if (h.startsWith(QLatin1Char('#')))
        h = h.mid(1);
    if (h.size() != 3 && h.size() != 6)
        return {};
    bool ok = false;
    h.toUInt(&ok, 16);                 // reject non-hex characters
    if (!ok)
        return {};
    return QColor::fromString(QLatin1Char('#') + h);   // handles #rgb and #rrggbb
}

QString toHtml(const QColor& c)
{
    return c.name(QColor::HexRgb);     // "#rrggbb", lowercase, no alpha
}

} // namespace ColorPicker
