#pragma once

#include <QImage>
#include <QString>

// Single-page PDF writer: a document whose only content is one image. Qt ships
// QPdfWriter, but it re-encodes every image as JPEG (/DCTDecode) with no API to
// turn that off — measured at max per-channel delta 24 on a UI screenshot, in a
// file 8x larger than the lossless form — so the format is encoded here instead,
// like TGA and GIF.
//
// Platform-agnostic (QImage in / bytes out), so it lives in snapcap_core and is
// headless unit-testable: the embedded stream inflates back to the exact source
// pixels, which is the property the whole encoder exists for.
//
// Known limits: no text layer (not searchable), one page, write-only. Raw RGB is
// Flated with no PNG predictor, so photos land ~1.5–3x a PNG's size (screenshots are
// fine: a 4K desktop shot measured 549 KB vs 587 KB as PNG); /DecodeParms
// << /Predictor 15 >> would close that gap. Acrobat caps a page at 14400 pt, which a
// scroll capture over ~14400 px (72 DPI) / ~19200 px (96 DPI) exceeds — Fit to A4
// is the workaround.
namespace PdfEncoder {

// How image pixels map to the page box. Only the declared page size changes —
// the embedded pixels are identical in all three.
enum class PageMode {
    Dpi72,   // 1 px = 1 pt. Page = image, largest nominal size.
    Dpi96,   // Windows screen DPI. Page = image, ~on-screen size at 100% zoom. Default.
    FitA4,   // A4 auto-oriented to the image aspect; image scaled to fit, centred.
};

// "dpi72" | "dpi96" | "a4" (the value of output/pdf/pageSize) -> PageMode.
// Anything else falls back to Dpi96, so a hand-edited config cannot break saving.
PageMode pageModeFromString(const QString& v);

// Write `img` to `path` as a one-page PDF, embedded LOSSLESSLY as a /FlateDecode
// DeviceRGB image. Any alpha channel is ignored — ImageExport flattens onto white
// first, since a PDF page is paper. Returns false on failure, with a technical
// reason in `error` when given.
bool write(const QImage& img, const QString& path, PageMode mode, QString* error = nullptr);

} // namespace PdfEncoder
