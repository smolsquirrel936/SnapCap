#pragma once

#include <QImage>
#include <QSize>

// Cut a captured region's corners into quarter-circle arcs. Pure pixel work on a
// finished (already annotated + flattened) image, so it stays testable and knows
// nothing about the snip overlay. Radii here are in the image's OWN pixels — for
// the snip pipeline that is capture px, pre-scaled from logical by the caller the
// same way pen sizes are (SnipOverlay::captureStyle).
namespace RoundedCorners {

// The largest radius `size` can actually hold: half the shorter side. A bigger
// request would make opposite arcs overlap and pinch the shape into a lens.
// Negative radii clamp to 0 (= square corners).
int clamp(int radius, const QSize& size);

// `img` with everything outside the rounded rectangle made transparent.
// radius <= 0 (or a null image) returns `img` untouched, so callers can pass the
// user's value unconditionally. The radius is clamped to the image's own size.
QImage apply(const QImage& img, int radius);

} // namespace RoundedCorners
