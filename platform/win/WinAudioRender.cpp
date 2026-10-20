#include "platform/win/WinAudioRender.h"

#include <QDebug>
#include <QMutexLocker>

#include <chrono>
#include <cstring>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
// See WinAudioCapture.cpp: MinGW has no __uuidof, so the endpoint GUIDs have to be
// instantiated here via initguid.h.
#include <initguid.h>
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace platform {
namespace {
// Half a second of stereo float. Deep enough that a decode hiccup doesn't glitch the
// speakers, shallow enough that a seek doesn't leave stale audio playing.
constexpr size_t kRingFrames = 24000;

AVSampleFormat sampleFormatOf(const WAVEFORMATEX* wfx)
{
    WORD tag = wfx->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && wfx->cbSize >= 22)
        tag = WORD(reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx)->SubFormat.Data1);
    if (tag == WAVE_FORMAT_IEEE_FLOAT)
        return AV_SAMPLE_FMT_FLT;
    if (tag == WAVE_FORMAT_PCM && wfx->wBitsPerSample == 16)
        return AV_SAMPLE_FMT_S16;
    if (tag == WAVE_FORMAT_PCM && wfx->wBitsPerSample == 32)
        return AV_SAMPLE_FMT_S32;
    return AV_SAMPLE_FMT_NONE;
}
} // namespace

WinAudioRender::WinAudioRender() = default;

WinAudioRender::~WinAudioRender() { stop(); }

bool WinAudioRender::start(int sampleRate, int channels)
{
    if (m_running.load())
        return true;
    m_srcRate = sampleRate > 0 ? sampleRate : 48000;
    m_srcCh = channels > 0 ? channels : 2;
    {
        QMutexLocker lock(&m_lock);
        m_ring.clear();
        m_ring.reserve(kRingFrames * size_t(m_srcCh));
    }
    m_written.store(0);
    m_consumed.store(0);
    m_stop.store(false);
    m_thread = std::make_unique<std::thread>([this] { run(); });
    for (int i = 0; i < 300 && !m_running.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return m_running.load();
}

void WinAudioRender::stop()
{
    m_stop.store(true);
    if (m_thread && m_thread->joinable())
        m_thread->join();
    m_thread.reset();
    m_running.store(false);
}

void WinAudioRender::reset()
{
    QMutexLocker lock(&m_lock);
    m_ring.clear();
    // The clock restarts with the buffer: after a seek, "frames played" must mean
    // frames played SINCE the seek, or the player would present the wrong frames.
    m_written.store(0);
    m_consumed.store(0);
}

int WinAudioRender::space() const
{
    QMutexLocker lock(&m_lock);
    const size_t used = m_ring.size() / size_t(m_srcCh);
    return int(kRingFrames > used ? kRingFrames - used : 0);
}

int WinAudioRender::write(const float* interleaved, int frames)
{
    if (frames <= 0)
        return 0;
    QMutexLocker lock(&m_lock);
    const size_t used = m_ring.size() / size_t(m_srcCh);
    const int room = int(kRingFrames > used ? kRingFrames - used : 0);
    const int take = std::min(frames, room);
    if (take <= 0)
        return 0;
    m_ring.insert(m_ring.end(), interleaved, interleaved + size_t(take) * size_t(m_srcCh));
    m_written.fetch_add(take);
    return take;
}

qint64 WinAudioRender::playedFrames() const
{
    return m_consumed.load();
}

void WinAudioRender::run()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comOwned = SUCCEEDED(hr);

    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    WAVEFORMATEX* wfx = nullptr;
    SwrContext* swr = nullptr;
    UINT32 bufferFrames = 0;

    auto cleanup = [&] {
        if (swr) swr_free(&swr);
        if (render) render->Release();
        if (client) { client->Stop(); client->Release(); }
        if (wfx) CoTaskMemFree(wfx);
        if (dev) dev->Release();
        if (en) en->Release();
        if (comOwned) CoUninitialize();
        m_running.store(false);
    };

    if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                IID_IMMDeviceEnumerator, reinterpret_cast<void**>(&en)))
        || FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))
        || FAILED(dev->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(&client)))
        || FAILED(client->GetMixFormat(&wfx)) || !wfx) {
        qWarning() << "[preview] no audio output device — playing silently";
        cleanup();
        return;
    }

    const AVSampleFormat outFmt = sampleFormatOf(wfx);
    if (outFmt == AV_SAMPLE_FMT_NONE) { cleanup(); return; }

    const REFERENCE_TIME kBuffer = 10'000'000 / 5;      // 200 ms
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBuffer, 0, wfx, nullptr))
        || FAILED(client->GetBufferSize(&bufferFrames))
        || FAILED(client->GetService(IID_IAudioRenderClient, reinterpret_cast<void**>(&render)))) {
        cleanup();
        return;
    }

    AVChannelLayout inLayout, outLayout;
    av_channel_layout_default(&inLayout, m_srcCh);
    av_channel_layout_default(&outLayout, wfx->nChannels);
    if (swr_alloc_set_opts2(&swr, &outLayout, outFmt, int(wfx->nSamplesPerSec),
                            &inLayout, AV_SAMPLE_FMT_FLT, m_srcRate, 0, nullptr) < 0
        || swr_init(swr) < 0) {
        av_channel_layout_uninit(&inLayout);
        av_channel_layout_uninit(&outLayout);
        cleanup();
        return;
    }

    client->Start();
    m_running.store(true);

    std::vector<float> chunk;
    while (!m_stop.load()) {
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding)))
            break;
        const UINT32 free = bufferFrames > padding ? bufferFrames - padding : 0;
        if (free == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // How many SOURCE frames fill that much device time.
        const int wantSrc = int(av_rescale_rnd(free, m_srcRate, wfx->nSamplesPerSec, AV_ROUND_DOWN));
        int haveSrc = 0;
        {
            QMutexLocker lock(&m_lock);
            haveSrc = int(m_ring.size() / size_t(m_srcCh));
            const int take = std::min(wantSrc, haveSrc);
            if (take > 0) {
                chunk.assign(m_ring.begin(), m_ring.begin() + ptrdiff_t(size_t(take) * m_srcCh));
                m_ring.erase(m_ring.begin(), m_ring.begin() + ptrdiff_t(size_t(take) * m_srcCh));
            }
            haveSrc = take;
        }
        if (haveSrc <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
            continue;
        }

        const int maxOut = int(av_rescale_rnd(swr_get_delay(swr, m_srcRate) + haveSrc,
                                              wfx->nSamplesPerSec, m_srcRate, AV_ROUND_UP));
        BYTE* dst = nullptr;
        if (FAILED(render->GetBuffer(UINT32(std::min<int>(maxOut, int(free))), &dst)) || !dst)
            continue;
        const uint8_t* src = reinterpret_cast<const uint8_t*>(chunk.data());
        const int got = swr_convert(swr, &dst, std::min<int>(maxOut, int(free)), &src, haveSrc);
        render->ReleaseBuffer(got > 0 ? UINT32(got) : 0,
                              got > 0 ? 0 : AUDCLNT_BUFFERFLAGS_SILENT);
        if (got > 0) {
            // Count in SOURCE frames: the player's clock is in the file's own rate.
            m_consumed.fetch_add(av_rescale_rnd(got, m_srcRate, wfx->nSamplesPerSec, AV_ROUND_DOWN));
        }
    }

    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);
    cleanup();
}

std::unique_ptr<IAudioSink> createAudioSink()
{
    return std::make_unique<WinAudioRender>();
}

} // namespace platform
