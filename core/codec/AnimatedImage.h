#pragma once

#include <QImage>
#include <QString>
#include <QVector>

// Multi-frame image reader — the playback side of an animated GIF (or any format
// whose Qt plugin reports animation support, e.g. animated WebP). The read side of
// core/codec/GifEncoder, which only ever writes one frame.
//
// Every frame is decoded up front into memory, so a pathological file is rejected by
// a pixel budget rather than swallowing RAM (same guard shape as core/barcode's
// kMaxDecodePixels). Loop count is deliberately ignored: a pin loops forever.
namespace anim {

// frames * width * height. 64M px ≈ 256 MB as ARGB32.
constexpr qint64 kMaxAnimationPixels = 64'000'000;

struct Animation {
    enum class Status {
        NotAnimated,    // readable, but a single frame (or a format with no animation)
        Loaded,
        TooLarge,       // more than kMaxAnimationPixels — caller pins the still instead
        Unreadable,     // missing file / not an image
    };

    Status status = Status::NotAnimated;
    QVector<QImage> frames;     // ARGB32, all the size of frame 0
    QVector<int> delaysMs;      // one per frame, parallel to `frames`

    bool isValid() const { return status == Status::Loaded; }
};

// Decode every frame of `path`. Only a Loaded result carries frames.
Animation load(const QString& path);

} // namespace anim
