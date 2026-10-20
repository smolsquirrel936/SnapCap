#include "platform/media/FfmpegPlayer.h"

#include "core/recording/RecordingSpec.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <QMutexLocker>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <vector>

namespace platform {
namespace {
constexpr int kMaxQueued = 8;             // decoded-ahead frames; also the decoder's leash
}

struct FfmpegPlayer::Decoder {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* vdec = nullptr;
    AVCodecContext* adec = nullptr;
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    int vIndex = -1;
    int aIndex = -1;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    std::vector<float> audioBuf;          // interleaved float, ready for the sink

    ~Decoder()
    {
        if (sws) sws_freeContext(sws);
        if (swr) swr_free(&swr);
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (vdec) avcodec_free_context(&vdec);
        if (adec) avcodec_free_context(&adec);
        if (fmt) avformat_close_input(&fmt);
    }
};

FfmpegPlayer::FfmpegPlayer(QObject* parent) : QObject(parent)
{
    m_presentTimer = new QTimer(this);
    m_presentTimer->setInterval(8);       // ~120 Hz: finer than any frame rate we record
    connect(m_presentTimer, &QTimer::timeout, this, [this] { present(); });
}

FfmpegPlayer::~FfmpegPlayer()
{
    closeAll();
}

void FfmpegPlayer::closeAll()
{
    m_stop.store(true);
    m_playing.store(false);
    if (m_thread && m_thread->joinable())
        m_thread->join();
    m_thread.reset();
    if (m_presentTimer)
        m_presentTimer->stop();
    if (m_sink)
        m_sink->stop();
    m_sink.reset();
    m_dec.reset();
    QMutexLocker lock(&m_queueLock);
    m_queue.clear();
}

// Opens the demuxer and codecs into `d`, leaving everything the format does not own
// (sws/swr/sink) alone. Split out of open() because it is also how a failed seek
// recovers: some formats we write cannot seek at all, and reopening from the top is
// the only way back — see the seek handling in run().
bool FfmpegPlayer::openInput(Decoder& d)
{
    const QByteArray p = QDir::toNativeSeparators(m_path).toUtf8();
    if (avformat_open_input(&d.fmt, p.constData(), nullptr, nullptr) < 0
        || avformat_find_stream_info(d.fmt, nullptr) < 0) {
        m_error = QStringLiteral("could not open the recording");
        return false;
    }

    d.vIndex = -1;
    d.aIndex = -1;
    for (unsigned i = 0; i < d.fmt->nb_streams; ++i) {
        const AVMediaType t = d.fmt->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_VIDEO && d.vIndex < 0) d.vIndex = int(i);
        if (t == AVMEDIA_TYPE_AUDIO && d.aIndex < 0) d.aIndex = int(i);
    }
    if (d.vIndex < 0) {
        m_error = QStringLiteral("the recording has no video track");
        return false;
    }

    auto openStream = [&](int index, AVCodecContext** out) {
        AVStream* st = d.fmt->streams[index];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec)
            return false;
        *out = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(*out, st->codecpar);
        return avcodec_open2(*out, codec, nullptr) >= 0;
    };
    if (!openStream(d.vIndex, &d.vdec)) {
        m_error = QStringLiteral("no decoder for this video");
        return false;
    }
    m_hasAudio = d.aIndex >= 0 && openStream(d.aIndex, &d.adec);
    return true;
}

bool FfmpegPlayer::open(const QString& path, Callbacks cb)
{
    closeAll();
    m_cb = std::move(cb);
    m_error.clear();
    m_stop.store(false);
    m_ended.store(false);
    m_positionMs = 0;
    m_baseMs.store(0);

    m_path = path;
    auto dec = std::make_unique<Decoder>();
    if (!openInput(*dec))
        return false;

    m_videoSize = QSize(dec->vdec->width, dec->vdec->height);
    m_durationMs = dec->fmt->duration > 0
        ? qint64(dec->fmt->duration) * 1000 / AV_TIME_BASE
        : 0;

    if (m_hasAudio) {
        AVChannelLayout out;
        av_channel_layout_default(&out, recording::kAudioChannels);
        if (swr_alloc_set_opts2(&dec->swr, &out, AV_SAMPLE_FMT_FLT,
                                recording::kAudioSampleRate, &dec->adec->ch_layout,
                                dec->adec->sample_fmt, dec->adec->sample_rate, 0, nullptr) < 0
            || swr_init(dec->swr) < 0) {
            m_hasAudio = false;           // fall back to silent playback, not failure
        }
        av_channel_layout_uninit(&out);
    }
    if (m_hasAudio) {
        m_sink = createAudioSink();
        if (!m_sink || !m_sink->start(recording::kAudioSampleRate, recording::kAudioChannels)) {
            m_sink.reset();
            m_hasAudio = false;
        }
    }

    dec->frame = av_frame_alloc();
    dec->packet = av_packet_alloc();
    m_dec = std::move(dec);

    m_thread = std::make_unique<std::thread>([this] { run(); });
    m_presentTimer->start();
    return true;
}

void FfmpegPlayer::play()
{
    if (!m_dec || m_playing.load())
        return;
    if (m_ended.load()) {                 // replay from the top rather than sitting at the end
        seek(0);
        m_ended.store(false);
    }
    m_baseMs.store(m_positionMs);
    m_wallStart = QDateTime::currentMSecsSinceEpoch();
    if (m_sink)
        m_sink->reset();
    m_playing.store(true);
}

void FfmpegPlayer::pause()
{
    m_playing.store(false);
    m_baseMs.store(m_positionMs);
    if (m_sink)
        m_sink->reset();                  // drop queued audio so resume doesn't replay it
}

void FfmpegPlayer::seek(qint64 ms)
{
    if (!m_dec)
        return;
    m_seekTo.store(qBound<qint64>(0, ms, m_durationMs > 0 ? m_durationMs : ms));
    m_positionMs = m_seekTo.load();
    m_baseMs.store(m_positionMs);
    m_wallStart = QDateTime::currentMSecsSinceEpoch();
    m_ended.store(false);
}

qint64 FfmpegPlayer::clockMs() const
{
    if (!m_playing.load())
        return m_baseMs.load();
    if (m_sink)                            // audio master
        return m_baseMs.load() + m_sink->playedFrames() * 1000 / recording::kAudioSampleRate;
    return m_baseMs.load() + (QDateTime::currentMSecsSinceEpoch() - m_wallStart);
}

void FfmpegPlayer::present()
{
    const qint64 now = clockMs();
    QImage due;
    qint64 duePts = -1;
    {
        QMutexLocker lock(&m_queueLock);
        // Drop anything already overdue and show the newest frame that is due, so a
        // stall catches up rather than playing the backlog in slow motion.
        while (!m_queue.empty() && m_queue.front().ptsMs <= now) {
            due = m_queue.front().image;
            duePts = m_queue.front().ptsMs;
            m_queue.pop_front();
        }
    }
    if (duePts >= 0) {
        m_positionMs = duePts;
        if (m_cb.onFrame)
            m_cb.onFrame(due, duePts);
    } else if (m_playing.load()) {
        m_positionMs = qMin(now, m_durationMs > 0 ? m_durationMs : now);
    }

    if (m_ended.load() && m_queue.empty() && m_playing.load()) {
        m_playing.store(false);
        m_baseMs.store(m_positionMs);
        if (m_cb.onEnded)
            m_cb.onEnded();
    }
}

void FfmpegPlayer::run()
{
    Decoder& d = *m_dec;
    std::vector<uint8_t> rgb;
    // >=0 while catching up to a seek target on a format that could not seek: frames
    // before it are decoded (they have to be) but thrown away rather than shown.
    qint64 skipToMs = -1;

    while (!m_stop.load()) {
        const qint64 want = m_seekTo.exchange(-1);
        if (want >= 0) {
            const int64_t ts = av_rescale_q(want, AVRational{1, 1000}, AV_TIME_BASE_Q);
            if (av_seek_frame(d.fmt, -1, ts, AVSEEK_FLAG_BACKWARD) < 0) {
                // Not every format we write can seek: the GIF demuxer cannot, and a
                // failed seek leaves the reader positioned past the header, after which
                // nothing decodes at all (measured: 0 frames, then EOF). Reopening from
                // the top is the only way back, so do that and decode forward instead.
                // This is also what makes replay-after-end work for GIF, since play()
                // seeks to 0.
                avcodec_free_context(&d.vdec);
                if (d.adec) avcodec_free_context(&d.adec);
                avformat_close_input(&d.fmt);
                if (!openInput(d)) {
                    m_ended.store(true);
                    break;                    // unrecoverable; leave the thread cleanly
                }
                skipToMs = want;              // decode-and-discard up to the target
                m_ended.store(false);
            }
            avcodec_flush_buffers(d.vdec);
            if (d.adec) avcodec_flush_buffers(d.adec);
            QMutexLocker lock(&m_queueLock);
            m_queue.clear();
        }

        if (!m_playing.load() && m_seekTo.load() < 0) {
            // Paused: keep the thread alive but idle. One frame is still decoded after
            // a seek (above) so the display shows where you scrubbed to.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            bool needFrame = false;
            {
                QMutexLocker lock(&m_queueLock);
                needFrame = m_queue.empty();
            }
            if (!needFrame)
                continue;
        }

        // Back off while the queue is full — this is what paces decoding to real time.
        {
            QMutexLocker lock(&m_queueLock);
            if (int(m_queue.size()) >= kMaxQueued) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
        }

        if (av_read_frame(d.fmt, d.packet) < 0) {
            m_ended.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (d.packet->stream_index == d.vIndex) {
            if (avcodec_send_packet(d.vdec, d.packet) >= 0) {
                while (avcodec_receive_frame(d.vdec, d.frame) >= 0) {
                    // pts first: while catching up after a reopen there is no point
                    // scaling frames that will be discarded.
                    const AVRational tbEarly = d.fmt->streams[d.vIndex]->time_base;
                    const qint64 framePts = d.frame->best_effort_timestamp != AV_NOPTS_VALUE
                        ? qint64(av_rescale_q(d.frame->best_effort_timestamp, tbEarly,
                                              AVRational{1, 1000}))
                        : m_positionMs;
                    if (skipToMs >= 0) {
                        if (framePts + 1 < skipToMs)
                            continue;                 // still before the target
                        skipToMs = -1;                // arrived: show this one
                    }
                    if (!d.sws) {
                        d.sws = sws_getContext(d.frame->width, d.frame->height,
                                               AVPixelFormat(d.frame->format),
                                               d.frame->width, d.frame->height,
                                               AV_PIX_FMT_RGB32, SWS_BILINEAR,
                                               nullptr, nullptr, nullptr);
                        if (!d.sws)
                            continue;                 // never hand sws_scale a null context
                    }
                    QImage img(d.frame->width, d.frame->height, QImage::Format_RGB32);
                    uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
                    int stride[4] = {int(img.bytesPerLine()), 0, 0, 0};
                    sws_scale(d.sws, d.frame->data, d.frame->linesize, 0,
                              d.frame->height, dst, stride);
                    QMutexLocker lock(&m_queueLock);
                    m_queue.push_back({img, framePts});
                }
            }
        } else if (d.adec && d.packet->stream_index == d.aIndex && m_playing.load()) {
            if (avcodec_send_packet(d.adec, d.packet) >= 0) {
                while (avcodec_receive_frame(d.adec, d.frame) >= 0) {
                    const int maxOut = int(av_rescale_rnd(
                        swr_get_delay(d.swr, d.adec->sample_rate) + d.frame->nb_samples,
                        recording::kAudioSampleRate, d.adec->sample_rate, AV_ROUND_UP));
                    d.audioBuf.resize(size_t(maxOut) * recording::kAudioChannels);
                    uint8_t* out = reinterpret_cast<uint8_t*>(d.audioBuf.data());
                    const int got = swr_convert(d.swr, &out, maxOut,
                                                const_cast<const uint8_t**>(d.frame->data),
                                                d.frame->nb_samples);
                    // Short writes are normal when the sink is full; retry the rest
                    // rather than dropping it, or the audio would develop gaps.
                    int written = 0;
                    while (m_sink && got > written && !m_stop.load() && m_playing.load()) {
                        const int n = m_sink->write(
                            d.audioBuf.data() + size_t(written) * recording::kAudioChannels,
                            got - written);
                        if (n <= 0)
                            std::this_thread::sleep_for(std::chrono::milliseconds(3));
                        written += n;
                    }
                }
            }
        }
        av_packet_unref(d.packet);
    }
}

std::unique_ptr<IMediaPlayer> createMediaPlayer()
{
    return std::make_unique<FfmpegPlayer>();
}

} // namespace platform
