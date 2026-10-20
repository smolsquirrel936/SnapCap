#pragma once

#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <qwindowdefs.h>      // WId
#include <functional>
#include <memory>

#include "core/recording/RecordingSpec.h"   // recording::Config (IRecorder::Request)

// SnapCap platform-abstraction layer.
//
// Everything OS-specific lives behind these interfaces so the core/UI stay
// platform-agnostic. Windows is the only backend implemented for now
// (platform/win/*). Mac/Linux backends can be added later by implementing the
// same interfaces and wiring them in createPlatform().
//
// See claude-plan/Tech-Stack-Plan.md ("Target architecture").
namespace platform {

// --- Screen capture ---------------------------------------------------------
// Snapshot of the mouse cursor for compositing into a capture.
struct CursorImage {
    bool   visible = false;   // false when there is no cursor to draw
    QImage image;             // ARGB32; the cursor as rendered (colour + mask combined)
    QPoint hotspot;           // hotspot in image pixels
};

class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;
    // Full virtual-desktop grab (all monitors, in device pixels).
    virtual QImage grabVirtualDesktop() = 0;
    // Grab a sub-rect of the virtual desktop (logical coords; impl handles DPI).
    virtual QImage grabRect(const QRect& rect) = 0;
    // Snapshot the current mouse cursor. Default: nothing to draw.
    virtual CursorImage grabCursor() { return {}; }
};

// --- Native monitor geometry (mixed-DPI snip coordinate model) --------------
// Per-monitor Qt logical geometry paired with its native physical-pixel rect.
// Feeds core/ScreenLayout so the snip pipeline works in gap-free physical-pixel
// space across monitors with different DPRs.
struct MonitorInfo {
    QRect logicalGeometry;    // Qt logical (QScreen::geometry)
    QRect captureGeometry;    // native physical px (Win32 rcMonitor)
};

// Every monitor, in the same order as QGuiApplication::screens(). Implemented
// per-OS in platform/<os>/.
QList<MonitorInfo> monitorLayout();

// Title of the current foreground window (empty if none / unsupported). Used to
// expand the %title% filename token; capture it before an overlay steals focus.
QString foregroundWindowTitle();

// Visible frame of the current foreground window in **capture px** (native
// physical), ready to hand to SnipOverlay::setPresetArea — this is what the
// "Snip active window" hotkey selects. Excludes the invisible resize border and
// drop shadow, so it matches what the overlay's own window detection highlights.
// SnapCap's own windows are skipped (a focused pin falls through to the window
// beneath it). Returns a null rect when nothing suitable is on screen; like
// foregroundWindowTitle(), call it before an overlay steals focus.
// Windows impl lives in platform/win/WinElementDetect.cpp, next to the frame and
// window-filtering helpers it shares with the hover hit test.
QRect foregroundWindowRect();

// A running application with a visible top-level window: its executable basename
// (e.g. "chrome.exe", lowercased) and the window title (for display). Used by
// Preferences → Control → App exclusions to pick an app to exempt from hotkeys.
struct RunningApp {
    QString exe;     // executable basename, lowercased
    QString title;   // window title
};
// Visible top-level windows' owning apps, de-duplicated by exe basename, sorted
// by title. Empty on platforms without a backend.
QList<RunningApp> runningApplications();

// --- Global hotkeys (not provided by Qt) ------------------------------------
class IGlobalHotkey {
public:
    virtual ~IGlobalHotkey() = default;
    // Register a system-wide hotkey from a portable string e.g. "F1",
    // "Ctrl+Shift+S". Returns an id (>=0) or -1 on failure.
    virtual int registerHotkey(const QString& sequence, std::function<void()> onActivated) = 0;
    virtual void unregister(int id) = 0;
    virtual void unregisterAll() = 0;              // drop every registration (e.g. before a remap)
    // True if the sequence is currently free to register system-wide (i.e. no
    // other app owns it). Used by Preferences to flag occupied hotkeys. Probes
    // by test-registering then immediately releasing, so the caller must have
    // released its own bindings first to avoid self-conflicts. Empty/invalid
    // sequences return false. Default (non-Windows) assumes available.
    virtual bool probe(const QString& sequence) { (void)sequence; return true; }
    // Suppress every registered hotkey while one of these apps (matched by exe
    // basename, case-insensitive) is the foreground window, so the app receives
    // the keys itself. Pass an empty list to disable. Default (non-Windows): no-op.
    virtual void setIgnoredApps(const QStringList& exeBasenames) { (void)exeBasenames; }
};

// --- Window / UI-element detection under the cursor --------------------------
class IElementDetect {
public:
    virtual ~IElementDetect() = default;
    // Bounding rect of the top-level window under the given global point.
    virtual QRect windowAt(const QPoint& globalPos) = 0;
    // Bounding rect of the finest UI element under the point. One provider hit test
    // — cheap enough to run on every throttled hover.
    virtual QRect elementAt(const QPoint& globalPos) = 0;
    // The element under the point AND its ancestors: innermost first, ending at the
    // top-level window frame. Each entry strictly encloses the one before it.
    //
    // Costs one cross-process round trip PER LEVEL (measured p90 516ms / max 762ms
    // on a 13-level browser chain), so callers must resolve this LAZILY — on the
    // first hierarchy step, never during plain hovering.
    virtual QList<QRect> elementChainAt(const QPoint& globalPos) = 0;
};

// --- Pin surface: always-on-top, click-through, per-pixel alpha --------------
class IPinSurface {
public:
    virtual ~IPinSurface() = default;
    virtual void setAlwaysOnTop(bool on) = 0;
    virtual void setClickThrough(bool on) = 0;     // mouse-through
    virtual void setOpacity(qreal value) = 0;      // 0.0 .. 1.0
};

// --- Autostart / elevation (General-tab "Run on system startup" + "As administrator") ---
// Windows-specific system integration: register the app to launch at sign-in and,
// optionally, elevated. Two mutually-exclusive backing forms (managed so the app
// never auto-starts twice):
//   * non-elevated  -> an HKCU ...\Run registry value (needs no elevation);
//   * elevated      -> a highest-privilege Scheduled Task (create/delete needs
//                      the *current* process to be elevated).
class IAutostart {
public:
    virtual ~IAutostart() = default;
    // Is the current process running with elevated (administrator) rights?
    virtual bool currentProcessElevated() const = 0;
    // Is any autostart entry present (either form)?
    virtual bool isEnabled() const = 0;
    // Is the present entry the elevated (Scheduled Task) form?
    virtual bool isElevated() const = 0;
    // Reconcile the OS to (enabled, elevated): removes the other form, then adds
    // the requested one. Returns false whenever the job needs elevation this
    // process does not have -- which is *three* cases, not one: creating the
    // Scheduled Task, and also **removing** it, whether because autostart is being
    // switched off or dropped back to the plain Run-key form. The caller must
    // offer a relaunch for all of them; treating only the create case as
    // recoverable is what once left users with an unremovable Scheduled Task.
    virtual bool apply(bool enabled, bool elevated) = 0;
    // Relaunch this executable elevated via a UAC prompt (ShellExecute "runas").
    // Returns true if the elevated instance was launched (caller should then quit).
    virtual bool relaunchElevated() = 0;
};

// --- Clipboard --------------------------------------------------------------
// Extra native clipboard formats requested when copying an image, beyond the
// base image that is always placed. Maps to the Output→Clipboard preferences.
struct ImageClipboardOptions {
    bool addFlatDib = false;   // also offer an alpha-flattened DIB for legacy apps (e.g. Skype)
    bool addFile    = false;   // also write a temp file and offer it as a file (CF_HDROP)
    QString fileName;          // basename for that temp file (already template-expanded; may be empty)
    int quality     = -1;      // encoder quality for the temp file (-1 = format default)
};

// Put an image on the system clipboard. The base image is always placed (so a
// default-options call behaves like Qt's QClipboard::setImage); `opt` adds the
// legacy-DIB / file formats. Concrete per-OS; the Windows backend adds the extra
// formats via Win32, other backends may honour only the base image.
class IClipboard {
public:
    virtual ~IClipboard() = default;
    virtual bool setImage(const QImage& img, const ImageClipboardOptions& opt = {}) = 0;
};

// --- OCR (text recognition) -------------------------------------------------
// One recognized word and its bounding box, in the recognized image's pixel
// coordinates. Words are grouped into reading-order lines.
struct OcrWord {
    QRect   box;    // image pixels
    QString text;
};
struct OcrLine {
    QList<OcrWord> words;   // left-to-right
};
// Result of recognizing an image: reading-order lines (top-to-bottom), each a run
// of words. `imageSize` is the size of the image that was recognized (so callers
// can map boxes back onto the source). Empty lines() means "nothing recognized".
struct OcrResult {
    QSize          imageSize;
    QList<OcrLine> lines;
};

// Optical character recognition backend. Concrete impl is Tesseract
// (platform/ocr/TesseractOcr), which is OS-neutral; `Platform::ocr` is null when
// no OCR backend was built/available (like autostart), so callers must null-check.
class IOcr {
public:
    virtual ~IOcr() = default;
    // Recognize text in `image`. `langs` is a '+'-joined Tesseract language spec
    // (e.g. "eng", "eng+chi_sim"); empty selects the backend default ("eng").
    // Word boxes are in `image` pixel coordinates. Returns an empty result on
    // failure or when the requested language data is missing.
    virtual OcrResult recognize(const QImage& image, const QString& langs) = 0;
    // Language codes whose traineddata is installed (e.g. {"eng","chi_sim"}),
    // sorted. Drives the sub-bar language dropdown and the Preferences OCR tab.
    virtual QStringList availableLanguages() = 0;
};

// --- Barcode decoding -------------------------------------------------------
// Barcode decoding backend, powering the snip code hint. Reads every symbology
// ZXing supports (`BarcodeFormat::Any` = LinearCodes | MatrixCodes, 18 in v2.2.1)
// — 2D: QR, Micro QR, rMQR, Aztec, DataMatrix, PDF417, MaxiCode; 1D: EAN-8/13,
// UPC-A/E, Code128/39/93, ITF, Codabar, DataBar (+ Expanded). Concrete impl is
// ZXing-C++ (platform/barcode/
// ZxingBarcodeDecode), which is OS-neutral; `Platform::barcode` is null when no
// backend was built (like ocr/autostart), so callers must null-check.
class IBarcodeDecode {
public:
    virtual ~IBarcodeDecode() = default;
    // Decode the first code found in `image`, returning its payload text.
    // Returns an empty string when the image holds no readable code — the
    // overwhelmingly common case, since this runs on every committed selection.
    virtual QString decodeFirst(const QImage& image) = 0;
};

// --- Snip-time keyboard filtering (Snip → Keys policies) --------------------
// A global low-level keyboard hook, installed only while a snip overlay is
// active, that enforces the Snip → Keys key policies that Qt/QKeyEvent cannot:
//   * ignoreSimCopy — QKeyEvent carries no "injected" flag, so only a
//     WH_KEYBOARD_LL hook (LLKHF_INJECTED) can tell a real Ctrl+C from one a
//     mouse driver / macro tool synthesises;
//   * ignoreWinLeft/Right — the Windows key is a system key Qt never receives,
//     so only such a hook can suppress its lone-press Start-menu trigger.
// Concrete per-OS (Windows only for now); other platforms leave keyFilter null.
class IKeyboardFilter {
public:
    virtual ~IKeyboardFilter() = default;
    struct Options {
        bool ignoreSimCopy  = false;   // swallow injected Ctrl+C / Ctrl+Alt+C
        bool ignoreWinLeft  = false;   // suppress lone Left-Win Start menu
        bool ignoreWinRight = false;   // suppress lone Right-Win Start menu
    };
    virtual void install(const Options& opt) = 0;
    virtual void uninstall() = 0;      // always safe to call, even if not installed
};

// --- Quick-snip hold-and-drag gesture ---------------------------------------
// A global WH_MOUSE_LL hook, armed only while the app is idle, that turns a
// "hold modifier(s) + drag" desktop gesture into a snip. When the left button is
// pressed with exactly `required` modifiers held and then dragged past the system
// drag threshold, onDrag(startCapturePx) fires (marshaled to the GUI thread) with
// the press point in virtual-desktop physical px (= capture px). A plain
// modifier+click that never drags is replayed to the app underneath, so the hook
// is transparent when the gesture doesn't fire. Concrete per-OS (Windows only for
// now); other platforms leave quickDrag null. See res notes in
// platform/win/WinQuickDragGesture and claude-plan/Features.md (Quick snip).
class IQuickDragGesture {
public:
    virtual ~IQuickDragGesture() = default;
    // The gesture drives four phases, all marshaled to the GUI thread, all in physical px:
    //   * onPress  — left button down with the modifiers held: build the (hidden) overlay
    //                NOW so its expensive desktop freeze lands on the press, not mid-drag;
    //   * onDrag   — dragged past the threshold: reveal the overlay, seeded at the press;
    //   * onCancel — released without dragging (it was a click): discard the built overlay
    //                (the swallowed click is replayed to the app underneath);
    //   * onRelease— released after dragging: finalize (Qt may not deliver a native release
    //                for a drag it never saw begin, so this is the authoritative one).
    struct Callbacks {
        std::function<void(QPoint startCapturePx)>   onPress;
        std::function<void(QPoint startCapturePx)>   onDrag;
        std::function<void()>                        onCancel;
        std::function<void(QPoint releaseCapturePx)> onRelease;
    };
    virtual void arm(Qt::KeyboardModifiers required, Callbacks cb) = 0;
    virtual void disarm() = 0;          // always safe to call, even if not armed
};

// --- Mouse-wheel watch (scrolling capture) ----------------------------------
// A global WH_MOUSE_LL hook that REPORTS wheel events without consuming them.
// A scrolling-capture session makes the snip views click-through, so Qt never sees
// the wheel — but the session still has to know when the user scrolled, in order to
// grab the region once the scrolling settles. Unlike IQuickDragGesture this hook
// swallows nothing: the window under the cursor must keep scrolling normally, which
// is the entire point. Concrete per-OS (Windows only for now); other platforms leave
// `wheel` null, which simply disables scrolling capture.
class IWheelWatch {
public:
    virtual ~IWheelWatch() = default;
    // Fires on the GUI thread for every wheel event anywhere on the desktop, with the
    // pointer position in virtual-desktop physical px (= capture px) so the caller can
    // ignore scrolling that happened outside its region.
    //
    // `injected` is true for events synthesised by software (SendInput) rather than a
    // physical wheel — which is how auto-scroll drives the page. Both kinds are
    // reported, deliberately: the capture pipeline wants them alike, and only the
    // "did the USER take over" question needs to tell them apart.
    //
    // `horizontalWheel` distinguishes a tilt wheel / two-finger sideways flick from an
    // ordinary one. Both axes are reported and the caller decides: a horizontal session
    // wants both (the usual way to scroll a page sideways is Shift+Wheel, which arrives
    // here as an ORDINARY vertical event), while a vertical one wants only the vertical
    // axis — a sideways scroll during it moves the content where nothing can stitch it.
    virtual void arm(std::function<void(QPoint posCapturePx, bool injected,
                                        bool horizontalWheel)> onWheel) = 0;
    virtual void disarm() = 0;          // always safe to call, even if not armed
};

// Synthesise `notches` of wheel at the current pointer position, one notch being the
// system's WHEEL_DELTA: vertical positive scrolls up, negative down; horizontal positive
// scrolls right, negative left. Windows delivers it to whatever window is under the
// POINTER, so the caller must have parked the pointer over its target first. Drives
// scrolling capture's auto-scroll — the horizontal one for a horizontal session, where
// an app that ignores WM_MOUSEHWHEEL simply does not move and auto-scroll stalls out.
void injectVerticalWheel(int notches);
void injectHorizontalWheel(int notches);

// --- Screen recording -------------------------------------------------------
// Records a screen region to a video file. The concrete backend (FFmpeg +
// DXGI/GDI, platform/win/WinRecorder) does the grabbing itself, so callers hand
// over a region and a destination and then only watch the callbacks.
//
// `Platform::recorder` is null when no backend was built (like ocr/barcode), so
// callers must null-check — that is the "recording disabled" state.
class IRecorder {
public:
    virtual ~IRecorder() = default;

    struct Request {
        QRect  regionCapturePx;    // what to record, in capture (physical) px
        QString outputPath;        // absolute; the backend creates missing directories
        recording::Config config;
    };

    // Fires on the GUI thread. `onFinished` reports the final file (ok == false
    // means it was not written — `message` says why, already translated).
    struct Callbacks {
        std::function<void(qint64 elapsedMs)> onTick;      // ~4 Hz while recording
        std::function<void(bool ok, QString path, QString message)> onFinished;
    };

    // Begins recording; returns false if it could not start at all (bad region,
    // no encoder would open, output not writable), in which case onFinished does
    // NOT fire and `error()` explains.
    virtual bool start(const Request& req, Callbacks cb) = 0;
    // Stop and finalize. Safe to call when not recording. The file is muxed and
    // closed before onFinished fires.
    virtual void stop() = 0;
    // Pause/resume. Paused time is simply absent from the file — frames keep being
    // drained (so the grabber stays alive) but none are encoded, so the video clock
    // does not advance. Safe to call in any state.
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual bool isPaused() const = 0;
    virtual bool isRecording() const = 0;

    // An image composited over every subsequent frame — this is how annotations drawn
    // during a recording get INTO the video. Region-sized, capture px,
    // ARGB32_Premultiplied; a null image clears it. Safe to call from the GUI thread
    // while recording (the encode loop reads it under a lock).
    virtual void setOverlay(const QImage& overlay) = 0;
    // Human-readable reason the last start()/recording failed (already translated).
    virtual QString error() const = 0;
};

// --- Playback (the post-recording preview) ----------------------------------
// A speaker sink the player pushes decoded audio into. Abstract so the decoder
// itself stays OS-neutral (platform/media/) while the actual endpoint is Win32.
class IAudioSink {
public:
    virtual ~IAudioSink() = default;
    virtual bool start(int sampleRate, int channels) = 0;
    virtual void stop() = 0;
    // Write interleaved float frames; returns how many were accepted (short writes
    // are normal — the caller retries rather than blocking the decode loop).
    virtual int write(const float* interleaved, int frames) = 0;
    virtual int space() const = 0;             // room available, in frames
    // Frames actually played out. This is the master clock: video is presented
    // against it, because ears notice a stutter that eyes forgive.
    virtual qint64 playedFrames() const = 0;
    virtual void reset() = 0;                  // drop buffered audio (used on seek)
};
std::unique_ptr<IAudioSink> createAudioSink();

// Plays a recorded file back for review. Decoding runs on its own thread; frames
// arrive on the GUI thread via the callback.
class IMediaPlayer {
public:
    virtual ~IMediaPlayer() = default;

    struct Callbacks {
        // A frame to show, with the position it represents.
        std::function<void(QImage frame, qint64 posMs)> onFrame;
        std::function<void()> onEnded;
    };

    virtual bool open(const QString& path, Callbacks cb) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual bool isPlaying() const = 0;
    virtual void seek(qint64 ms) = 0;
    virtual qint64 durationMs() const = 0;
    virtual qint64 positionMs() const = 0;
    virtual bool hasAudio() const = 0;
    virtual QSize videoSize() const = 0;
    virtual QString error() const = 0;
};
std::unique_ptr<IMediaPlayer> createMediaPlayer();

// Copy [startMs, endMs) of `src` into `dst` WITHOUT re-encoding. Instant even on
// long files, at the cost of cutting only on keyframes — so the real start can sit
// up to one GOP earlier than asked (the dialog says so rather than pretending).
// `src` is never modified. False on failure, with a translated reason in `error`.
bool trimRecording(const QString& src, const QString& dst,
                   qint64 startMs, qint64 endMs, QString* error);

// Which encoder a recording would ACTUALLY use for `codec` under `hw` on this
// machine, found by trying to open each candidate in turn — presence in the build
// does not mean it opens (hevc_mf is the standing example). Empty when none opens or
// no recording backend was built. Preferences → Recording shows it, so "hardware or
// software?" stops being a black box. Implemented per-OS; a stub returns empty.
QString activeVideoEncoder(recording::Codec codec, recording::HwPreference hw);

// --- Tray ------------------------------------------------------------------
class ITray {
public:
    virtual ~ITray() = default;
    virtual void show() = 0;
    virtual void hide() = 0;
    // Mouse actions are routed to handlers (left/middle/right).
    virtual void setOnActivate(std::function<void(/*button*/ int)> handler) = 0;
};

// --- Factory ----------------------------------------------------------------
// Aggregate of the platform services for the current OS.
struct Platform {
    std::unique_ptr<IScreenCapture> capture;
    std::unique_ptr<IGlobalHotkey>  hotkeys;
    std::unique_ptr<IElementDetect> detect;
    std::unique_ptr<IAutostart>     autostart;   // null on platforms without a backend
    std::unique_ptr<IClipboard>     clipboard;
    std::unique_ptr<IKeyboardFilter> keyFilter;  // null on platforms without a backend
    std::unique_ptr<IQuickDragGesture> quickDrag; // null on platforms without a backend
    std::unique_ptr<IWheelWatch>    wheel;       // null on platforms without a backend
    std::unique_ptr<IOcr>           ocr;         // null when no OCR backend is available
    std::unique_ptr<IBarcodeDecode>      barcode;          // null when no barcode backend is available
    std::unique_ptr<IRecorder>      recorder;    // null when no FFmpeg backend is available
    // IPinSurface is created per-window; ITray is created by the app shell.
};

// Implemented per-OS in platform/<os>/. Builds the concrete backend.
Platform createPlatform();

// Create a pin surface bound to a native window handle (per pin window). Returns
// nullptr if the platform has no implementation. Implemented in platform/<os>/.
std::unique_ptr<IPinSurface> createPinSurface(WId window);

} // namespace platform
