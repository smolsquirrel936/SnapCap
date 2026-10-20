#include "core/ClipboardContent.h"
#include "core/ColorPicker.h"

#include <QMimeData>
#include <QUrl>
#include <QImageReader>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QFileInfo>
#include <QStringList>
#include <QRegularExpression>
#include <QTextDocument>
#include <cmath>

namespace clip {

namespace {
bool looksLikeImageFile(const QString& path)
{
    static const QStringList exts = {"png", "jpg", "jpeg", "bmp", "gif", "tif", "tiff",
                                     "ico", "webp", "tga"};
    return exts.contains(QFileInfo(path).suffix().toLower());
}
} // namespace

std::optional<QColor> parseColor(const QString& sIn)
{
    const QString s = sIn.trimmed();
    if (s.isEmpty())
        return std::nullopt;

    // HEX form.
    if (s.startsWith(QLatin1Char('#'))) {
        const QColor c(s);
        return c.isValid() ? std::optional<QColor>(c) : std::nullopt;
    }

    // "r, g, b" (commas and/or whitespace).
    static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
    const QStringList parts = s.split(sep, Qt::SkipEmptyParts);
    if (parts.size() != 3)
        return std::nullopt;

    const bool floatValued = s.contains(QLatin1Char('.'));
    if (floatValued) {
        double v[3];
        for (int i = 0; i < 3; ++i) {
            bool ok = false;
            v[i] = parts[i].toDouble(&ok);
            if (!ok || v[i] < 0.0 || v[i] > 1.0)
                return std::nullopt;
        }
        return QColor::fromRgbF(v[0], v[1], v[2]);
    }
    int v[3];
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        v[i] = parts[i].toInt(&ok);
        if (!ok || v[i] < 0 || v[i] > 255)
            return std::nullopt;
    }
    return QColor(v[0], v[1], v[2]);
}

QImage renderText(const QString& text, const PasteOptions& opt)
{
    const QFont f = opt.textFont;
    const QFontMetrics fm(f);
    const int pad = qMax(0, opt.pageMargin);

    const int maxTextW = qMax(40, opt.maxWidth - pad * 2);
    const int flags = Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop
                    | (opt.forceHtmlWrap ? 0 : Qt::TextSingleLine);
    QRect br = fm.boundingRect(QRect(0, 0, maxTextW, 100000), flags, text);
    if (br.isEmpty())
        br = QRect(0, 0, fm.horizontalAdvance(QStringLiteral("…")), fm.height());

    const int w = qMin(opt.maxWidth, br.width() + pad * 2);
    const int h = br.height() + pad * 2;
    QImage img(qMax(1, w), qMax(1, h), QImage::Format_ARGB32);
    img.fill(opt.bgColor);

    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(f);
    p.setPen(opt.textColor);
    p.drawText(QRect(pad, pad, w - pad * 2, h - pad * 2), flags, text);
    p.end();
    return img;
}

QImage renderHtml(const QString& html, const PasteOptions& opt)
{
    const int pad = qMax(0, opt.pageMargin);
    const int maxTextW = qMax(40, opt.maxWidth - pad * 2);

    QTextDocument doc;
    doc.setDefaultFont(opt.textFont);
    doc.setDocumentMargin(0);
    doc.setHtml(html);

    // Wrap at maxTextW, then shrink back to the ideal width so short fragments don't
    // get a full-width page (mirrors renderText()'s min() sizing).
    doc.setTextWidth(maxTextW);
    const int textW = qMax(1, int(std::ceil(qMin(doc.idealWidth(), qreal(maxTextW)))));
    doc.setTextWidth(textW);
    const int textH = qMax(1, int(std::ceil(doc.size().height())));

    QImage img(textW + pad * 2, textH + pad * 2, QImage::Format_ARGB32);
    img.fill(opt.bgColor);

    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.translate(pad, pad);
    p.setPen(opt.textColor);      // default ink; colors in the markup win over it
    doc.drawContents(&p);
    p.end();
    return img;
}

QImage renderColorCard(const QColor& c, bool uppercaseHex)
{
    const int swatch = 140;
    const int barH = 40;
    QImage img(swatch, swatch + barH, QImage::Format_ARGB32);
    img.fill(Qt::white);

    QPainter p(&img);
    p.fillRect(QRect(0, 0, swatch, swatch), c);

    QFont f;
    f.setPointSize(11);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::black);
    const QString label = ColorPicker::format(c, ColorPicker::Format::Hex, uppercaseHex);
    p.drawText(QRect(0, swatch, swatch, barH), Qt::AlignCenter, label);
    p.end();
    return img;
}

QImage renderFilePaths(const QStringList& files, const PasteOptions& opt)
{
    return renderText(files.join(QLatin1Char('\n')), opt);
}

Content classify(const QMimeData& mime, const PasteOptions& opt)
{
    Content out;

    // 1. Image data.
    if (mime.hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime.imageData());
        if (!img.isNull()) {
            out.kind = Kind::Image;
            out.image = img.convertToFormat(QImage::Format_ARGB32);
            return out;
        }
    }

    // 2. File paths (a single image file → that image; otherwise render the paths).
    if (mime.hasUrls()) {
        QStringList files;
        for (const QUrl& u : mime.urls())
            if (u.isLocalFile())
                files << u.toLocalFile();
        if (!files.isEmpty()) {
            // A copied image file pins the picture itself — that's an image paste, not a
            // path rendered as text, so neither pasteFilePaths nor textToImage gates it.
            if (files.size() == 1 && looksLikeImageFile(files.first())) {
                QImageReader r(files.first());
                const QImage img = r.read();
                if (!img.isNull()) {
                    out.kind = Kind::Image;
                    out.image = img.convertToFormat(QImage::Format_ARGB32);
                    out.files = files;
                    return out;
                }
            }
            if (!opt.pasteFilePaths)
                return out;             // path-list clipboards ignored when disabled
            if (!opt.textToImage)
                return out;             // drawing the paths *is* a text→image conversion
            out.kind = Kind::FilePaths;
            out.files = files;
            out.image = renderFilePaths(files, opt);
            return out;
        }
    }

    // 3. Text — a color string becomes a color card; otherwise plain/HTML text.
    if (mime.hasText()) {
        const QString text = mime.text();
        // A color card is its own feature (a swatch, not rendered glyphs), so it stays
        // available even when the text→image conversion is switched off.
        if (const auto col = parseColor(text)) {
            out.kind = Kind::Color;
            out.color = *col;
            out.text = text;
            out.image = renderColorCard(*col, opt.uppercaseHex);
            return out;
        }
        if (!opt.textToImage)
            return out;
        out.text = text;
        if (mime.hasHtml() && !opt.ignoreHtml) {
            out.kind = Kind::Html;
            out.image = renderHtml(mime.html(), opt);
        } else {
            out.kind = Kind::PlainText;
            out.image = renderText(text, opt);
        }
        return out;
    }

    return out;   // None
}

} // namespace clip
