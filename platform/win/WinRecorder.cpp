#include "platform/win/WinRecorder.h"

#include "core/i18n/I18n.h"
#include "platform/win/WinAudioCapture.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPainter>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <vector>

namespace platform {
namespace {

QString avErr(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof buf);
    return QString::fromLocal8Bit(buf);
}

// The primary monitor's capture rect. On Windows the primary is by definition at
// (0,0) in physical virtual-desktop coordinates, which is the space our regions
// live in (capture px).
QRect primaryCaptureRect()
{
    for (const MonitorInfo& m : monitorLayout())
        if (m.captureGeometry.topLeft() == QPoint(0, 0))
            return m.captureGeometry;
    return {};
}

} // namespace

// libav* state for one recording. Everything here is touched only by the worker
// thread, which is why none of it appears in the header.
struct WinRecorder::Pipeline {
    // Source A: ddagrab filter graph (no demuxer involved).
    AVFilterGraph*   graph = nullptr;
    AVFilterContext* sink = nullptr;

    // Source B: gdigrab demuxer + decoder + colour conversion.
    AVFormatContext* inFmt = nullptr;
    AVCodecContext*  dec = nullptr;
    SwsContext*      sws = nullptr;
    int              inStream = -1;

    // Encode + mux.
    AVFormatContext* outFmt = nullptr;
    AVCodecContext*  enc = nullptr;
    AVStream*        stream = nullptr;
    QString          encoderName;

    // Audio: up to two capture sources summed into one encoded track.
    std::unique_ptr<WinAudioCapture> sysAudio;
    std::unique_ptr<WinAudioCapture> micAudio;
    AVCodecContext* aEnc = nullptr;
    AVStream*       aStream = nullptr;
    AVFrame*        aFrame = nullptr;       // planar float, one encoder frame
    SwrContext*     aSwr = nullptr;         // interleaved mix -> the encoder's layout
    std::vector<float> mixBuf;              // interleaved stereo scratch, one video frame's worth
    qint64          aSamples = 0;           // samples fed to the encoder (its pts clock)
    int             aFrameCapacity = 0;     // aFrame's ALLOCATED sample count (see pumpAudio)

    AVFrame* srcFrame = nullptr;    // frame as the source delivers it
    AVFrame* encFrame = nullptr;    // yuv420p frame handed to the encoder
    AVPacket* packet = nullptr;

    // GIF path: BGRA frames go through fps → scale → palettegen/paletteuse instead of
    // the yuv420p conversion, and come back out as PAL8 ready for the gif encoder.
    AVFilterGraph*   gifGraph = nullptr;
    AVFilterContext* gifSrc = nullptr;
    AVFilterContext* gifSink = nullptr;
    AVFrame*         gifFrame = nullptr;
    QSize            gifSize;

    bool usingDda = false;
    bool headerWritten = false;

    ~Pipeline()
    {
        if (sysAudio) sysAudio->stop();
        if (micAudio) micAudio->stop();
        if (outFmt && headerWritten)
            av_write_trailer(outFmt);
        if (aSwr) swr_free(&aSwr);
        if (aFrame) av_frame_free(&aFrame);
        if (aEnc) avcodec_free_context(&aEnc);
        if (enc) avcodec_free_context(&enc);
        if (outFmt) {
            if (outFmt->pb) avio_closep(&outFmt->pb);
            avformat_free_context(outFmt);
        }
        if (gifFrame) av_frame_free(&gifFrame);
        if (gifGraph) avfilter_graph_free(&gifGraph);
        if (graph) avfilter_graph_free(&graph);
        if (dec) avcodec_free_context(&dec);
        if (inFmt) avformat_close_input(&inFmt);
        if (sws) sws_freeContext(sws);
        if (srcFrame) av_frame_free(&srcFrame);
        if (encFrame) av_frame_free(&encFrame);
        if (packet) av_packet_free(&packet);
    }
};

WinRecorder::WinRecorder(QObject* parent) : QObject(parent)
{
    static bool once = false;
    if (!once) {
        avdevice_register_all();     // registers gdigrab (the fallback grabber)
        once = true;
    }
}

WinRecorder::~WinRecorder()
{
    stop();
    if (m_thread && m_thread->joinable())
        m_thread->join();
}

bool WinRecorder::isRecording() const { return m_running.load(); }

bool WinRecorder::start(const Request& req, Callbacks cb)
{
    if (m_running.load()) {
        m_error = I18n::instance().t(QStringLiteral("record.error.busy"));
        return false;
    }
    if (req.regionCapturePx.isEmpty()) {
        m_error = I18n::instance().t(QStringLiteral("record.error.region"));
        return false;
    }
    // Join a previous worker before reusing the handle (it has already finished —
    // this only reaps it).
    if (m_thread && m_thread->joinable())
        m_thread->join();

    if (!QDir().mkpath(QFileInfo(req.outputPath).path())) {
        m_error = I18n::instance().t(QStringLiteral("record.error.output"));
        return false;
    }

    m_cb = std::move(cb);
    m_error.clear();
    m_stop.store(false);
    m_running.store(true);
    m_thread = std::make_unique<std::thread>([this, req] { run(req); });
    return true;
}

void WinRecorder::stop()
{
    m_stop.store(true);
    m_paused.store(false);       // a paused recording must still be able to finalize
}

void WinRecorder::pause()  { m_paused.store(true); }
void WinRecorder::resume() { m_paused.store(false); }
bool WinRecorder::isPaused() const { return m_paused.load(); }

void WinRecorder::setOverlay(const QImage& overlay)
{
    QMutexLocker lock(&m_overlayLock);
    // Normalise once here rather than per frame: the encode loop blends it onto every
    // captured frame, so the format conversion must not sit in that path.
    m_overlay = overlay.isNull()
        ? QImage()
        : overlay.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void WinRecorder::finish(bool ok, const QString& path, const QString& message)
{
    m_running.store(false);
    if (!ok)
        m_error = message;
    // Back to the GUI thread: the caller updates widgets and may delete things.
    auto cb = m_cb.onFinished;
    if (!cb)
        return;
    QMetaObject::invokeMethod(this, [cb, ok, path, message] { cb(ok, path, message); },
                              Qt::QueuedConnection);
}

bool WinRecorder::openSource(Pipeline& p, const Request& req)
{
    const QRect r = req.regionCapturePx;
    const int fps = recording::clampFps(req.config.fps);
    const int mouse = req.config.captureCursor ? 1 : 0;

    // DXGI Desktop Duplication is per-output and we only trust output 0 == the
    // primary monitor, so a region that is not wholly on the primary takes the
    // gdigrab path instead (which addresses the whole virtual desktop and is
    // therefore unambiguous). Mapping the other outputs needs a real DXGI
    // enumeration.
    const QRect primary = primaryCaptureRect();
    if (!primary.isEmpty() && primary.contains(r)) {
        // One graph, no buffersrc: ddagrab IS the source, and the chain ends in
        // system-memory yuv420p so the encoder can take frames directly.
        // Ends at BGRA, not yuv420p: the overlay is composited in RGB, so BOTH source
        // paths hand over BGRA and share one conversion step below.
        const QString desc =
            QStringLiteral("ddagrab=output_idx=0:framerate=%1:video_size=%2x%3"
                           ":offset_x=%4:offset_y=%5:draw_mouse=%6:dup_frames=1"
                           ",hwdownload,format=bgra")
                .arg(fps).arg(r.width()).arg(r.height())
                .arg(r.x() - primary.x()).arg(r.y() - primary.y()).arg(mouse);

        p.graph = avfilter_graph_alloc();
        AVFilterInOut* inputs = nullptr;
        AVFilterInOut* outputs = nullptr;
        int rc = avfilter_graph_parse2(p.graph, desc.toUtf8().constData(), &inputs, &outputs);
        if (rc >= 0 && outputs && !inputs) {
            rc = avfilter_graph_create_filter(&p.sink, avfilter_get_by_name("buffersink"),
                                              "out", nullptr, nullptr, p.graph);
            if (rc >= 0)
                rc = avfilter_link(outputs->filter_ctx, outputs->pad_idx, p.sink, 0);
            if (rc >= 0)
                rc = avfilter_graph_config(p.graph, nullptr);
        } else if (rc >= 0) {
            rc = AVERROR(EINVAL);        // a source-only chain must leave no inputs
        }
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        if (rc >= 0) {
            p.usingDda = true;
            qInfo() << "[record] source: ddagrab (DXGI) region" << r;
            return true;
        }
        qWarning() << "[record] ddagrab unavailable (" << avErr(rc) << ") — falling back to gdigrab";
        avfilter_graph_free(&p.graph);
        p.sink = nullptr;
    } else {
        qInfo() << "[record] region is not wholly on the primary monitor — using gdigrab";
    }

    // Fallback: GDI desktop grabber. Same offsets, virtual-desktop origin.
    const AVInputFormat* fmt = av_find_input_format("gdigrab");
    if (!fmt) {
        m_error = I18n::instance().t(QStringLiteral("record.error.source"));
        return false;
    }
    AVDictionary* opts = nullptr;
    av_dict_set_int(&opts, "framerate", fps, 0);
    av_dict_set_int(&opts, "offset_x", r.x(), 0);
    av_dict_set_int(&opts, "offset_y", r.y(), 0);
    av_dict_set(&opts, "video_size",
                QStringLiteral("%1x%2").arg(r.width()).arg(r.height()).toUtf8().constData(), 0);
    av_dict_set_int(&opts, "draw_mouse", mouse, 0);

    int rc = avformat_open_input(&p.inFmt, "desktop", fmt, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        qWarning() << "[record] gdigrab open failed:" << avErr(rc);
        m_error = I18n::instance().t(QStringLiteral("record.error.source"));
        return false;
    }
    if (avformat_find_stream_info(p.inFmt, nullptr) < 0 || p.inFmt->nb_streams == 0) {
        m_error = I18n::instance().t(QStringLiteral("record.error.source"));
        return false;
    }
    p.inStream = 0;
    const AVCodec* dec = avcodec_find_decoder(p.inFmt->streams[0]->codecpar->codec_id);
    if (!dec) {
        m_error = I18n::instance().t(QStringLiteral("record.error.source"));
        return false;
    }
    p.dec = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(p.dec, p.inFmt->streams[0]->codecpar);
    if (avcodec_open2(p.dec, dec, nullptr) < 0) {
        m_error = I18n::instance().t(QStringLiteral("record.error.source"));
        return false;
    }
    qInfo() << "[record] source: gdigrab (GDI) region" << r;
    return true;
}

bool WinRecorder::openEncoder(Pipeline& p, const Request& req)
{
    const QRect r = req.regionCapturePx;
    const int fps = recording::clampFps(req.config.fps);

    // Presence in the build does not mean it opens here (hevc_mf is the standing
    // example), so every candidate gets a real avcodec_open2 and the first one
    // that survives wins. The list always ends in a software encoder.
    // GIF is its own encoder with its own pixel format, and carries no codec choice.
    if (req.config.container == recording::Container::Gif) {
        const AVCodec* codec = avcodec_find_encoder_by_name("gif");
        if (!codec) {
            m_error = I18n::instance().t(QStringLiteral("record.error.encoder"));
            return false;
        }
        AVCodecContext* enc = avcodec_alloc_context3(codec);
        enc->width = p.gifSize.width();
        enc->height = p.gifSize.height();
        enc->time_base = AVRational{1, recording::clampFps(req.config.gifFps)};
        enc->framerate = AVRational{recording::clampFps(req.config.gifFps), 1};
        enc->pix_fmt = AV_PIX_FMT_PAL8;          // what paletteuse hands us
        if (p.outFmt->oformat->flags & AVFMT_GLOBALHEADER)
            enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        const int rc = avcodec_open2(enc, codec, nullptr);
        if (rc < 0) {
            qInfo() << "[record] gif encoder did not open:" << avErr(rc);
            avcodec_free_context(&enc);
            m_error = I18n::instance().t(QStringLiteral("record.error.encoder"));
            return false;
        }
        p.enc = enc;
        p.encoderName = QStringLiteral("gif");
        qInfo() << "[record] encoder: gif" << p.gifSize;
        return true;
    }

    const QStringList candidates = recording::encoderCandidates(req.config.codec, req.config.hw);
    for (const QString& name : candidates) {
        const AVCodec* codec = avcodec_find_encoder_by_name(name.toUtf8().constData());
        if (!codec)
            continue;

        AVCodecContext* enc = avcodec_alloc_context3(codec);
        enc->width = r.width();
        enc->height = r.height();
        enc->time_base = AVRational{1, fps};
        enc->framerate = AVRational{fps, 1};
        enc->pix_fmt = AV_PIX_FMT_YUV420P;
        enc->bit_rate = qint64(req.config.bitrateKbps) * 1000;
        enc->gop_size = fps * 2;          // a keyframe every 2s — also the trim granularity
        if (p.outFmt->oformat->flags & AVFMT_GLOBALHEADER)
            enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        // Constant-quality mode where the encoder has one. `qualityFor` knows both the
        // option name and the direction of each encoder's scale; an empty name means
        // this encoder is bitrate-only (libopenh264, the MF wrappers), so the bit_rate
        // set above stands.
        if (req.config.rateMode == recording::RateMode::Quality) {
            const recording::RateControl rcq =
                recording::qualityFor(name, req.config.quality);
            if (!rcq.optionName.isEmpty()) {
                av_opt_set_int(enc->priv_data, rcq.optionName.toUtf8().constData(),
                               rcq.value, 0);
                enc->bit_rate = 0;        // let the quantizer decide the size
            }
        }

        const int rc = avcodec_open2(enc, codec, nullptr);
        if (rc >= 0) {
            p.enc = enc;
            p.encoderName = name;
            qInfo() << "[record] encoder:" << name
                    << (recording::isSoftwareEncoder(name) ? "(software)" : "(hardware)");
            return true;
        }
        qInfo() << "[record] encoder" << name << "did not open:" << avErr(rc);
        avcodec_free_context(&enc);
    }
    m_error = I18n::instance().t(QStringLiteral("record.error.encoder"));
    return false;
}

// Trial-opens each candidate in turn and reports the first that survives — the same
// walk openEncoder() does, so what Preferences shows is what a recording will use.
QString activeVideoEncoder(recording::Codec codec, recording::HwPreference hw)
{
    for (const QString& name : recording::encoderCandidates(codec, hw)) {
        const AVCodec* c = avcodec_find_encoder_by_name(name.toUtf8().constData());
        if (!c)
            continue;
        AVCodecContext* ctx = avcodec_alloc_context3(c);
        if (!ctx)
            continue;
        // A representative frame size: some hardware encoders reject tiny ones, so
        // probing at 16x16 would report a false negative.
        ctx->width = 1280;
        ctx->height = 720;
        ctx->time_base = AVRational{1, 30};
        ctx->framerate = AVRational{30, 1};
        ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        ctx->bit_rate = 4'000'000;
        const bool ok = avcodec_open2(ctx, c, nullptr) >= 0;
        avcodec_free_context(&ctx);
        if (ok)
            return name;
    }
    return {};
}

bool WinRecorder::openAudio(Pipeline& p, const Request& req)
{
    if (!recording::wantsAudio(req.config))
        return true;                       // video-only: no stream, not a silent one

    // Start the sources first. A source that fails to open is dropped with a log line
    // rather than failing the recording — losing the mic should not cost you the video.
    if (req.config.systemAudio) {
        auto cap = std::make_unique<WinAudioCapture>();
        if (cap->start(WinAudioCapture::Source::Loopback))
            p.sysAudio = std::move(cap);
        else
            qWarning() << "[record] system audio unavailable:" << cap->error();
    }
    if (req.config.microphone) {
        auto cap = std::make_unique<WinAudioCapture>();
        if (cap->start(WinAudioCapture::Source::Microphone))
            p.micAudio = std::move(cap);
        else
            qWarning() << "[record] microphone unavailable:" << cap->error();
    }
    if (!p.sysAudio && !p.micAudio) {
        qWarning() << "[record] no audio source opened — recording video only";
        return true;
    }

    const QString name = recording::audioEncoderFor(req.config.container);
    const AVCodec* codec = avcodec_find_encoder_by_name(name.toUtf8().constData());
    if (!codec) {
        qWarning() << "[record] audio encoder" << name << "not found — recording video only";
        p.sysAudio.reset();
        p.micAudio.reset();
        return true;
    }

    p.aEnc = avcodec_alloc_context3(codec);
    p.aEnc->sample_rate = recording::kAudioSampleRate;
    p.aEnc->bit_rate = qint64(req.config.audioBitrateKbps) * 1000;
    av_channel_layout_default(&p.aEnc->ch_layout, recording::kAudioChannels);
    // Take the encoder's own preferred sample format rather than assuming planar
    // float: AAC wants FLTP, but libopus takes S16 or FLT depending on the build.
    const enum AVSampleFormat* fmts = nullptr;
    if (avcodec_get_supported_config(p.aEnc, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                     reinterpret_cast<const void**>(&fmts), nullptr) < 0 || !fmts)
        p.aEnc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    else
        p.aEnc->sample_fmt = fmts[0];
    if (p.outFmt->oformat->flags & AVFMT_GLOBALHEADER)
        p.aEnc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    int rc = avcodec_open2(p.aEnc, codec, nullptr);
    if (rc < 0) {
        qWarning() << "[record] audio encoder" << name << "did not open:" << avErr(rc)
                   << "— recording video only";
        avcodec_free_context(&p.aEnc);
        p.sysAudio.reset();
        p.micAudio.reset();
        return true;
    }

    p.aStream = avformat_new_stream(p.outFmt, nullptr);
    avcodec_parameters_from_context(p.aStream->codecpar, p.aEnc);
    p.aStream->time_base = AVRational{1, recording::kAudioSampleRate};

    // The mixer works in interleaved stereo float; swresample lays it out however the
    // encoder wants.
    AVChannelLayout inLayout;
    av_channel_layout_default(&inLayout, recording::kAudioChannels);
    if (swr_alloc_set_opts2(&p.aSwr, &p.aEnc->ch_layout, p.aEnc->sample_fmt,
                            p.aEnc->sample_rate, &inLayout, AV_SAMPLE_FMT_FLT,
                            recording::kAudioSampleRate, 0, nullptr) < 0
        || swr_init(p.aSwr) < 0) {
        qWarning() << "[record] could not build the audio mixer — recording video only";
        av_channel_layout_uninit(&inLayout);
        return true;
    }
    av_channel_layout_uninit(&inLayout);

    // frame_size is 0 for codecs that take any size (then pick one video frame's worth).
    const int frameSize = p.aEnc->frame_size > 0
        ? p.aEnc->frame_size
        : recording::audioSamplesPerFrame(req.config.fps);
    p.aFrame = av_frame_alloc();
    p.aFrame->format = p.aEnc->sample_fmt;
    p.aFrame->sample_rate = p.aEnc->sample_rate;
    p.aFrame->nb_samples = frameSize;
    p.aFrameCapacity = frameSize;
    av_channel_layout_copy(&p.aFrame->ch_layout, &p.aEnc->ch_layout);
    av_frame_get_buffer(p.aFrame, 0);

    qInfo() << "[record] audio encoder:" << name << p.aEnc->bit_rate / 1000 << "kbps"
            << (p.sysAudio ? "system" : "") << (p.micAudio ? "mic" : "");
    return true;
}

// GIF needs a palette, and a good one: `palettegen` in single mode computes a fresh
// 256-colour palette per frame and `paletteuse=new=1` applies it, which is why
// recordings use FFmpeg's chain rather than our own GifEncoder. Both source paths feed this through a
// buffersrc, so it works with ddagrab and gdigrab alike.
bool WinRecorder::openGifChain(Pipeline& p, const Request& req, int srcW, int srcH)
{
    const int gifFps = recording::clampFps(req.config.gifFps);
    // Downscale only — never upscale a small region to the cap — and keep the width
    // even so the scaler cannot land on a half pixel.
    int outW = srcW, outH = srcH;
    if (req.config.gifMaxWidth > 0 && srcW > req.config.gifMaxWidth) {
        outW = req.config.gifMaxWidth - (req.config.gifMaxWidth % 2);
        outH = int(qRound(double(srcH) * outW / double(srcW)));
        outH -= outH % 2;
    }
    p.gifSize = QSize(qMax(2, outW), qMax(2, outH));

    const QString desc =
        QStringLiteral("fps=%1,scale=%2:%3:flags=lanczos,"
                       "split[a][b];[a]palettegen=stats_mode=single[p];[b][p]paletteuse=new=1")
            .arg(gifFps).arg(p.gifSize.width()).arg(p.gifSize.height());

    p.gifGraph = avfilter_graph_alloc();
    // time_base is the CAPTURE rate, not gifFps: the frames fed to this source carry
    // pts = the capture frame counter, which advances at req.config.fps. Declaring
    // 1/gifFps told the graph the input was already at the GIF rate, so `fps` had
    // nothing to decimate and passed every captured frame through -- doubling both the
    // palettegen/lanczos cost per second AND the output duration, since the gif encoder
    // then laid 30 fps worth of frames onto a 15 fps timeline (half-speed playback).
    //
    // pix_fmt is BGR0, not BGRA, although the bytes ARE the BGRA frames: it declares the
    // 4th byte padding, so the scale step converts to the RGB32 the palette filters take
    // with alpha forced to 255. The captured alpha is not trustworthy -- ddagrab draws
    // the cursor with SrcBlendAlpha=ONE/DestBlendAlpha=ZERO, writing the sprite's own
    // alpha (0 outside the arrow) over its whole bounding box -- and paletteuse maps
    // every pixel under alpha 128 to the transparent index, which showed as a black
    // square riding along with the cursor. The video path never noticed: YUV has no alpha.
    const int srcFps = recording::clampFps(req.config.fps);
    const QString args = QStringLiteral("video_size=%1x%2:pix_fmt=%3:time_base=1/%4:pixel_aspect=1/1")
                             .arg(srcW).arg(srcH).arg(int(AV_PIX_FMT_BGR0)).arg(srcFps);
    int rc = avfilter_graph_create_filter(&p.gifSrc, avfilter_get_by_name("buffer"), "in",
                                          args.toUtf8().constData(), nullptr, p.gifGraph);
    if (rc >= 0)
        rc = avfilter_graph_create_filter(&p.gifSink, avfilter_get_by_name("buffersink"), "out",
                                          nullptr, nullptr, p.gifGraph);

    AVFilterInOut* inputs = nullptr;
    AVFilterInOut* outputs = nullptr;
    if (rc >= 0)
        rc = avfilter_graph_parse2(p.gifGraph, desc.toUtf8().constData(), &inputs, &outputs);
    if (rc >= 0 && inputs && outputs) {
        rc = avfilter_link(p.gifSrc, 0, inputs->filter_ctx, inputs->pad_idx);
        if (rc >= 0)
            rc = avfilter_link(outputs->filter_ctx, outputs->pad_idx, p.gifSink, 0);
    } else if (rc >= 0) {
        rc = AVERROR(EINVAL);
    }
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (rc >= 0)
        rc = avfilter_graph_config(p.gifGraph, nullptr);
    if (rc < 0) {
        qWarning() << "[record] GIF filter chain failed:" << avErr(rc);
        m_error = I18n::instance().t(QStringLiteral("record.error.encoder"));
        return false;
    }
    p.gifFrame = av_frame_alloc();
    qInfo() << "[record] GIF chain:" << srcW << "x" << srcH << "->" << p.gifSize
            << "@" << gifFps << "fps";
    return true;
}

bool WinRecorder::openOutput(Pipeline& p, const Request& req)
{
    const QByteArray path = QDir::toNativeSeparators(req.outputPath).toUtf8();
    const QByteArray muxer = recording::muxerFor(req.config.container).toUtf8();

    int rc = avformat_alloc_output_context2(&p.outFmt, nullptr, muxer.constData(), path.constData());
    if (rc < 0 || !p.outFmt) {
        m_error = I18n::instance().t(QStringLiteral("record.error.output"));
        return false;
    }
    // The GIF muxer spells "loop forever" as 0 and "play once" as -1.
    if (req.config.container == recording::Container::Gif && p.outFmt->priv_data)
        av_opt_set_int(p.outFmt->priv_data, "loop", req.config.gifLoop ? 0 : -1, 0);
    return true;
}

void WinRecorder::run(Request req)
{
    const QString path = req.outputPath;
    bool ok = false;
    QString message;

    // The pipeline lives in its own scope: its destructor writes the trailer, and
    // finish() must not fire until the file on disk is complete — the caller opens
    // it the moment it hears "finished".
    {
    Pipeline p;

    const bool gif = (req.config.container == recording::Container::Gif);
    if (!openOutput(p, req) || !openSource(p, req)) {
        finish(false, path, m_error);
        return;
    }
    // The GIF chain decides the output size (it may downscale), and the gif encoder is
    // opened at that size — so the chain has to be built first.
    if (gif && !openGifChain(p, req, req.regionCapturePx.width(), req.regionCapturePx.height())) {
        finish(false, path, m_error);
        return;
    }
    if (!openEncoder(p, req)) {
        finish(false, path, m_error);
        return;
    }

    p.stream = avformat_new_stream(p.outFmt, nullptr);
    avcodec_parameters_from_context(p.stream->codecpar, p.enc);
    p.stream->time_base = p.enc->time_base;

    // After the video stream, so video is stream 0 — players and editors default to
    // the first video track, and some tools assume index 0. GIF carries no audio at
    // all, so the sources are not even opened for it.
    if (!gif)
        openAudio(p, req);

    int rc = avio_open(&p.outFmt->pb, QDir::toNativeSeparators(path).toUtf8().constData(),
                       AVIO_FLAG_WRITE);
    if (rc < 0) {
        finish(false, path, I18n::instance().t(QStringLiteral("record.error.output")));
        return;
    }
    rc = avformat_write_header(p.outFmt, nullptr);
    if (rc < 0) {
        finish(false, path, I18n::instance().t(QStringLiteral("record.error.output")));
        return;
    }
    p.headerWritten = true;

    p.srcFrame = av_frame_alloc();
    p.packet = av_packet_alloc();
    p.encFrame = av_frame_alloc();
    p.encFrame->format = AV_PIX_FMT_YUV420P;
    p.encFrame->width = p.enc->width;
    p.encFrame->height = p.enc->height;
    av_frame_get_buffer(p.encFrame, 0);

    QElapsedTimer clock;
    clock.start();
    qint64 lastTick = 0;
    qint64 frames = 0;
    qint64 gifFrames = 0;      // the GIF chain emits at its own rate, so it counts separately
    std::vector<float> mixScratch, srcScratch;   // reused per frame; never reallocated in steady state

    auto encodeFrame = [&](AVFrame* f) {
        if (avcodec_send_frame(p.enc, f) < 0)
            return;
        while (avcodec_receive_packet(p.enc, p.packet) >= 0) {
            av_packet_rescale_ts(p.packet, p.enc->time_base, p.stream->time_base);
            p.packet->stream_index = p.stream->index;
            av_interleaved_write_frame(p.outFmt, p.packet);
            av_packet_unref(p.packet);
        }
    };

    const int fps = recording::clampFps(req.config.fps);
    const int audioPerFrame = recording::audioSamplesPerFrame(fps);

    // Pull one video frame's worth of audio, sum the sources, and hand it to the
    // encoder. Slaving the audio clock to the VIDEO FRAME COUNTER is what makes drift
    // between the two streams impossible: they advance together or not at all, so a
    // pause (which stops the counter) cuts both cleanly.
    auto pumpAudio = [&] {
        if (!p.aEnc || !p.aFrame)
            return;
        mixScratch.assign(size_t(audioPerFrame) * recording::kAudioChannels, 0.0f);
        if (p.sysAudio) {
            srcScratch.resize(mixScratch.size());
            p.sysAudio->read(srcScratch.data(), audioPerFrame);
            for (size_t i = 0; i < mixScratch.size(); ++i)
                mixScratch[i] += srcScratch[i];
        }
        if (p.micAudio) {
            srcScratch.resize(mixScratch.size());
            p.micAudio->read(srcScratch.data(), audioPerFrame);
            for (size_t i = 0; i < mixScratch.size(); ++i)
                mixScratch[i] += srcScratch[i];
        }
        // Summing two sources can exceed full scale; clamp rather than let it wrap
        // into a loud crackle.
        if (p.sysAudio && p.micAudio)
            for (float& s : mixScratch)
                s = qBound(-1.0f, s, 1.0f);

        const uint8_t* in = reinterpret_cast<const uint8_t*>(mixScratch.data());
        if (swr_convert(p.aSwr, nullptr, 0, &in, audioPerFrame) < 0)
            return;
        // Drain whole encoder frames; swresample holds the remainder for next time.
        // `capacity` is fixed at the ALLOCATED size — reading the loop bound from
        // aFrame->nb_samples would shrink it permanently the first time a convert came
        // up short, and every later frame with it.
        const int capacity = p.aFrameCapacity;
        while (swr_get_out_samples(p.aSwr, 0) >= capacity) {
            if (av_frame_make_writable(p.aFrame) < 0)
                return;
            p.aFrame->nb_samples = capacity;
            const int got = swr_convert(p.aSwr, p.aFrame->data, capacity, nullptr, 0);
            if (got <= 0)
                return;
            p.aFrame->nb_samples = got;
            p.aFrame->pts = p.aSamples;
            p.aSamples += got;
            if (avcodec_send_frame(p.aEnc, p.aFrame) < 0)
                return;
            while (avcodec_receive_packet(p.aEnc, p.packet) >= 0) {
                av_packet_rescale_ts(p.packet, AVRational{1, recording::kAudioSampleRate},
                                     p.aStream->time_base);
                p.packet->stream_index = p.aStream->index;
                av_interleaved_write_frame(p.outFmt, p.packet);
                av_packet_unref(p.packet);
            }
        }
    };

    // Auto-stop measured in ENCODED frames, i.e. video time — so paused seconds do
    // not count towards the limit, which is what "record at most N minutes" means.
    const qint64 maxFrames = qint64(qMax(0, req.config.maxDurationMin)) * 60 * fps;

    while (!m_stop.load()) {
        if (maxFrames > 0 && (gif ? gifFrames : frames) >= maxFrames) {
            qInfo() << "[record] stopping: reached the" << req.config.maxDurationMin
                    << "minute limit";
            break;
        }
        // Pull a BGRA frame from whichever source is running. Both paths land here
        // with the same format so the overlay composite + conversion below is shared.
        if (p.usingDda) {
            rc = av_buffersink_get_frame(p.sink, p.srcFrame);
            if (rc == AVERROR(EAGAIN)) continue;
            if (rc < 0) break;                     // EOF or a real error: stop cleanly
        } else {
            rc = av_read_frame(p.inFmt, p.packet);
            if (rc < 0) break;
            if (p.packet->stream_index != p.inStream) { av_packet_unref(p.packet); continue; }
            rc = avcodec_send_packet(p.dec, p.packet);
            av_packet_unref(p.packet);
            if (rc < 0) continue;
            if (avcodec_receive_frame(p.dec, p.srcFrame) < 0) continue;
        }

        // Paused: keep draining the source (stopping it would tear down the grabber)
        // but encode nothing and do not advance the frame counter — which is exactly
        // what makes paused time absent from the file rather than frozen into it.
        if (m_paused.load()) {
            av_frame_unref(p.srcFrame);
            // Drop what the mics captured meanwhile, so resuming does not replay the
            // paused seconds — the audio equivalent of not advancing the frame counter.
            if (p.sysAudio) p.sysAudio->discard();
            if (p.micAudio) p.micAudio->discard();
            continue;
        }

        // Burn the annotation layer into the frame. Both images are labelled
        // ARGB32_Premultiplied so SourceOver blends RGB correctly whatever the
        // captured alpha happens to be (desktop frames are opaque, but ddagrab does
        // not promise a meaningful alpha channel).
        QImage overlay;
        {
            QMutexLocker lock(&m_overlayLock);
            overlay = m_overlay;
        }
        if (!overlay.isNull() && av_frame_make_writable(p.srcFrame) >= 0) {
            QImage frame(p.srcFrame->data[0], p.srcFrame->width, p.srcFrame->height,
                         p.srcFrame->linesize[0], QImage::Format_ARGB32_Premultiplied);
            QPainter painter(&frame);
            painter.drawImage(frame.rect(), overlay, overlay.rect());
        }

        if (gif) {
            // The chain does its own fps conversion, so feed every captured frame and
            // take whatever comes out — it emits at the GIF rate, not the capture rate.
            //
            // duration MUST be reset alongside pts. The frame arrives carrying the
            // grabber's own duration -- gdigrab counts microseconds -- while this
            // buffersrc declares time_base=1/srcFps, so a stale ~40000 us reads as 40000
            // THIRTIETHS of a second. At EOF the `fps` filter then duplicates the last
            // frame to cover that phantom ~22 minutes: one measured case drained 20459
            // frames instead of 189 and wrote a 586 MB GIF over six minutes, which looked
            // exactly like a hang on Stop. One tick = one frame at the capture rate.
            p.srcFrame->pts = frames;
            p.srcFrame->duration = 1;
            // Relabel to the BGR0 openGifChain() declares (same bytes, alpha ignored).
            // A frame still tagged BGRA is not converted by buffersrc -- it only logs
            // "Changing video frame properties" and passes the BGRA tag on -- so the
            // cursor box's alpha 0 would survive and the black square come back.
            p.srcFrame->format = AV_PIX_FMT_BGR0;
            if (av_buffersrc_add_frame_flags(p.gifSrc, p.srcFrame,
                                             AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                while (av_buffersink_get_frame(p.gifSink, p.gifFrame) >= 0) {
                    p.gifFrame->pts = gifFrames++;
                    encodeFrame(p.gifFrame);
                    av_frame_unref(p.gifFrame);
                }
            }
            ++frames;
            av_frame_unref(p.srcFrame);
        } else {
            if (!p.sws) {
                p.sws = sws_getContext(p.srcFrame->width, p.srcFrame->height,
                                       AVPixelFormat(p.srcFrame->format),
                                       p.enc->width, p.enc->height, AV_PIX_FMT_YUV420P,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!p.sws) { av_frame_unref(p.srcFrame); break; }
            }
            av_frame_make_writable(p.encFrame);
            sws_scale(p.sws, p.srcFrame->data, p.srcFrame->linesize, 0, p.srcFrame->height,
                      p.encFrame->data, p.encFrame->linesize);

            // Frame-counter timestamps. Both grabbers are real-time sources pinned to
            // `framerate` (ddagrab duplicates frames to hold it), so counting is
            // accurate here AND monotonic, which a raw source pts is not guaranteed to
            // be.
            p.encFrame->pts = frames++;
            encodeFrame(p.encFrame);
            pumpAudio();             // one frame of video ⇒ one frame's worth of audio
            av_frame_unref(p.srcFrame);
        }

        // Elapsed is derived from the frames actually encoded, not the wall clock, so
        // the readout matches the video's own duration and stalls while paused.
        const qint64 ms = frames * 1000 / fps;
        if (m_cb.onTick && ms - lastTick >= 250) {
            lastTick = ms;
            auto tick = m_cb.onTick;
            QMetaObject::invokeMethod(this, [tick, ms] { tick(ms); }, Qt::QueuedConnection);
        }
    }

    if (gif && p.gifSrc) {       // drain the palette chain before flushing the encoder
        av_buffersrc_add_frame_flags(p.gifSrc, nullptr, 0);
        while (av_buffersink_get_frame(p.gifSink, p.gifFrame) >= 0) {
            p.gifFrame->pts = gifFrames++;
            encodeFrame(p.gifFrame);
            av_frame_unref(p.gifFrame);
        }
    }
    encodeFrame(nullptr);        // flush the video encoder before the trailer
    if (p.aEnc) {                // and the audio encoder, or its tail is lost
        avcodec_send_frame(p.aEnc, nullptr);
        while (avcodec_receive_packet(p.aEnc, p.packet) >= 0) {
            av_packet_rescale_ts(p.packet, AVRational{1, recording::kAudioSampleRate},
                                 p.aStream->time_base);
            p.packet->stream_index = p.aStream->index;
            av_interleaved_write_frame(p.outFmt, p.packet);
            av_packet_unref(p.packet);
        }
    }
    const qint64 durationMs = frames * 1000 / qMax(1, recording::clampFps(req.config.fps));
    qInfo() << "[record] wrote" << frames << "frames (" << durationMs << "ms) to" << path;

    ok = (gif ? gifFrames : frames) > 0;
    if (!ok)
        message = I18n::instance().t(QStringLiteral("record.error.no_frames"));
    }   // ~Pipeline: flushes the muxer and closes the file

    finish(ok, path, message);
}

} // namespace platform
