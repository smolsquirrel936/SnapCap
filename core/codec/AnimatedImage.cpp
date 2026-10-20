#include "core/codec/AnimatedImage.h"

#include <QDebug>
#include <QFileInfo>
#include <QImageReader>

namespace anim {

namespace {
// Legacy GIFs store 0 or 1 (hundredths of a second) meaning "as fast as possible";
// every browser floors those at 100 ms, so an old banner animates at its intended
// speed instead of strobing.
constexpr int kMinDelayMs = 10;
constexpr int kDefaultDelayMs = 100;

int clampDelay(int ms) { return ms <= kMinDelayMs ? kDefaultDelayMs : ms; }
}

Animation load(const QString& path)
{
    Animation out;
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        out.status = Animation::Status::Unreadable;
        return out;
    }

    QImageReader reader(path);
    if (!reader.canRead()) {
        qWarning() << "[anim] not readable:" << path << reader.errorString();
        out.status = Animation::Status::Unreadable;
        return out;
    }
    const int count = reader.imageCount();
    if (!reader.supportsAnimation() || count < 2)
        return out;                              // NotAnimated: a plain still

    const QSize size = reader.size();
    const qint64 pixels = qint64(count) * size.width() * size.height();
    if (pixels > kMaxAnimationPixels) {
        qWarning() << "[anim] too large to play:" << path << count << "frames of" << size;
        out.status = Animation::Status::TooLarge;
        return out;
    }

    for (int i = 0; i < count; ++i) {
        const QImage frame = reader.read();
        if (frame.isNull())
            break;                               // truncated file — keep what decoded
        if (!out.frames.isEmpty() && frame.size() != out.frames.first().size()) {
            qWarning() << "[anim] frame" << i << "changes size — stopping at" << out.frames.size();
            break;
        }
        out.frames.append(frame.convertToFormat(QImage::Format_ARGB32));
        out.delaysMs.append(clampDelay(reader.nextImageDelay()));
    }

    if (out.frames.size() < 2) {
        out.frames.clear();
        out.delaysMs.clear();
        return out;                              // NotAnimated
    }
    out.status = Animation::Status::Loaded;
    return out;
}

} // namespace anim
