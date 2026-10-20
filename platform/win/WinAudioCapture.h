#pragma once

#include <QMutex>
#include <QString>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace platform {

// One WASAPI audio endpoint, captured into a FIFO of 48 kHz stereo float samples.
//
// Two modes, same class:
//   * loopback (`Loopback`) — taps the default PLAYBACK device, i.e. everything the
//     PC is playing ("desktop audio");
//   * microphone (`Microphone`) — the default recording device.
//
// Whatever the endpoint's native rate/format/channel count, output is always
// interleaved stereo float at recording::kAudioSampleRate, so the recorder can sum
// two of these sample-for-sample without caring where they came from.
//
// **Reads are pull-based and zero-pad on underrun.** That is the whole silence
// story: a loopback endpoint delivers NOTHING while nothing is playing, so anything
// that waited for its packets would stall the muxer and desync the file. Instead the
// recorder asks for exactly the samples one video frame is worth, and short reads are
// filled with silence — which is also what covers a slow mic or a device glitch.
class WinAudioCapture {
public:
    enum class Source { Loopback, Microphone };

    WinAudioCapture();
    ~WinAudioCapture();

    // Opens the endpoint and starts the capture thread. False if the device is
    // missing or refuses the format — recording then simply continues without this
    // source rather than failing.
    bool start(Source source);
    void stop();
    bool running() const { return m_running.load(); }

    // Copy `frames` interleaved stereo frames into `dst` (2 * frames floats),
    // zero-padding whatever the FIFO is short. Consumed samples are dropped.
    void read(float* dst, int frames);

    // Throw away everything buffered — used on pause, so resuming does not replay
    // the audio captured while paused.
    void discard();

    QString error() const { return m_error; }

private:
    void run(Source source);

    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_running{false};
    QMutex m_lock;
    std::vector<float> m_fifo;      // interleaved stereo, guarded by m_lock
    QString m_error;
};

} // namespace platform
