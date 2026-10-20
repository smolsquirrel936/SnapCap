#include "platform/platform.h"

#include "core/i18n/I18n.h"

#include <QDebug>
#include <QDir>
#include <QFile>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <vector>

namespace platform {

// Copy a time range without re-encoding: packets are read from the source and
// written straight out with rebased timestamps. Instant even on a long file, and it
// cannot degrade the picture — the trade is that the cut can only land on a
// keyframe, so the real start may sit up to one GOP earlier than requested.
bool trimRecording(const QString& src, const QString& dst,
                   qint64 startMs, qint64 endMs, QString* error)
{
    auto fail = [&](const char* key) {
        if (error)
            *error = I18n::instance().t(QString::fromLatin1(key));
        return false;
    };
    if (endMs <= startMs)
        return fail("record.trim.error.range");

    AVFormatContext* in = nullptr;
    AVFormatContext* out = nullptr;
    std::vector<int> streamMap;
    bool ok = false;

    const QByteArray inPath = QDir::toNativeSeparators(src).toUtf8();
    const QByteArray outPath = QDir::toNativeSeparators(dst).toUtf8();

    auto cleanup = [&] {
        if (out) {
            if (out->pb) avio_closep(&out->pb);
            avformat_free_context(out);
        }
        if (in) avformat_close_input(&in);
    };

    if (avformat_open_input(&in, inPath.constData(), nullptr, nullptr) < 0
        || avformat_find_stream_info(in, nullptr) < 0) {
        cleanup();
        return fail("record.trim.error.open");
    }
    if (avformat_alloc_output_context2(&out, nullptr, nullptr, outPath.constData()) < 0 || !out) {
        cleanup();
        return fail("record.trim.error.output");
    }

    // Mirror every stream we can copy verbatim.
    streamMap.assign(in->nb_streams, -1);
    for (unsigned i = 0; i < in->nb_streams; ++i) {
        AVStream* s = in->streams[i];
        const AVMediaType t = s->codecpar->codec_type;
        if (t != AVMEDIA_TYPE_VIDEO && t != AVMEDIA_TYPE_AUDIO)
            continue;
        AVStream* o = avformat_new_stream(out, nullptr);
        if (!o) { cleanup(); return fail("record.trim.error.output"); }
        avcodec_parameters_copy(o->codecpar, s->codecpar);
        o->codecpar->codec_tag = 0;        // let the muxer pick a tag it accepts
        streamMap[i] = o->index;
    }

    if (!(out->oformat->flags & AVFMT_NOFILE)
        && avio_open(&out->pb, outPath.constData(), AVIO_FLAG_WRITE) < 0) {
        cleanup();
        return fail("record.trim.error.output");
    }
    if (avformat_write_header(out, nullptr) < 0) {
        cleanup();
        return fail("record.trim.error.output");
    }

    // Seek to the keyframe at or before the requested start.
    const int64_t seekTs = av_rescale_q(startMs, AVRational{1, 1000}, AV_TIME_BASE_Q);
    av_seek_frame(in, -1, seekTs, AVSEEK_FLAG_BACKWARD);

    AVPacket* pkt = av_packet_alloc();
    // Timestamps are rebased per stream off the FIRST packet actually kept, so the
    // output starts at zero however far back the keyframe sat.
    std::vector<int64_t> firstDts(in->nb_streams, AV_NOPTS_VALUE);

    while (av_read_frame(in, pkt) >= 0) {
        const int si = pkt->stream_index;
        if (si < 0 || si >= int(streamMap.size()) || streamMap[si] < 0) {
            av_packet_unref(pkt);
            continue;
        }
        const AVRational tb = in->streams[si]->time_base;
        const qint64 tsMs = pkt->pts != AV_NOPTS_VALUE
            ? qint64(av_rescale_q(pkt->pts, tb, AVRational{1, 1000}))
            : qint64(av_rescale_q(pkt->dts, tb, AVRational{1, 1000}));
        if (tsMs > endMs) {
            av_packet_unref(pkt);
            break;
        }
        if (firstDts[si] == AV_NOPTS_VALUE)
            firstDts[si] = pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts;

        AVStream* os = out->streams[streamMap[si]];
        if (pkt->pts != AV_NOPTS_VALUE) pkt->pts = av_rescale_q(pkt->pts - firstDts[si], tb, os->time_base);
        if (pkt->dts != AV_NOPTS_VALUE) pkt->dts = av_rescale_q(pkt->dts - firstDts[si], tb, os->time_base);
        pkt->duration = av_rescale_q(pkt->duration, tb, os->time_base);
        pkt->pos = -1;
        pkt->stream_index = os->index;
        av_interleaved_write_frame(out, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    ok = av_write_trailer(out) >= 0;
    cleanup();
    if (!ok) {
        QFile::remove(dst);                // never leave a half-written clip behind
        return fail("record.trim.error.output");
    }
    qInfo() << "[preview] trimmed" << startMs << "-" << endMs << "ms ->" << dst;
    return true;
}

} // namespace platform
