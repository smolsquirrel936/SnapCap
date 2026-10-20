#pragma once

#include "platform/platform.h"

#include <QString>

namespace platform {

// Tesseract-backed IOcr. OS-neutral (Tesseract + Leptonica are cross-platform), so
// this lives under platform/ocr rather than platform/win. Only compiled when the
// Tesseract dependency is available (SNAPCAP_HAVE_TESSERACT); createPlatform()
// leaves Platform::ocr null otherwise.
//
// Language data ("<code>.traineddata") is read from "<prefix>/tessdata/". `prefix`
// is the directory that *contains* the tessdata folder — the exe dir, for the
// portable layout. A fresh TessBaseAPI is created per recognize() call, so calls
// are independent and safe to run on a worker thread.
class TesseractOcr : public IOcr {
public:
    explicit TesseractOcr(QString prefixDir);

    OcrResult   recognize(const QImage& image, const QString& langs) override;
    QStringList availableLanguages() override;

private:
    QString m_prefix;    // dir containing "tessdata/"
    QString m_tessdata;  // "<prefix>/tessdata"
};

} // namespace platform
