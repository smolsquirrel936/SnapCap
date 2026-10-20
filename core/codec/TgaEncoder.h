#pragma once

#include <QImage>
#include <QString>

// Truevision TGA writer. Qt has no TGA *writer* at all — its qtga plugin is
// read-only, and ships only with the optional qtimageformats module — so the
// format from Features.md §5 has to be encoded here. Uncompressed true-colour
// (type 2) only: RLE would shrink flat UI screenshots nicely but is not worth
// the complexity for a format users pick for interchange, not for size.
//
// Platform-agnostic (QImage in/bytes out), so it lives in snapcap_core and is
// headless unit-testable. Note the tests must verify the bytes by hand: without
// the qtga plugin Qt cannot read a TGA back, so no round-trip is available.
namespace TgaEncoder {

// TGA stores width/height as unsigned 16-bit, so this is a hard format limit.
constexpr int kMaxSide = 65535;

// Write `img` to `path`. 32-bit BGRA when the image has an alpha channel,
// 24-bit BGR when it does not (ImageExport's alpha-removal runs first, so an
// opaque shot lands in the smaller form). Returns false on failure, with a
// technical reason in `error` when given.
bool write(const QImage& img, const QString& path, QString* error = nullptr);

} // namespace TgaEncoder
