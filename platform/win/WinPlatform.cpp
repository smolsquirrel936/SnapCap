#include "platform/platform.h"
#include "platform/win/WinGlobalHotkey.h"
#include "platform/win/WinScreenCapture.h"
#include "platform/win/WinElementDetect.h"
#include "platform/win/WinPinSurface.h"
#include "platform/win/WinAutostart.h"
#include "platform/win/WinClipboard.h"
#include "platform/win/WinKeyboardFilter.h"
#include "platform/win/WinQuickDragGesture.h"
#include "platform/win/WinWheelWatch.h"
#ifdef SNAPCAP_HAVE_TESSERACT
#  include "platform/ocr/TesseractOcr.h"
#  include <QCoreApplication>
#endif
#ifdef SNAPCAP_HAVE_ZXING
#  include "platform/barcode/ZxingBarcodeDecode.h"
#endif
#ifdef SNAPCAP_HAVE_FFMPEG
#  include "platform/win/WinRecorder.h"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <QFileInfo>
#include <QList>
#include <QSet>
#include <QString>
#include <algorithm>
#include <memory>
#include <string>

namespace platform {

// Assembles the concrete Windows backend bundle: global hotkeys (Stage 1),
// screen capture (Stage 2), and window/element detection (Stage 3).
Platform createPlatform()
{
    Platform p;
    p.hotkeys   = std::make_unique<WinGlobalHotkey>();
    p.capture   = std::make_unique<WinScreenCapture>();
    p.detect    = std::make_unique<WinElementDetect>();
    p.autostart = std::make_unique<WinAutostart>();
    p.clipboard = std::make_unique<WinClipboard>();
    p.keyFilter = std::make_unique<WinKeyboardFilter>();
    p.quickDrag = std::make_unique<WinQuickDragGesture>();
    p.wheel     = std::make_unique<WinWheelWatch>();
#ifdef SNAPCAP_HAVE_TESSERACT
    // Tessdata lives in "<exe>/tessdata" (portable); the prefix is the exe dir.
    p.ocr = std::make_unique<TesseractOcr>(QCoreApplication::applicationDirPath());
#endif
#ifdef SNAPCAP_HAVE_ZXING
    p.barcode = std::make_unique<ZxingBarcodeDecode>();
#endif
#ifdef SNAPCAP_HAVE_FFMPEG
    p.recorder = std::make_unique<WinRecorder>();
#endif
    return p;
}

void injectVerticalWheel(int notches)
{
    if (notches == 0)
        return;
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    // Positive scrolls up, so a caller asking to scroll DOWN passes a negative count.
    in.mi.mouseData = DWORD(notches * WHEEL_DELTA);
    SendInput(1, &in, sizeof(INPUT));
}

void injectHorizontalWheel(int notches)
{
    if (notches == 0)
        return;
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_HWHEEL;
    // Positive scrolls RIGHT (the opposite convention to the vertical wheel, where
    // positive is up). An app that ignores WM_MOUSEHWHEEL simply does not move.
    in.mi.mouseData = DWORD(notches * WHEEL_DELTA);
    SendInput(1, &in, sizeof(INPUT));
}

QString foregroundWindowTitle()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return QString();
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0)
        return QString();
    std::wstring buf(size_t(len) + 1, L'\0');
    const int got = GetWindowTextW(hwnd, buf.data(), len + 1);
    return QString::fromWCharArray(buf.data(), got);
}

namespace {

// EnumWindows callback: collect owning-app info for each "real" top-level window
// (visible, unowned, non-tool-window, has a title, not our own process).
BOOL CALLBACK collectApp(HWND hwnd, LPARAM lp)
{
    auto* out = reinterpret_cast<QList<RunningApp>*>(lp);
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
        return TRUE;
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return TRUE;
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0)
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId())
        return TRUE;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return TRUE;
    wchar_t path[MAX_PATH];
    DWORD sz = MAX_PATH;
    QString exe;
    if (QueryFullProcessImageNameW(h, 0, path, &sz))
        exe = QFileInfo(QString::fromWCharArray(path, int(sz))).fileName().toLower();
    CloseHandle(h);
    if (exe.isEmpty())
        return TRUE;
    std::wstring title(size_t(len) + 1, L'\0');
    const int got = GetWindowTextW(hwnd, title.data(), len + 1);
    out->append({exe, QString::fromWCharArray(title.data(), got)});
    return TRUE;
}

} // namespace

QList<RunningApp> runningApplications()
{
    QList<RunningApp> raw;
    ::EnumWindows(&collectApp, reinterpret_cast<LPARAM>(&raw));

    QList<RunningApp> apps;                 // de-dupe by exe basename, keep first title
    QSet<QString> seen;
    for (const RunningApp& a : raw)
        if (!seen.contains(a.exe)) { seen.insert(a.exe); apps.append(a); }
    std::sort(apps.begin(), apps.end(), [](const RunningApp& a, const RunningApp& b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    return apps;
}

#ifndef SNAPCAP_HAVE_FFMPEG
// No recording backend was built, so nothing can encode. The real implementation
// lives in WinRecorder.cpp, which only compiles when FFmpeg is available.
QString activeVideoEncoder(recording::Codec, recording::HwPreference) { return {}; }
#endif

std::unique_ptr<IPinSurface> createPinSurface(WId window)
{
    return std::make_unique<WinPinSurface>(window);
}

} // namespace platform
