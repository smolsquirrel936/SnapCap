#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QStringList>

#include "core/codec/PdfEncoder.h"   // toFile's page-mode argument

// Output sinks for a captured/annotated image. Platform-agnostic (Qt Gui).
// Quick-save and print come in later Stage 2 increments. See
// claude-plan/Implementation-Plan.md (Stage 2, core/ImageExport).
namespace ImageExport {

// Put the image on the system clipboard. Needs a running QGuiApplication.
bool toClipboard(const QImage& img);

// Every extension toFile() can actually WRITE right now, lowercased — Qt's own
// writers plus the encoders we ship ourselves. This is a RUNTIME probe, not a
// fixed list: Qt's set depends on which image-format plugins are deployed (e.g.
// TIFF needs qtiff.dll from the qtimageformats module, and is simply absent when
// that module is not installed). The Save dialog builds its filter from this, so
// it can never offer a format that would then fail to write.
QStringList supportedSaveFormats();

// Save to a file; the format is inferred from the path's extension
// (PNG/JPG/BMP/TIFF/...). `quality` is passed to QImageWriter::setQuality when
// >= 0 (0..100); -1 leaves the format's default. Meaning is format-dependent:
// for JPEG it is lossy quality, for PNG it tunes the (lossless) compression
// level, and BMP/GIF ignore it. When `removeAlphaIfOpaque` is true and the image
// is fully opaque, its alpha channel is dropped before writing (lossless — see
// output/auto_alpha_channel_removal). `pdfPage` sizes the page of a .pdf write
// (output/pdf/pageSize) and is ignored by every other format. Returns false on
// null image / write failure; `error` (optional) then receives a human-readable
// reason to show the user.
bool toFile(const QImage& img, const QString& path, int quality = -1,
            bool removeAlphaIfOpaque = false, QString* error = nullptr,
            PdfEncoder::PageMode pdfPage = PdfEncoder::PageMode::Dpi96);

// The ICO format caps a single image at 256x256, and Qt's writer silently
// downscales anything larger while still reporting success — a 1920x1080 shot
// becomes a 256x144 icon with no error. Callers ask this FIRST so they can warn
// instead of quietly mangling the image. True only for an .ico path over the cap.
constexpr int kIcoMaxSide = 256;
bool icoWouldDownscale(const QImage& img, const QString& path);

// True when no pixel has alpha < 255 (a null or alpha-less image counts as opaque).
bool isFullyOpaque(const QImage& img);

// `img` composited over an opaque `bg`, dropping the alpha channel. toFile() runs
// this for formats that cannot store transparency (JPG/BMP/...), so a rounded-corner
// snip gets white corners there instead of the black Qt's writers would leave.
QImage flattenOnto(const QImage& img, const QColor& bg);

} // namespace ImageExport
