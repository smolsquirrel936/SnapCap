#pragma once

#include "platform/platform.h"

#include <QMutex>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

struct IAudioClient;
struct IAudioRenderClient;

namespace platform {

// WASAPI speaker output for the preview player (the counterpart of
// WinAudioCapture). Shared mode at the endpoint's own mix format, with our
// interleaved float frames resampled into it.
//
// The player writes into a ring buffer; a small thread keeps the endpoint fed from
// it. `playedFrames()` is derived from what has actually been handed to the device
// minus what is still queued in it, which is what makes it usable as the master
// clock rather than just "what we submitted".
class WinAudioRender : public IAudioSink {
public:
    WinAudioRender();
    ~WinAudioRender() override;

    bool start(int sampleRate, int channels) override;
    void stop() override;
    int  write(const float* interleaved, int frames) override;
    int  space() const override;
    qint64 playedFrames() const override;
    void reset() override;

private:
    void run();

    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_running{false};
    std::atomic<qint64> m_written{0};      // frames accepted from the player
    std::atomic<qint64> m_consumed{0};     // frames handed to the endpoint

    mutable QMutex m_lock;
    std::vector<float> m_ring;             // interleaved, source rate/channels
    int m_srcRate = 48000;
    int m_srcCh = 2;
};

} // namespace platform
