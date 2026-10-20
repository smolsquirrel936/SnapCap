#pragma once

#include "platform/platform.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <atomic>
#include <deque>
#include <memory>
#include <thread>

namespace platform {

// libav*-based playback for the post-recording preview. OS-neutral: the speaker
// endpoint arrives as an injected IAudioSink.
//
// Two clocks, one rule — **audio is the master when the file has any**, because a
// stutter is far more obvious in sound than in picture. Video frames are decoded
// ahead into a small bounded queue and presented when the audio clock reaches their
// timestamp; with no audio track the wall clock stands in.
//
// The decode thread never waits on presentation: it fills the queue (blocking only
// when it is full) and pushes audio at the sink. A GUI-thread timer does the
// presenting. That decoupling is what keeps audio from starving while a frame waits.
//
// Ours rather than Qt Multimedia, which is in the kit and would be ~80 lines: its
// FFmpeg backend brings its own av*.dll set, putting two FFmpeg copies (+40–60 MB)
// beside one exe.
class FfmpegPlayer : public QObject, public IMediaPlayer {
    Q_OBJECT
public:
    explicit FfmpegPlayer(QObject* parent = nullptr);
    ~FfmpegPlayer() override;

    bool open(const QString& path, Callbacks cb) override;
    void play() override;
    void pause() override;
    bool isPlaying() const override { return m_playing.load(); }
    void seek(qint64 ms) override;
    qint64 durationMs() const override { return m_durationMs; }
    qint64 positionMs() const override { return m_positionMs; }
    bool hasAudio() const override { return m_hasAudio; }
    QSize videoSize() const override { return m_videoSize; }
    QString error() const override { return m_error; }

private:
    struct Decoder;                       // libav* state, kept out of the header

    void run();                           // decode thread
    void present();                       // GUI-thread timer: show what is due now
    qint64 clockMs() const;               // audio clock, or the wall clock without audio
    void closeAll();
    bool openInput(Decoder& d);           // demuxer + codecs only; also used to recover
                                          // from a failed seek on an unseekable format

    std::unique_ptr<Decoder> m_dec;
    std::unique_ptr<IAudioSink> m_sink;
    std::unique_ptr<std::thread> m_thread;
    QTimer* m_presentTimer = nullptr;

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_playing{false};
    std::atomic<qint64> m_seekTo{-1};     // >=0 = a seek is pending
    std::atomic<qint64> m_baseMs{0};      // position the current clock epoch starts at
    std::atomic<bool> m_ended{false};
    QString m_path;                        // kept so a failed seek can reopen the input

    // Decoded-ahead frames, oldest first. Bounded: the decoder blocks when it is
    // full, which is what paces decoding to real time.
    struct Pending { QImage image; qint64 ptsMs; };
    QMutex m_queueLock;
    std::deque<Pending> m_queue;

    Callbacks m_cb;
    QString m_error;
    qint64 m_durationMs = 0;
    qint64 m_positionMs = 0;
    bool m_hasAudio = false;
    QSize m_videoSize;
    qint64 m_wallStart = 0;               // ms since epoch when the clock epoch began
};

} // namespace platform
