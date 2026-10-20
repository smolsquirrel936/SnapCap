#pragma once

#include <QImage>
#include <QList>
#include <QRgb>

// Colour reduction to a <=256-entry palette, for the GIF encoder (Features.md §5).
//
// Two paths, because screenshots are not photos: UI, text and diagram captures
// very often use fewer than 256 distinct colours, and those must come out
// PIXEL-EXACT rather than being needlessly dithered. Only when an image really
// exceeds the budget does it fall back to median-cut + Floyd-Steinberg.
//
// Platform-agnostic (QImage in, indices out), so it lives in snapcap_core and is
// headless unit-testable.
namespace Quantize {

// A pixel is treated as transparent below this alpha. GIF has only 1-bit
// transparency, so partial alpha has to collapse one way or the other.
constexpr int kAlphaThreshold = 128;

struct Indexed
{
    QList<quint8> pixels;          // width*height palette indices, row-major
    QList<QRgb>   palette;         // <=256 entries; index 0..palette.size()-1
    int  transparentIndex = -1;    // -1 when the image had no transparent pixel
    bool exact = false;            // true when no colour was altered (lossless)
    int  width = 0;
    int  height = 0;
};

// Reduce `img` to at most `maxColors` palette entries (including the reserved
// transparency slot, when the image needs one). Returns an empty Indexed for a
// null image.
Indexed toIndexed(const QImage& img, int maxColors = 256);

} // namespace Quantize
