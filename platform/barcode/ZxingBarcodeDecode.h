#pragma once

#include "platform/platform.h"

namespace platform {

// ZXing-C++-backed IBarcodeDecode. OS-neutral (ZXing is cross-platform), so this lives
// under platform/barcode rather than platform/win. Only compiled when the ZXing
// dependency is available (SNAPCAP_HAVE_ZXING); createPlatform() leaves
// Platform::barcode null otherwise.
//
// Stateless — decodeFirst() builds its own view over the caller's image, so calls
// are independent and safe to run on a worker thread.
class ZxingBarcodeDecode : public IBarcodeDecode {
public:
    QString decodeFirst(const QImage& image) override;
};

} // namespace platform
