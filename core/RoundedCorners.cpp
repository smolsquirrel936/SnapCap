#include "core/RoundedCorners.h"

#include <QPainter>
#include <QPainterPath>

namespace RoundedCorners {

int clamp(int radius, const QSize& size)
{
    if (radius <= 0 || size.isEmpty())
        return 0;
    return qMin(radius, qMin(size.width(), size.height()) / 2);
}

QImage apply(const QImage& img, int radius)
{
    const int r = clamp(radius, img.size());
    if (img.isNull() || r <= 0)
        return img;

    // Premultiplied is the only format DestinationIn composites correctly, and it
    // is what QPainter would convert to internally anyway.
    QImage out = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // Erase the four corner slivers — the full rect MINUS the rounded rect. Painting
    // the rounded rect itself with DestinationIn would not work: that mode only
    // touches pixels the source actually covers, so the corners would stay opaque.
    // Antialiased Clear scales the erase by coverage, giving a smooth arc edge.
    const QRectF bounds(0, 0, out.width(), out.height());
    QPainterPath full;
    full.addRect(bounds);
    QPainterPath rounded;
    rounded.addRoundedRect(bounds, r, r);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);          // colour is irrelevant under Clear
    p.drawPath(full.subtracted(rounded));
    p.end();
    return out;
}

} // namespace RoundedCorners
