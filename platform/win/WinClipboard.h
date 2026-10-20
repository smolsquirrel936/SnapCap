#pragma once

#include "platform/platform.h"

namespace platform {

// Windows clipboard backend. The base image is always placed; when extra formats
// are requested it takes over the clipboard in one session and adds an
// alpha-flattened CF_DIB (for legacy apps that mis-read alpha, e.g. Skype) and/or
// a temp file as CF_HDROP. With no extras it defers to Qt's QClipboard::setImage.
class WinClipboard : public IClipboard {
public:
    bool setImage(const QImage& img, const ImageClipboardOptions& opt = {}) override;
};

} // namespace platform
