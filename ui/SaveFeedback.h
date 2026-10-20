#pragma once

#include <QString>

class QImage;
class QWidget;

// User-facing feedback for the manual "Save as..." path, shared by the snip
// overlay and the pin window so both report failures identically. The decision
// logic itself lives in core/ImageExport; this is only the dialogs.
namespace SaveFeedback {

// Warn before an ICO save that would silently shrink the image, since Qt's ICO
// writer downscales anything over 256px and still reports success. Returns true
// when the save should go ahead (always true for non-ICO paths and small images,
// so callers can just guard on it).
bool confirmIcoDownscale(QWidget* parent, const QImage& img, const QString& path);

// Report a save that failed, with the technical reason from ImageExport.
void warnSaveFailed(QWidget* parent, const QString& path, const QString& reason);

} // namespace SaveFeedback
