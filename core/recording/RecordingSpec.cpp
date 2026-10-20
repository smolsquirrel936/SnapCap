#include "core/recording/RecordingSpec.h"

#include "core/FilenameTemplate.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace recording {
namespace {

// Software encoders shipped by our LGPL build (tools/build_ffmpeg.sh). Listed
// last in every candidate list so a recording always has a working fallback.
const QString kSwH264 = QStringLiteral("libopenh264");
const QString kSwH265 = QStringLiteral("libkvazaar");
const QString kSwAV1  = QStringLiteral("libsvtav1");
const QString kSwVP9  = QStringLiteral("libvpx-vp9");

// Hardware encoders, in the order worth trying: NVENC first (most common and the
// most complete), then Intel QSV, then AMD AMF, then Media Foundation as the
// vendor-neutral catch-all.
QStringList hardwareFor(Codec v)
{
    switch (v) {
        case Codec::H264: return {QStringLiteral("h264_nvenc"), QStringLiteral("h264_qsv"),
                                  QStringLiteral("h264_amf"),   QStringLiteral("h264_mf")};
        case Codec::H265: return {QStringLiteral("hevc_nvenc"), QStringLiteral("hevc_qsv"),
                                  QStringLiteral("hevc_amf"),   QStringLiteral("hevc_mf")};
        case Codec::AV1:  return {QStringLiteral("av1_nvenc"),  QStringLiteral("av1_qsv"),
                                  QStringLiteral("av1_amf"),    QStringLiteral("av1_mf")};
        // VP9 has no NVENC/AMF/MF encoder in this build — QSV is the only one.
        case Codec::VP9:  return {QStringLiteral("vp9_qsv")};
    }
    return {};
}

QString softwareFor(Codec v)
{
    switch (v) {
        case Codec::H264: return kSwH264;
        case Codec::H265: return kSwH265;
        case Codec::AV1:  return kSwAV1;
        case Codec::VP9:  return kSwVP9;
    }
    return {};
}

} // namespace

QString extensionFor(Container c)
{
    switch (c) {
        case Container::Mp4:  return QStringLiteral("mp4");
        case Container::Mkv:  return QStringLiteral("mkv");
        case Container::WebM: return QStringLiteral("webm");
        case Container::Gif:  return QStringLiteral("gif");
    }
    return QStringLiteral("mp4");
}

QString muxerFor(Container c)
{
    switch (c) {
        case Container::Mp4:  return QStringLiteral("mp4");
        case Container::Mkv:  return QStringLiteral("matroska");
        case Container::WebM: return QStringLiteral("webm");
        case Container::Gif:  return QStringLiteral("gif");
    }
    return QStringLiteral("mp4");
}

bool supports(Container c, Codec v)
{
    switch (c) {
        // MP4 carries all four: AV1 and VP9 in MP4 are standardised and every
        // current player handles them.
        case Container::Mp4:  return true;
        case Container::Mkv:  return true;
        // WebM is the restrictive one: VP9/AV1 only, never H.264/H.265.
        case Container::WebM: return v == Codec::VP9 || v == Codec::AV1;
        // GIF has no video codec — it IS the encoder.
        case Container::Gif:  return false;
    }
    return false;
}

QVector<Container> containersFor(Codec v)
{
    QVector<Container> out;
    for (Container c : {Container::Mp4, Container::Mkv, Container::WebM})
        if (supports(c, v))
            out.append(c);
    return out;
}

QStringList encoderCandidates(Codec v, HwPreference hw)
{
    const QString sw = softwareFor(v);
    if (hw == HwPreference::ForceSoftware)
        return {sw};

    QStringList out = hardwareFor(v);
    // Auto and PreferHardware try the same encoders in the same order — the
    // difference is only what the UI promises, and both must degrade to software
    // rather than fail. A separate ordering would be a distinction without a
    // behavioural difference.
    out.append(sw);
    return out;
}

bool isSoftwareEncoder(const QString& name)
{
    return name == kSwH264 || name == kSwH265 || name == kSwAV1 || name == kSwVP9;
}

QString timecode(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 total = ms / 1000;
    const qint64 h = total / 3600;
    const qint64 m = (total % 3600) / 60;
    const qint64 s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0'))
                                          .arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString timecodeForFilename(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 total = ms / 1000;
    return QStringLiteral("%1-%2-%3")
        .arg(total / 3600,        2, 10, QLatin1Char('0'))
        .arg((total % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60,          2, 10, QLatin1Char('0'));
}

QString resolveOutputPath(const QString& pathTemplate, Container c,
                          const QDateTime& when, const QMap<QString, QString>& specials)
{
    const QString templ = pathTemplate.isEmpty() ? defaultPathTemplate() : pathTemplate;
    const QString expanded = FilenameTemplate::expand(templ, when, specials);

    // Force the container's extension. completeBaseName() drops only the LAST
    // suffix, so "clip.2026.mp4" keeps its dots and becomes "clip.2026.webm".
    const QFileInfo fi(expanded);
    const QString base = fi.completeBaseName();
    if (base.isEmpty())            // template expanded to nothing usable
        return expanded;
    const QString dir = fi.path();
    const QString name = base + QLatin1Char('.') + extensionFor(c);
    return dir.isEmpty() || dir == QLatin1String(".") ? name : dir + QLatin1Char('/') + name;
}

QString defaultPathTemplate()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    return QDir(dir).filePath(QStringLiteral("SnapCap_$yyyy-MM-dd_HH-mm-ss$"));
}

QRect normalizeRegion(const QRect& captureRect, const QRect& bounds)
{
    QRect r = bounds.isEmpty() ? captureRect : captureRect.intersected(bounds);
    if (r.width() <= 0 || r.height() <= 0)
        return {};
    // Shrink (never grow) to even dimensions: growing could push the region back
    // outside `bounds`, which is the one thing the intersect above just guaranteed.
    r.setWidth(r.width() - (r.width() % 2));
    r.setHeight(r.height() - (r.height() % 2));
    if (r.width() <= 0 || r.height() <= 0)
        return {};
    return r;
}

int clampFps(int fps)
{
    return qBound(5, fps, 60);
}

QString keyFor(Codec c)
{
    switch (c) {
        case Codec::H264: return QStringLiteral("h264");
        case Codec::H265: return QStringLiteral("h265");
        case Codec::AV1:  return QStringLiteral("av1");
        case Codec::VP9:  return QStringLiteral("vp9");
    }
    return QStringLiteral("h264");
}

QString keyFor(Container c) { return extensionFor(c); }   // mp4/mkv/webm/gif read well in a file

QString keyFor(HwPreference h)
{
    switch (h) {
        case HwPreference::Auto:           return QStringLiteral("auto");
        case HwPreference::PreferHardware: return QStringLiteral("hardware");
        case HwPreference::ForceSoftware:  return QStringLiteral("software");
    }
    return QStringLiteral("auto");
}

QString keyFor(RateMode m)
{
    return m == RateMode::Bitrate ? QStringLiteral("bitrate") : QStringLiteral("quality");
}

Codec codecFromKey(const QString& key, Codec fallback)
{
    const QString k = key.trimmed().toLower();
    for (Codec c : {Codec::H264, Codec::H265, Codec::AV1, Codec::VP9})
        if (keyFor(c) == k)
            return c;
    // "hevc" is what FFmpeg and most users call it; accept it as an alias rather than
    // silently resetting someone's hand-edited config back to H.264.
    if (k == QLatin1String("hevc"))
        return Codec::H265;
    return fallback;
}

Container containerFromKey(const QString& key, Container fallback)
{
    const QString k = key.trimmed().toLower();
    for (Container c : {Container::Mp4, Container::Mkv, Container::WebM, Container::Gif})
        if (keyFor(c) == k)
            return c;
    if (k == QLatin1String("matroska"))
        return Container::Mkv;
    return fallback;
}

HwPreference hwFromKey(const QString& key, HwPreference fallback)
{
    const QString k = key.trimmed().toLower();
    for (HwPreference h : {HwPreference::Auto, HwPreference::PreferHardware,
                           HwPreference::ForceSoftware})
        if (keyFor(h) == k)
            return h;
    return fallback;
}

RateMode rateModeFromKey(const QString& key, RateMode fallback)
{
    const QString k = key.trimmed().toLower();
    if (k == QLatin1String("bitrate")) return RateMode::Bitrate;
    if (k == QLatin1String("quality")) return RateMode::Quality;
    return fallback;
}

RateControl qualityFor(const QString& encoder, int quality0to100)
{
    const int q = qBound(0, quality0to100, 100);
    // Map the slider onto [lo,hi] of the encoder's own scale. `lo` is what quality=100
    // means and `hi` what quality=0 means, so a scale that runs "lower number = better"
    // (every CRF/QP one) is expressed by lo < hi and needs no special-casing.
    auto scale = [q](const char* name, int lo, int hi) {
        return RateControl{QString::fromLatin1(name),
                           int(qRound(hi + (lo - hi) * (q / 100.0)))};
    };

    // Software encoders: real constant-quality modes.
    if (encoder == QLatin1String("libsvtav1"))  return scale("crf", 20, 55);
    if (encoder == QLatin1String("libaom-av1")) return scale("crf", 20, 55);
    if (encoder == QLatin1String("librav1e"))   return scale("qp",  60, 180);
    if (encoder == QLatin1String("libvpx-vp9")) return scale("crf", 20, 50);
    if (encoder == QLatin1String("libkvazaar")) return scale("qp",  18, 42);
    // NVENC/QSV/AMF constant-quality knobs; all spell it differently.
    if (encoder.endsWith(QLatin1String("_nvenc"))) return scale("cq", 18, 44);
    if (encoder.endsWith(QLatin1String("_qsv")))   return scale("global_quality", 18, 44);
    if (encoder.endsWith(QLatin1String("_amf")))   return scale("qp_p", 18, 44);

    // libopenh264 and the MediaFoundation wrappers are bitrate-only — the caller must
    // fall back rather than set an option they would reject.
    return RateControl{};
}

bool wantsAudio(const Config& c)
{
    return c.systemAudio || c.microphone;
}

QString audioEncoderFor(Container c)
{
    // WebM carries Opus (or Vorbis) only — AAC in WebM is not a thing. Everywhere
    // else AAC is the safe default: every player and editor reads it.
    return c == Container::WebM ? QStringLiteral("libopus") : QStringLiteral("aac");
}

int audioSamplesPerFrame(int fps)
{
    return kAudioSampleRate / clampFps(fps);
}

} // namespace recording
