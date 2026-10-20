#include <QtTest>

#include "core/recording/RecordingSpec.h"

using namespace recording;

// Pure recording-config logic: container/codec compatibility, encoder candidate
// ordering, the elapsed-time readout, output-path resolution and region hygiene.
// No FFmpeg is involved — these are the answers the backend translates into
// libav* calls.
class TstRecordingSpec : public QObject
{
    Q_OBJECT
private slots:
    void extensionsAndMuxers();
    void webmRejectsPatentedCodecs();
    void gifCarriesNoCodec();
    void containersForCodec();
    void candidatesAlwaysEndInSoftware();
    void forceSoftwareDropsHardware();
    void vp9HasOnlyQsvHardware();
    void timecodeFormats();
    void filenameTimecodeIsFixedWidth();
    void outputPathForcesContainerExtension();
    void outputPathKeepsInnerDots();
    void outputPathExpandsDateTokens();
    void emptyTemplateFallsBackToDefault();
    void regionRoundsToEvenAndClamps();
    void regionRejectsDegenerate();
    void fpsIsClamped();
    void audioIsOptional();
    void audioEncoderFollowsContainer();
    void audioPullIsLockedToTheFrameRate();
    void settingsKeysRoundTrip();
    void settingsKeysToleratePeople();
    void qualityMapsPerEncoder();
    void qualityIsMonotonicAndBounded();
    void bitrateOnlyEncodersReportNoQualityKnob();
};

void TstRecordingSpec::settingsKeysRoundTrip()
{
    for (Codec c : {Codec::H264, Codec::H265, Codec::AV1, Codec::VP9})
        QCOMPARE(codecFromKey(keyFor(c)), c);
    for (Container c : {Container::Mp4, Container::Mkv, Container::WebM, Container::Gif})
        QCOMPARE(containerFromKey(keyFor(c)), c);
    for (HwPreference h : {HwPreference::Auto, HwPreference::PreferHardware,
                           HwPreference::ForceSoftware})
        QCOMPARE(hwFromKey(keyFor(h)), h);
    for (RateMode m : {RateMode::Quality, RateMode::Bitrate})
        QCOMPARE(rateModeFromKey(keyFor(m)), m);

    // The stored form must be a STABLE string, not an enum ordinal — reordering the
    // enum must never repoint a saved setting at a different codec.
    QCOMPARE(keyFor(Codec::H265), QStringLiteral("h265"));
    QCOMPARE(keyFor(Container::WebM), QStringLiteral("webm"));
}

void TstRecordingSpec::settingsKeysToleratePeople()
{
    // config.ini is hand-editable, so casing/whitespace must not matter...
    QCOMPARE(codecFromKey(QStringLiteral("  H265 ")), Codec::H265);
    QCOMPARE(containerFromKey(QStringLiteral("WEBM")), Container::WebM);
    // ...and the names people actually use are accepted as aliases.
    QCOMPARE(codecFromKey(QStringLiteral("hevc")), Codec::H265);
    QCOMPARE(containerFromKey(QStringLiteral("matroska")), Container::Mkv);
    // Anything unrecognised falls back instead of throwing the setting away.
    QCOMPARE(codecFromKey(QStringLiteral("nonsense")), Codec::H264);
    QCOMPARE(codecFromKey(QStringLiteral(""), Codec::VP9), Codec::VP9);
}

void TstRecordingSpec::qualityMapsPerEncoder()
{
    // Each encoder gets ITS OWN option name — the point of the mapping.
    QCOMPARE(qualityFor(QStringLiteral("libsvtav1"), 50).optionName, QStringLiteral("crf"));
    QCOMPARE(qualityFor(QStringLiteral("libkvazaar"), 50).optionName, QStringLiteral("qp"));
    QCOMPARE(qualityFor(QStringLiteral("h264_nvenc"), 50).optionName, QStringLiteral("cq"));
    QCOMPARE(qualityFor(QStringLiteral("hevc_qsv"), 50).optionName, QStringLiteral("global_quality"));
    QCOMPARE(qualityFor(QStringLiteral("av1_amf"), 50).optionName, QStringLiteral("qp_p"));
}

void TstRecordingSpec::qualityIsMonotonicAndBounded()
{
    for (const char* enc : {"libsvtav1", "libvpx-vp9", "libkvazaar", "h264_nvenc"}) {
        const QString e = QString::fromLatin1(enc);
        const int best  = qualityFor(e, 100).value;
        const int mid   = qualityFor(e, 50).value;
        const int worst = qualityFor(e, 0).value;
        // Every one of these scales is "lower number = better picture", so a higher
        // slider must produce a strictly lower quantizer.
        QVERIFY2(best < mid && mid < worst, enc);
        // ...and the ends must land exactly on the declared range, not near it.
        QCOMPARE(qualityFor(e, 150).value, best);   // clamped
        QCOMPARE(qualityFor(e, -10).value, worst);
    }
}

void TstRecordingSpec::bitrateOnlyEncodersReportNoQualityKnob()
{
    // libopenh264 and the MediaFoundation wrappers have no constant-quality mode;
    // the caller must see an empty option name and fall back to a bitrate rather
    // than setting an option they would reject.
    QVERIFY(qualityFor(QStringLiteral("libopenh264"), 70).optionName.isEmpty());
    QVERIFY(qualityFor(QStringLiteral("h264_mf"), 70).optionName.isEmpty());
    QVERIFY(qualityFor(QStringLiteral("hevc_mf"), 70).optionName.isEmpty());
}

void TstRecordingSpec::audioIsOptional()
{
    Config c;
    c.systemAudio = false;
    c.microphone = false;
    QVERIFY(!wantsAudio(c));          // neither source ⇒ no audio stream at all
    c.microphone = true;
    QVERIFY(wantsAudio(c));
    c.systemAudio = true;
    QVERIFY(wantsAudio(c));           // both sources still means ONE mixed track
}

void TstRecordingSpec::audioEncoderFollowsContainer()
{
    // WebM cannot carry AAC; everything else takes it.
    QCOMPARE(audioEncoderFor(Container::WebM), QStringLiteral("libopus"));
    QCOMPARE(audioEncoderFor(Container::Mp4),  QStringLiteral("aac"));
    QCOMPARE(audioEncoderFor(Container::Mkv),  QStringLiteral("aac"));
}

void TstRecordingSpec::audioPullIsLockedToTheFrameRate()
{
    // Audio is pulled per encoded video frame, so the two clocks cannot drift apart.
    QCOMPARE(audioSamplesPerFrame(30), 1600);
    QCOMPARE(audioSamplesPerFrame(60), 800);
    QCOMPARE(audioSamplesPerFrame(5),  9600);
    // ...and an out-of-range fps is clamped first, never producing a zero pull.
    QCOMPARE(audioSamplesPerFrame(0), audioSamplesPerFrame(5));
    QVERIFY(audioSamplesPerFrame(240) > 0);
    QCOMPARE(kAudioSampleRate, 48000);
    QCOMPARE(kAudioChannels, 2);
}

void TstRecordingSpec::extensionsAndMuxers()
{
    QCOMPARE(extensionFor(Container::Mp4),  QStringLiteral("mp4"));
    QCOMPARE(extensionFor(Container::Mkv),  QStringLiteral("mkv"));
    QCOMPARE(extensionFor(Container::WebM), QStringLiteral("webm"));
    QCOMPARE(extensionFor(Container::Gif),  QStringLiteral("gif"));

    // The muxer name is libavformat's, which differs from the extension for MKV.
    QCOMPARE(muxerFor(Container::Mkv),  QStringLiteral("matroska"));
    QCOMPARE(muxerFor(Container::WebM), QStringLiteral("webm"));
}

void TstRecordingSpec::webmRejectsPatentedCodecs()
{
    QVERIFY(supports(Container::WebM, Codec::VP9));
    QVERIFY(supports(Container::WebM, Codec::AV1));
    QVERIFY(!supports(Container::WebM, Codec::H264));
    QVERIFY(!supports(Container::WebM, Codec::H265));

    // MP4 and MKV take everything.
    for (Codec v : {Codec::H264, Codec::H265, Codec::AV1, Codec::VP9}) {
        QVERIFY(supports(Container::Mp4, v));
        QVERIFY(supports(Container::Mkv, v));
    }
}

void TstRecordingSpec::gifCarriesNoCodec()
{
    for (Codec v : {Codec::H264, Codec::H265, Codec::AV1, Codec::VP9})
        QVERIFY(!supports(Container::Gif, v));
}

void TstRecordingSpec::containersForCodec()
{
    QCOMPARE(containersFor(Codec::H264), QVector<Container>({Container::Mp4, Container::Mkv}));
    QCOMPARE(containersFor(Codec::VP9),
             QVector<Container>({Container::Mp4, Container::Mkv, Container::WebM}));
    // GIF is never offered as a container *for a codec* — it is its own output mode.
    for (Codec v : {Codec::H264, Codec::H265, Codec::AV1, Codec::VP9})
        QVERIFY(!containersFor(v).contains(Container::Gif));
}

void TstRecordingSpec::candidatesAlwaysEndInSoftware()
{
    for (Codec v : {Codec::H264, Codec::H265, Codec::AV1, Codec::VP9}) {
        for (HwPreference hw : {HwPreference::Auto, HwPreference::PreferHardware}) {
            const QStringList c = encoderCandidates(v, hw);
            QVERIFY(!c.isEmpty());
            // The last resort must be software, or a machine with no GPU encoder
            // would have nothing to fall back to.
            QVERIFY2(isSoftwareEncoder(c.last()),
                     qPrintable(QStringLiteral("last candidate not software: ") + c.last()));
            // ...and exactly one software entry, at the end.
            int sw = 0;
            for (const QString& n : c)
                if (isSoftwareEncoder(n)) ++sw;
            QCOMPARE(sw, 1);
        }
    }
}

void TstRecordingSpec::forceSoftwareDropsHardware()
{
    const QStringList c = encoderCandidates(Codec::H265, HwPreference::ForceSoftware);
    QCOMPARE(c, QStringList{QStringLiteral("libkvazaar")});

    // H.265 in software is the finding that kept the codec shippable — x265 is GPL
    // and absent, so libkvazaar is the only software HEVC encoder we have.
    QVERIFY(isSoftwareEncoder(QStringLiteral("libkvazaar")));
}

void TstRecordingSpec::vp9HasOnlyQsvHardware()
{
    const QStringList c = encoderCandidates(Codec::VP9, HwPreference::PreferHardware);
    QCOMPARE(c, QStringList({QStringLiteral("vp9_qsv"), QStringLiteral("libvpx-vp9")}));
}

void TstRecordingSpec::timecodeFormats()
{
    QCOMPARE(timecode(0),          QStringLiteral("0:00"));
    QCOMPARE(timecode(7'400),      QStringLiteral("0:07"));   // truncates, never rounds up
    QCOMPARE(timecode(59'999),     QStringLiteral("0:59"));
    QCOMPARE(timecode(60'000),     QStringLiteral("1:00"));
    QCOMPARE(timecode(83'000),     QStringLiteral("1:23"));
    QCOMPARE(timecode(3'723'000),  QStringLiteral("1:02:03"));
    QCOMPARE(timecode(-5),         QStringLiteral("0:00"));   // clock skew must not print junk
}

void TstRecordingSpec::filenameTimecodeIsFixedWidth()
{
    // Always HH-MM-SS, so the two ends of a trimmed range are the same width and the
    // names sort. No ':' anywhere — it cannot appear in a Windows filename.
    QCOMPARE(timecodeForFilename(0),         QStringLiteral("00-00-00"));
    QCOMPARE(timecodeForFilename(140'000),   QStringLiteral("00-02-20"));
    QCOMPARE(timecodeForFilename(315'000),   QStringLiteral("00-05-15"));
    QCOMPARE(timecodeForFilename(3'723'000), QStringLiteral("01-02-03"));
    QCOMPARE(timecodeForFilename(7'400),     QStringLiteral("00-00-07"));  // truncates, as timecode()
    QCOMPARE(timecodeForFilename(-5),        QStringLiteral("00-00-00"));
}

void TstRecordingSpec::outputPathForcesContainerExtension()
{
    const QDateTime when(QDate(2026, 7, 30), QTime(14, 5, 9));

    // A template carrying the wrong extension is corrected, not appended to.
    QCOMPARE(resolveOutputPath(QStringLiteral("C:/vids/clip.mp4"), Container::WebM, when),
             QStringLiteral("C:/vids/clip.webm"));
    // No extension at all: one is supplied.
    QCOMPARE(resolveOutputPath(QStringLiteral("C:/vids/clip"), Container::Mp4, when),
             QStringLiteral("C:/vids/clip.mp4"));
    QCOMPARE(resolveOutputPath(QStringLiteral("C:/vids/clip"), Container::Gif, when),
             QStringLiteral("C:/vids/clip.gif"));
}

void TstRecordingSpec::outputPathKeepsInnerDots()
{
    const QDateTime when(QDate(2026, 7, 30), QTime(14, 5, 9));
    QCOMPARE(resolveOutputPath(QStringLiteral("C:/vids/clip.2026.07.mp4"), Container::Mkv, when),
             QStringLiteral("C:/vids/clip.2026.07.mkv"));
}

void TstRecordingSpec::outputPathExpandsDateTokens()
{
    const QDateTime when(QDate(2026, 7, 30), QTime(14, 5, 9));
    QCOMPARE(resolveOutputPath(QStringLiteral("C:/vids/SnapCap_$yyyy-MM-dd_HH-mm-ss$"),
                               Container::Mp4, when),
             QStringLiteral("C:/vids/SnapCap_2026-07-30_14-05-09.mp4"));

    // %title% comes from the specials map, exactly as the image save paths do.
    QMap<QString, QString> specials{{QStringLiteral("title"), QStringLiteral("Notepad")}};
    QCOMPARE(resolveOutputPath(QStringLiteral("C:/vids/%title%_$yyyy$"), Container::Mp4,
                               when, specials),
             QStringLiteral("C:/vids/Notepad_2026.mp4"));
}

void TstRecordingSpec::emptyTemplateFallsBackToDefault()
{
    const QDateTime when(QDate(2026, 7, 30), QTime(14, 5, 9));
    const QString p = resolveOutputPath(QString(), Container::Mp4, when);
    QVERIFY(p.endsWith(QStringLiteral(".mp4")));
    QVERIFY(p.contains(QStringLiteral("SnapCap_2026-07-30_14-05-09")));
    QVERIFY(!defaultPathTemplate().isEmpty());
}

void TstRecordingSpec::regionRoundsToEvenAndClamps()
{
    const QRect bounds(0, 0, 1920, 1080);

    // Odd dimensions shrink by one — yuv420p cannot take them.
    QCOMPARE(normalizeRegion(QRect(10, 20, 101, 203), bounds), QRect(10, 20, 100, 202));
    // Already even: untouched.
    QCOMPARE(normalizeRegion(QRect(10, 20, 100, 200), bounds), QRect(10, 20, 100, 200));
    // Overhanging the desktop is clipped, and the clipped size is then evened —
    // shrinking, so the result can never spill back outside bounds.
    const QRect clipped = normalizeRegion(QRect(1900, 1000, 500, 500), bounds);
    QVERIFY(bounds.contains(clipped));
    QCOMPARE(clipped.width() % 2, 0);
    QCOMPARE(clipped.height() % 2, 0);
}

void TstRecordingSpec::regionRejectsDegenerate()
{
    const QRect bounds(0, 0, 1920, 1080);
    QVERIFY(normalizeRegion(QRect(), bounds).isEmpty());
    QVERIFY(normalizeRegion(QRect(10, 10, 1, 1), bounds).isEmpty());     // 1px → 0 after evening
    QVERIFY(normalizeRegion(QRect(5000, 5000, 100, 100), bounds).isEmpty());  // fully outside
}

void TstRecordingSpec::fpsIsClamped()
{
    QCOMPARE(clampFps(30), 30);
    QCOMPARE(clampFps(0), 5);
    QCOMPARE(clampFps(240), 60);
}

QTEST_GUILESS_MAIN(TstRecordingSpec)
#include "tst_recording_spec.moc"
