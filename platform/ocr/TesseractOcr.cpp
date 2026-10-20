#include "platform/ocr/TesseractOcr.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>

#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>

namespace platform {

TesseractOcr::TesseractOcr(QString prefixDir)
    : m_prefix(std::move(prefixDir))
    , m_tessdata(m_prefix + QStringLiteral("/tessdata"))
{
}

OcrResult TesseractOcr::recognize(const QImage& image, const QString& langs)
{
    OcrResult result;
    if (image.isNull())
        return result;

    const QByteArray lang = (langs.isEmpty() ? QStringLiteral("eng") : langs).toUtf8();
    // Tesseract 5.x: the datapath points directly at the tessdata directory (unlike v4,
    // which took the parent and appended "tessdata/").
    const QByteArray prefix = QDir::toNativeSeparators(m_tessdata).toUtf8();

    tesseract::TessBaseAPI api;
    // Init returns 0 on success.
    if (api.Init(prefix.constData(), lang.constData()) != 0)
        return result;

    // Feed raw RGBA pixels directly (no Leptonica image codecs needed).
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    api.SetImage(rgba.constBits(), rgba.width(), rgba.height(),
                 /*bytes_per_pixel*/ 4, /*bytes_per_line*/ int(rgba.bytesPerLine()));
    api.SetSourceResolution(96);   // screen DPI; silences a Tesseract warning

    result.imageSize = image.size();
    if (api.Recognize(nullptr) != 0) {
        api.End();
        return result;
    }

    tesseract::ResultIterator* it = api.GetIterator();
    if (it) {
        OcrLine cur;
        do {
            if (it->Empty(tesseract::RIL_WORD))
                continue;
            // A new text line flushes the current one (words stay in reading order).
            if (it->IsAtBeginningOf(tesseract::RIL_TEXTLINE) && !cur.words.isEmpty()) {
                result.lines.append(cur);
                cur = OcrLine{};
            }
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (!it->BoundingBox(tesseract::RIL_WORD, &x1, &y1, &x2, &y2))
                continue;
            char* utf8 = it->GetUTF8Text(tesseract::RIL_WORD);
            QString text = utf8 ? QString::fromUtf8(utf8).trimmed() : QString();
            delete[] utf8;
            if (text.isEmpty())
                continue;
            // Tesseract's (x2,y2) is exclusive → width/height are the differences.
            cur.words.append(OcrWord{ QRect(x1, y1, x2 - x1, y2 - y1), text });
        } while (it->Next(tesseract::RIL_WORD));
        if (!cur.words.isEmpty())
            result.lines.append(cur);
        delete it;
    }

    api.End();
    return result;
}

QStringList TesseractOcr::availableLanguages()
{
    QStringList out;
    QDir dir(m_tessdata);
    const QStringList files = dir.entryList({ QStringLiteral("*.traineddata") }, QDir::Files);
    for (const QString& f : files)
        out.append(QFileInfo(f).completeBaseName());
    out.sort();
    return out;
}

} // namespace platform
