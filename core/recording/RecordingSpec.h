#pragma once

#include <QDateTime>
#include <QMap>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

// The Settings-driven pieces of a screen recording: which encoder to try, what the
// file is called, and what the elapsed clock reads. Pure Qt-Core logic — no FFmpeg,
// no widgets, no Win32 — so it is headless unit-testable, and the recorder backend
// stays a thin translation of these answers into libav* calls.
//
// Codec availability is NOT decided here: every codec below has a software encoder
// in the shipped FFmpeg build (tools/build_ffmpeg.sh), and whether a
// *hardware* one can open is a runtime question only the backend can answer.
namespace recording {

enum class Codec { H264, H265, AV1, VP9 };
enum class Container { Mp4, Mkv, WebM, Gif };
enum class HwPreference { Auto, PreferHardware, ForceSoftware };
// How the encoder is told what quality to aim for.
enum class RateMode { Quality, Bitrate };

// What a recording is being asked to produce. Defaults are the shipping
// defaults; Preferences overrides them per key (recording/*).
struct Config {
    Codec codec = Codec::H264;
    Container container = Container::Mp4;
    int fps = 60;
    int bitrateKbps = 8000;
    int quality = 70;               // 0..100, used when rateMode == Quality
    bool captureCursor = true;
    HwPreference hw = HwPreference::Auto;
    RateMode rateMode = RateMode::Quality;

    // Audio sources, mixed into ONE track. Neither ⇒ a video-only file with no audio
    // stream at all (not a silent one).
    bool systemAudio = true;        // WASAPI loopback on the default playback device
    bool microphone = false;        // WASAPI capture on the default input device
    int audioBitrateKbps = 256;

    // GIF output (container == Gif). Deliberately its own frame rate and width cap,
    // since GIF is far heavier per frame than video — the width cap is now what keeps
    // a GIF down to a sane size, the frame rate having been raised to match `fps`.
    // 0 = no limit. Stops a forgotten recording before it fills the disk.
    int maxDurationMin = 0;

    int gifFps = 30;
    int gifMaxWidth = 800;
    bool gifLoop = true;
};

// Everything audio is resampled to this before mixing, so the two sources can be
// summed sample-for-sample whatever their endpoints natively run at.
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 2;

// True when the recording should carry an audio stream at all.
bool wantsAudio(const Config& c);

// Audio encoder for a container: AAC everywhere except WebM, which takes Opus only.
QString audioEncoderFor(Container c);

// Samples per video frame at `fps`. Audio is pulled in lockstep with the video frame
// counter, which is what makes drift between the two streams impossible by
// construction rather than by correction.
int audioSamplesPerFrame(int fps);

// File extension (no dot) and the libavformat muxer name for a container.
QString extensionFor(Container c);
QString muxerFor(Container c);

// Can this container carry this codec? GIF carries none of them — it is its own
// encoder — so supports(Gif, anything) is false and GIF output bypasses `codec`.
bool supports(Container c, Codec v);
QVector<Container> containersFor(Codec v);

// Encoder names to try, in order, for a codec under a hardware preference. The
// backend opens the first one that actually works: presence in the build does not
// mean it can open here (hevc_mf exists everywhere but needs a hardware MFT).
// Every list ends with a software encoder, so it can never come back empty-handed.
QStringList encoderCandidates(Codec v, HwPreference hw);

// True when `name` is one of the software encoders — the backend logs which kind it
// settled on, and the fallback is worth knowing about.
bool isSoftwareEncoder(const QString& name);

// Elapsed-time readout for the control bar: "0:07", "1:23", "1:02:03".
QString timecode(qint64 ms);

// The same instant for a FILENAME: always "HH-MM-SS", zero-padded and hyphenated, so a
// trimmed copy carries its range in its name. Fixed width (unlike timecode(), which drops
// a zero hour) so the names sort, and hyphens because ':' is illegal in a Windows path.
QString timecodeForFilename(qint64 ms);

// Expand a path template (FilenameTemplate syntax) and force `container`'s extension,
// so a template left over from another container can't mislabel the file. `specials`
// feeds %title% etc. exactly as the image save paths do.
QString resolveOutputPath(const QString& pathTemplate, Container c,
                          const QDateTime& when,
                          const QMap<QString, QString>& specials = {});

// The factory default for recording/output/pathTemplate: the user's Videos folder
// plus a timestamped name. Extension is supplied by resolveOutputPath().
QString defaultPathTemplate();

// A capture region an encoder will accept: clamped to `bounds` and rounded to even
// width/height. Odd sizes are the classic yuv420p failure — chroma is half-resolution,
// so an odd dimension either errors or silently drops a column. Empty in, empty out.
QRect normalizeRegion(const QRect& captureRect, const QRect& bounds);

// fps clamped to what the UI offers (and what a grabber can sustain).
int clampFps(int fps);

// --- Settings round-trip ----------------------------------------------------
// config.ini stores these as stable lowercase keys, never enum ordinals — reordering
// an enum must not silently repoint a user's saved setting at a different codec.
QString keyFor(Codec c);
QString keyFor(Container c);
QString keyFor(HwPreference h);
Codec codecFromKey(const QString& key, Codec fallback = Codec::H264);
Container containerFromKey(const QString& key, Container fallback = Container::Mp4);
HwPreference hwFromKey(const QString& key, HwPreference fallback = HwPreference::Auto);

// --- Rate control -----------------------------------------------------------
QString keyFor(RateMode m);
RateMode rateModeFromKey(const QString& key, RateMode fallback = RateMode::Quality);

// A quality slider reading 0 (smallest file) .. 100 (best picture) mapped onto the
// quantizer `encoder` actually understands. Encoders disagree on both the option NAME
// and the direction of its scale, which is the whole reason this lives in one place.
//   optionName — "crf", "qp", "cq" … or empty when the encoder has no quality mode,
//                in which case the caller must fall back to a target bitrate.
//   value      — already in that encoder's own range and direction.
struct RateControl {
    QString optionName;
    int value = 0;
};
RateControl qualityFor(const QString& encoder, int quality0to100);

} // namespace recording
