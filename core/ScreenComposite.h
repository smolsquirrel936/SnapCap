#pragma once

#include <QImage>
#include <QRect>
#include <QList>

class QScreen;

// One captured monitor: its global logical geometry, the frozen pixels (device
// resolution), and its device-pixel ratio. Used by the multi-monitor snip
// overlay to stitch a selection that may span several screens.
struct ScreenShot {
    QScreen* screen = nullptr;   // source screen (null in unit tests)
    QRect geo;                   // global logical geometry (QScreen::geometry space)
    QRect captureGeo;            // native physical-px rect — canonical snip space (mixed-DPI fix)
    QImage shot;                 // frozen pixels, device resolution (geo.size * dpr)
    qreal dpr = 1.0;             // device-pixel ratio of this screen
};

namespace ScreenComposite {

// Stitch the part of every screen covered by `selCapture` (native physical-pixel
// coords — the canonical snip space) into a single image. Each screen's captured
// pixels are copied 1:1 (no rescale), so mixed-DPI monitors pack edge-adjacent with
// no phantom gap. Output is `selCapture.size()` px, dpr 1.0, ready for clipboard/file
// export; pixels covered by no screen stay transparent (a genuine offset gap, not the
// old logical-coordinate artifact). The coordinate model is core/ScreenLayout.
QImage compositeSelection(const QList<ScreenShot>& screens, const QRect& selCapture);

} // namespace ScreenComposite
