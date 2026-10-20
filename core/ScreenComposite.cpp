#include "core/ScreenComposite.h"

#include <QPainter>

namespace ScreenComposite {

QImage compositeSelection(const QList<ScreenShot>& screens, const QRect& selCapture)
{
    if (selCapture.isEmpty())
        return {};

    bool touchesAny = false;
    for (const ScreenShot& s : screens)
        if (!s.shot.isNull() && selCapture.intersects(s.captureGeo)) {
            touchesAny = true;
            break;
        }
    if (!touchesAny)
        return {};                       // selection covers no captured screen

    QImage out(selCapture.size(), QImage::Format_ARGB32);   // capture px, 1:1
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const ScreenShot& s : screens) {
        if (s.shot.isNull())
            continue;
        const QRect overlap = selCapture.intersected(s.captureGeo);   // capture px
        if (overlap.isEmpty())
            continue;

        // Source: this screen's frozen pixels for the overlap (captureGeo == shot 1:1).
        const QRect src = overlap.translated(-s.captureGeo.topLeft()).intersected(s.shot.rect());
        // Destination: the overlap placed inside the output.
        const QRect dst = overlap.translated(-selCapture.topLeft());
        p.drawImage(dst, s.shot, src);    // integer 1:1, no rescale
    }
    p.end();

    out.setDevicePixelRatio(1.0);
    return out;
}

} // namespace ScreenComposite
