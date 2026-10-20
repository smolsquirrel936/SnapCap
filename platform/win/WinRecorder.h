#pragma once

#include "platform/platform.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <thread>

namespace platform {

// FFmpeg-backed screen recorder.
//
// Frames come from FFmpeg's own grabbers rather than our BitBlt path: `ddagrab`
// (libavfilter, DXGI Desktop Duplication) when the region sits on one monitor,
// falling back to `gdigrab` (libavdevice) when it spans monitors or DDA refuses to
// start. That is what makes accelerated video record as pixels instead of black.
//
// The encode loop runs on its own thread; callbacks are marshaled back to the GUI
// thread, so the caller never touches libav* state.
class WinRecorder : public QObject, public IRecorder {
    Q_OBJECT
public:
    explicit WinRecorder(QObject* parent = nullptr);
    ~WinRecorder() override;

    bool start(const Request& req, Callbacks cb) override;
    void stop() override;
    void pause() override;
    void resume() override;
    bool isPaused() const override;
    bool isRecording() const override;
    void setOverlay(const QImage& overlay) override;
    QString error() const override { return m_error; }

private:
    struct Pipeline;                        // libav* state, kept out of the header

    // The worker: builds the graph, pumps frames until m_stop, then finalizes.
    void run(Request req);
    // Try ddagrab for a single-monitor region, else gdigrab. Sets m_error and
    // returns false only when NEITHER works.
    bool openSource(Pipeline& p, const Request& req);
    bool openEncoder(Pipeline& p, const Request& req);
    bool openOutput(Pipeline& p, const Request& req);
    // Opens the audio sources + encoder. Returns true even when audio could not be
    // opened at all: a missing mic degrades to a silent video, it never fails the take.
    bool openAudio(Pipeline& p, const Request& req);
    // GIF output: fps → scale → palettegen/paletteuse, fed by a buffersrc so it works
    // with both grabbers. Sets p.gifSize, which the gif encoder is then opened at.
    bool openGifChain(Pipeline& p, const Request& req, int srcW, int srcH);

    void finish(bool ok, const QString& path, const QString& message);

    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    // The annotation layer burned into each frame. Written by the GUI thread,
    // read by the encode loop — hence the mutex. QImage is implicitly shared, so
    // the copy taken under the lock is cheap.
    mutable QMutex m_overlayLock;
    QImage m_overlay;
    Callbacks m_cb;
    QString m_error;
};

} // namespace platform
