#pragma once

#include <QImage>
#include <QString>

// GIF89a writer. Qt's qgif plugin is read-only — it has never shipped an encoder —
// so the format from Features.md §5 has to be written here. Single frame only:
// this is a screenshot sink, not an animation tool.
//
// Two caveats are inherent to the format, not to this implementation:
//   * 256 colours max. Images within that budget are stored losslessly; larger
//     ones are median-cut and dithered (see core/codec/Quantize).
//   * 1-bit transparency. A pixel is either fully transparent or fully opaque,
//     so partial alpha collapses at Quantize::kAlphaThreshold.
//
// The LZW patent expired in 2004, so there is no licensing constraint here.
namespace GifEncoder {

// GIF stores width/height as unsigned 16-bit — a hard format limit.
constexpr int kMaxSide = 65535;

// Write `img` to `path`. Returns false on failure, with a technical reason in
// `error` when given.
bool write(const QImage& img, const QString& path, QString* error = nullptr);

} // namespace GifEncoder
