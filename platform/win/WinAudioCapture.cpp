#include "platform/win/WinAudioCapture.h"

#include "core/recording/RecordingSpec.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMutexLocker>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
// initguid.h must precede the endpoint headers: it is what turns their DEFINE_GUID
// declarations into actual symbols in this translation unit. MinGW has no __uuidof,
// so CLSID_MMDeviceEnumerator & friends have to be instantiated here.
#include <initguid.h>
#include <windows.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace platform {
namespace {

constexpr int kOutRate = recording::kAudioSampleRate;
constexpr int kOutCh   = recording::kAudioChannels;
// One second of stereo float. The recorder drains this every video frame, so it only
// fills up if the encode loop stalls — at which point dropping the oldest audio is
// the right call anyway (it keeps latency bounded instead of growing forever).
constexpr size_t kFifoCap = size_t(kOutRate) * kOutCh;

// Loopback hands us the mix AFTER the speaker slider has been applied, so a quiet
// system records a quiet file — measured here as a 42 dB loss at a 6% slider, which
// is precisely the "why is my recording so much quieter than the video?" complaint.
// Dividing the endpoint's own gain back out makes the recording independent of the
// slider, which is what a screen recorder should do.
//
// Only the MASTER endpoint gain can be undone this way. A per-app level in the Volume
// Mixer is applied before the mix and is not recoverable — see the Usage.md note.
// The floor caps the boost at 60 dB so a near-silent endpoint can't run away; the
// engine mixes in float, so dividing is mathematically clean rather than noise
// amplification.
constexpr float kMinGain = 0.001f;

float endpointGain(IAudioEndpointVolume* vol)
{
    if (!vol)
        return 1.0f;
    BOOL mute = FALSE;
    if (SUCCEEDED(vol->GetMute(&mute)) && mute)
        return 0.0f;                       // genuinely silent: nothing to recover
    float db = 0.0f;
    if (FAILED(vol->GetMasterVolumeLevel(&db)))
        return 1.0f;
    return std::pow(10.0f, db / 20.0f);    // dB attenuation -> the linear multiplier applied
}

// A WAVEFORMATEXTENSIBLE's SubFormat GUID is {tag,0000,0010,8000,00AA00389B71}, so
// its Data1 IS the classic format tag. Reading it that way avoids pulling in
// ksmedia.h just for KSDATAFORMAT_SUBTYPE_IEEE_FLOAT.
AVSampleFormat sampleFormatOf(const WAVEFORMATEX* wfx)
{
    WORD tag = wfx->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && wfx->cbSize >= 22)
        tag = WORD(reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx)->SubFormat.Data1);

    if (tag == WAVE_FORMAT_IEEE_FLOAT)
        return AV_SAMPLE_FMT_FLT;
    if (tag == WAVE_FORMAT_PCM) {
        switch (wfx->wBitsPerSample) {
            case 16: return AV_SAMPLE_FMT_S16;
            case 32: return AV_SAMPLE_FMT_S32;
            default: break;
        }
    }
    return AV_SAMPLE_FMT_NONE;
}

} // namespace

WinAudioCapture::WinAudioCapture() = default;

WinAudioCapture::~WinAudioCapture()
{
    stop();
}

bool WinAudioCapture::start(Source source)
{
    if (m_running.load())
        return true;
    m_stop.store(false);
    m_error.clear();
    m_thread = std::make_unique<std::thread>([this, source] { run(source); });

    // The thread reports success by flipping m_running; wait briefly so callers get a
    // truthful answer (device errors surface immediately, not a second later).
    for (int i = 0; i < 200 && !m_running.load() && m_error.isEmpty(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return m_running.load();
}

void WinAudioCapture::stop()
{
    m_stop.store(true);
    if (m_thread && m_thread->joinable())
        m_thread->join();
    m_thread.reset();
    m_running.store(false);
    discard();
}

void WinAudioCapture::discard()
{
    QMutexLocker lock(&m_lock);
    m_fifo.clear();
}

void WinAudioCapture::read(float* dst, int frames)
{
    const size_t want = size_t(frames) * kOutCh;
    QMutexLocker lock(&m_lock);
    const size_t have = std::min(want, m_fifo.size());
    if (have)
        std::memcpy(dst, m_fifo.data(), have * sizeof(float));
    if (have < want)                       // underrun (idle loopback is the usual one)
        std::memset(dst + have, 0, (want - have) * sizeof(float));
    m_fifo.erase(m_fifo.begin(), m_fifo.begin() + ptrdiff_t(have));
}

void WinAudioCapture::run(Source source)
{
    const bool loopback = (source == Source::Loopback);
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comOwned = SUCCEEDED(hr);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    IAudioEndpointVolume* endpointVol = nullptr;
    WAVEFORMATEX* wfx = nullptr;
    SwrContext* swr = nullptr;

    auto cleanup = [&] {
        if (swr) swr_free(&swr);
        if (endpointVol) endpointVol->Release();
        if (capture) capture->Release();
        if (client) { client->Stop(); client->Release(); }
        if (wfx) CoTaskMemFree(wfx);
        if (device) device->Release();
        if (enumerator) enumerator->Release();
        if (comOwned) CoUninitialize();
        m_running.store(false);
    };

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                          IID_IMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) { m_error = QStringLiteral("no audio device enumerator"); cleanup(); return; }

    // Loopback taps the RENDER endpoint (what is being played); a mic is a CAPTURE one.
    hr = enumerator->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole, &device);
    if (FAILED(hr)) { m_error = QStringLiteral("no default audio endpoint"); cleanup(); return; }

    hr = device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
    if (FAILED(hr)) { m_error = QStringLiteral("could not activate audio client"); cleanup(); return; }

    hr = client->GetMixFormat(&wfx);
    if (FAILED(hr) || !wfx) { m_error = QStringLiteral("no mix format"); cleanup(); return; }

    const AVSampleFormat inFmt = sampleFormatOf(wfx);
    if (inFmt == AV_SAMPLE_FMT_NONE) {
        m_error = QStringLiteral("unsupported endpoint sample format");
        cleanup();
        return;
    }

    // Shared mode at the endpoint's own mix format — the only combination guaranteed
    // to be accepted; we resample afterwards rather than asking the device to convert.
    const REFERENCE_TIME kBuffer = 10'000'000 / 5;      // 200 ms
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0,
                            kBuffer, 0, wfx, nullptr);
    if (FAILED(hr)) { m_error = QStringLiteral("could not open audio stream"); cleanup(); return; }

    hr = client->GetService(IID_IAudioCaptureClient, reinterpret_cast<void**>(&capture));
    if (FAILED(hr)) { m_error = QStringLiteral("no capture service"); cleanup(); return; }

    AVChannelLayout inLayout;
    av_channel_layout_default(&inLayout, wfx->nChannels);
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kOutCh);
    if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, kOutRate,
                            &inLayout, inFmt, int(wfx->nSamplesPerSec), 0, nullptr) < 0
        || swr_init(swr) < 0) {
        m_error = QStringLiteral("could not build the audio resampler");
        cleanup();
        return;
    }

    // Only the speaker path gets volume compensation. A microphone's level IS the
    // level the user chose to record at, so undoing it would fight them.
    if (loopback)
        device->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, nullptr,
                         reinterpret_cast<void**>(&endpointVol));

    if (FAILED(client->Start())) { m_error = QStringLiteral("could not start audio capture"); cleanup(); return; }

    qInfo() << "[record] audio:" << (loopback ? "system (loopback)" : "microphone")
            << wfx->nSamplesPerSec << "Hz" << wfx->nChannels << "ch"
            << av_get_sample_fmt_name(inFmt) << wfx->wBitsPerSample << "bit";
    m_running.store(true);

    std::vector<float> converted;
    // Re-read the slider periodically rather than per packet: it is a COM call, and
    // the user may well change the volume mid-recording.
    float gain = endpointGain(endpointVol);
    QElapsedTimer gainClock;
    gainClock.start();
    if (endpointVol)
        qInfo("[record] system volume compensation: %.1f dB (endpoint gain %.5f)",
              gain > 0 ? -20.0 * std::log10(std::max(gain, kMinGain)) : 0.0, gain);

    while (!m_stop.load()) {
        if (endpointVol && gainClock.elapsed() >= 250) {
            gain = endpointGain(endpointVol);
            gainClock.restart();
        }
        UINT32 packet = 0;
        if (FAILED(capture->GetNextPacketSize(&packet)) )
            break;
        if (packet == 0) {
            // Nothing queued. For loopback this is the normal idle case — the silence
            // it implies is supplied by read()'s zero-padding, not by us.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        while (packet > 0 && !m_stop.load()) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;

            if (frames > 0) {
                const int maxOut = int(av_rescale_rnd(swr_get_delay(swr, wfx->nSamplesPerSec) + frames,
                                                      kOutRate, wfx->nSamplesPerSec, AV_ROUND_UP));
                converted.resize(size_t(maxOut) * kOutCh);
                uint8_t* outPtr = reinterpret_cast<uint8_t*>(converted.data());
                // AUDCLNT_BUFFERFLAGS_SILENT means "the buffer contents are not
                // meaningful" — feed the resampler zeros rather than whatever is there.
                const uint8_t* inPtr = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? nullptr : data;
                const int got = inPtr
                    ? swr_convert(swr, &outPtr, maxOut, &inPtr, int(frames))
                    : swr_convert(swr, &outPtr, maxOut, nullptr, 0);
                if (got > 0) {
                    // Undo the speaker slider so the file's level reflects the source,
                    // not how loud the machine happened to be playing it.
                    if (endpointVol && gain > 0.0f && gain < 1.0f) {
                        const float boost = 1.0f / std::max(gain, kMinGain);
                        const size_t n = size_t(got) * kOutCh;
                        for (size_t i = 0; i < n; ++i)
                            converted[i] = qBound(-1.0f, converted[i] * boost, 1.0f);
                    }
                    QMutexLocker lock(&m_lock);
                    const size_t add = size_t(got) * kOutCh;
                    if (m_fifo.size() + add > kFifoCap) {
                        // Encode loop stalled: drop the OLDEST audio so latency stays
                        // bounded and the stream keeps tracking the video.
                        const size_t over = m_fifo.size() + add - kFifoCap;
                        m_fifo.erase(m_fifo.begin(),
                                     m_fifo.begin() + ptrdiff_t(std::min(over, m_fifo.size())));
                    }
                    m_fifo.insert(m_fifo.end(), converted.begin(), converted.begin() + ptrdiff_t(add));
                }
            }
            capture->ReleaseBuffer(frames);
            if (FAILED(capture->GetNextPacketSize(&packet)))
                break;
        }
    }

    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);
    cleanup();
}

} // namespace platform
