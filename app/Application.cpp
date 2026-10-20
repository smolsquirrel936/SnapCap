#include "app/Application.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QApplication>
#include <QFont>
#include <QColor>
#include <QClipboard>
#include <QMimeData>
#include <QCursor>
#include <QScreen>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <QImage>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QTimer>
#include <QMessageBox>
#include <QStyleHints>
#include <memory>

#include "core/Logging.h"
#include "core/i18n/I18n.h"
#include "core/action/KeySequenceCodec.h"
#include "core/PinModel.h"
#include "core/annotation/AnnotationDocument.h"
#include "core/render/Raster.h"
#include "core/History.h"
#include "core/SessionStore.h"
#include "ui/TrayIcon.h"
#include "ui/SnipOverlay.h"
#include "ui/PinWindow.h"
#include "ui/PreferencesDialog.h"
#include "ui/GroupManagerDialog.h"

namespace {
// Global hotkeys wired in Stage 1, with their settings-override keys.
struct GlobalHotkey { const char* name; ActionId id; };
const GlobalHotkey kGlobalHotkeys[] = {
    {"snip",         ActionId::Snip},
    {"snipCopy",     ActionId::SnipCopy},
    {"customSnip",   ActionId::CustomSnip},
    {"snipActiveWindow", ActionId::SnipActiveWindow},
    // quickSnip is NOT here: its trigger lives in snip/quickSnip/trigger and is
    // applied by applyQuickSnipTrigger() (a full hotkey OR a hold-drag gesture).
    {"paste",        ActionId::Paste},
    {"hideAll",      ActionId::HideAllPins},
    {"closeAll",     ActionId::CloseAllPins},
    {"destroyAll",   ActionId::DestroyAllPins},
    {"clickThrough", ActionId::ToggleClickThrough},
    {"pickColor",    ActionId::PickColor},
    {"scanBarcode",  ActionId::ScanBarcode},
};
constexpr auto kInstanceKey = "SnapCapInstance";

// SnapCap's baseline UI point size, applied on top of the platform default family
// whenever interface/font is unset. Qt's Windows default is 9; we ship one notch up.
constexpr int kDefaultUiPointSize = 10;

// Snapshot a live pin window for the session/group stores (pixels + transform +
// annotations + frame). The image is the CLEAN source: annotations stay geometry, so a
// restored pin can still undo/edit them rather than getting them baked into its pixels.
PinSnapshot snapshotFor(PinWindow* w)
{
    PinSnapshot s;
    s.image     = w->model()->source();
    s.transform = w->model()->toJson();
    s.geometry  = w->frameGeometry();
    // toJson() always yields {"items": []}, which SessionStore's is-empty guard would
    // happily write out — so only hand it over when there is something to store.
    if (!w->model()->annotations()->isEmpty())
        s.annotations = w->model()->annotations()->toJson();
    return s;
}

PinWindow* windowForModel(PinModel* m)
{
    for (PinWindow* w : PinWindow::windows())
        if (w->model() == m)
            return w;
    return nullptr;
}

// Re-attach a restored pin's animation. SessionStore keeps only a still sidecar PNG, but the
// path the pixels came from is already part of PinModel::toJson() — so an animated pin plays
// again after a restart. Silent by design (no warn parent): a file that moved, or one too big
// to hold, just leaves the restored still in place instead of popping a dialog at startup.
void reattachAnimation(PinModel* m)
{
    if (m && !m->sourcePath().isEmpty())
        PinWindow::attachAnimation(m, m->sourcePath(), nullptr);
}
} // namespace

Application::Application(QObject* parent)
    : QObject(parent)
{
    // Logging — file resolves from log/file (next to config.ini unless absolute).
    const QString cfg = m_settings.filePath();
    const QString logPath = m_settings.logFilePath();
    Logging::init(logPath, m_settings.value(QStringLiteral("log/rotateSizeMb"), 2.0).toDouble());
    Logging::setLevel(Logging::levelFromString(
        m_settings.value(QStringLiteral("log/level"), QStringLiteral("normal")).toString()));
    qInfo() << "[app] SnapCap starting; config =" << cfg;

    // Locale from settings (falls back to en if unknown).
    I18n::instance().setLocale(m_settings.value(QStringLiteral("general/locale"), QStringLiteral("en")).toString());

    // Interface font — build the baseline first (the platform family at our own point
    // size, so an empty setting restores it), then apply the saved override and react
    // to live changes.
    m_defaultUiFont = QApplication::font();
    m_defaultUiFont.setPointSize(kDefaultUiPointSize);
    applyInterfaceFont();
    applyGpuAcceleration();
    applyColorScheme();
    connect(&m_settings, &Settings::changed, this, [this](const QString& key) {
        if (key == QStringLiteral("interface/font"))
            applyInterfaceFont();
        else if (key == QStringLiteral("general/gpu_acceleration"))
            applyGpuAcceleration();
        else if (key == QStringLiteral("interface/theme") || key.isEmpty())
            applyColorScheme();
    });

    // Platform backend (global hotkeys for now).
    m_platform = platform::createPlatform();
    PinWindow::setClipboard(m_platform.clipboard.get());   // Output→Clipboard extra formats for pins
    PinWindow::setOcr(m_platform.ocr.get());               // OCR backend for the pin toolbar (null → no-op)

    // Reconcile OS autostart with the saved preference. Best-effort: when the
    // elevated (Scheduled Task) form is requested but we are not elevated, this
    // no-ops until a later elevated run (or the Preferences relaunch) applies it.
    // The `|| currentProcessElevated()` half is load-bearing for *removals*:
    // Preferences relaunches elevated to delete a Scheduled Task, and this is
    // where that deletion actually happens. Without it the reconcile would skip
    // (runOnStartup now being false) and the task would survive the relaunch,
    // which is exactly the trap recorded as G2. Guarded rather than made
    // unconditional because apply() shells out to schtasks: the common case --
    // autostart off, not elevated -- must stay free at startup.
    if (m_platform.autostart) {
        const bool want = m_settings.value(QStringLiteral("general/runOnStartup"), false).toBool();
        const bool elev = m_settings.value(QStringLiteral("general/startupElevated"), false).toBool();
        if (want || m_platform.autostart->currentProcessElevated())
            m_platform.autostart->apply(want, elev);
    }

    loadKeymap();
    setupHistory();
    wireActions();
    wirePins();
    wireGroups();

    m_tray = new TrayIcon(&m_actions, &m_settings, &m_groups, this);
    connect(m_tray, &TrayIcon::manageGroupsRequested, this, &Application::showGroupManager);
    if (!m_settings.value(QStringLiteral("general/hideTray"), false).toBool())
        m_tray->show();

    registerGlobalHotkeys();
    applyQuickSnipTrigger();          // full-hotkey OR hold-drag gesture (snip/quickSnip/trigger)
    startSingleInstanceServer(QString::fromLatin1(kInstanceKey));

    // Restore pins from the last session, and persist the open set on exit.
    if (m_settings.value(QStringLiteral("general/restoreSession"), true).toBool())
        restoreSession();
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { saveSession(); });
}

Application::~Application()
{
    delete m_prefs;
    delete m_groupManager;
}

// `general/gpu_acceleration` = "auto" (default) | "cpu". This is the ONLY consumer
// of that key, and it reaches only core/render's *blur* dispatch: mosaic is CPU-only
// by measurement, and the pin drop shadow never went through core/render at all.
// "auto" is safe to default because the GPU path probes for capability and silently
// falls back to the CPU box blur on any failure.
void Application::applyGpuAcceleration()
{
    const QString mode = m_settings.value(QStringLiteral("general/gpu_acceleration"),
                                          QStringLiteral("auto")).toString();
    const bool useGpu = (mode.compare(QLatin1String("cpu"), Qt::CaseInsensitive) != 0);
    render::setBlurMode(useGpu ? render::BlurMode::Auto : render::BlurMode::Cpu);
    qInfo("[app] blur backend: %s", useGpu ? "auto (GPU when available)" : "cpu");
}

// `interface/theme` = "auto" (default) | "light" | "dark". Setting Qt's colour scheme
// is what carries the theme beyond the QSS-styled dialogs: it repaints every window
// that uses the standard palette (ColorDialog, the group manager, the recording
// preview, message boxes, menus) and darkens the native title bars too. "auto" hands
// the decision back to Qt, which tracks the OS and re-emits colorSchemeChanged when
// the user flips Windows between light and dark.
void Application::applyColorScheme()
{
    const QString mode = m_settings.value(QStringLiteral("interface/theme"),
                                          QStringLiteral("auto")).toString();
    Qt::ColorScheme scheme = Qt::ColorScheme::Unknown;         // Unknown = follow the OS
    if (mode.compare(QLatin1String("light"), Qt::CaseInsensitive) == 0)
        scheme = Qt::ColorScheme::Light;
    else if (mode.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0)
        scheme = Qt::ColorScheme::Dark;

    QApplication::styleHints()->setColorScheme(scheme);
    qInfo("[app] color scheme: %s", qPrintable(mode));
}

void Application::applyInterfaceFont()
{
    const QString s = m_settings.value(QStringLiteral("interface/font")).toString();
    QFont f = m_defaultUiFont;
    if (!s.isEmpty())
        f.fromString(s);       // no-op on malformed strings, keeping the baseline

    // Always push the resolved font. This used to skip the call when the font matched
    // the platform default, so that an unset setting left Qt's per-widget-class system
    // fonts alone; that can't apply now, because the baseline deliberately differs from
    // the platform default (kDefaultUiPointSize) and only setFont() can deliver it.
    QApplication::setFont(f);
}

void Application::loadKeymap()
{
    const QString km = QDir(QCoreApplication::applicationDirPath())
                           .filePath(QStringLiteral("res/keymap.default.yaml"));
    if (!m_actions.loadDefaults(km))
        qWarning() << "[app] no default keymap loaded from" << km;

    // Apply user overrides from config.ini (hotkey/<action>) on top of defaults.
    for (const auto& g : kGlobalHotkeys) {
        const QString key = QStringLiteral("hotkey/") + QLatin1String(g.name);
        const QVariant ov = m_settings.value(key);
        if (ov.isValid())
            m_actions.bindKey(g.id, ov.toString(), KeyContext::Global);
    }
}

void Application::wireActions()
{
    // Snip opens the capture overlay (Stage 2). Paste is still a stub (Stage 5).
    m_actions.registerAction(ActionId::Snip, [this] {
        if (auto* overlay = beginSnip())
            overlay->start();
    });
    m_actions.registerAction(ActionId::SnipCopy, [this] {
        if (auto* overlay = beginSnip()) {
            overlay->setOneShotCopy(true);   // first confirmed region → clipboard, then close
            overlay->start();
        }
    });
    m_actions.registerAction(ActionId::CustomSnip, [this] {
        if (m_activeSnip) {
            qInfo() << "[action] CustomSnip ignored: capture already in progress";
            return;
        }
        // The delay is there to let the user set the screen up (open a menu, hover a
        // tooltip), so it must elapse BEFORE the screens are frozen — i.e. before the
        // overlay is even built.
        const int delayMs = qRound(m_settings.value(QStringLiteral("snip/customSnip/delay"),
                                                    0.0).toDouble() * 1000.0);
        if (delayMs > 0)
            QTimer::singleShot(delayMs, this, [this] { startCustomSnip(); });
        else
            startCustomSnip();
    });
    m_actions.registerAction(ActionId::SnipActiveWindow, [this] {
        // Resolve the frame BEFORE building the overlay — once it is up, the
        // foreground window is ours. Null means nothing snippable is on screen
        // (bare desktop, everything minimised), so there is no sane region to
        // preselect and we do nothing rather than opening an empty snip.
        const QRect frame = platform::foregroundWindowRect();
        if (frame.isEmpty()) {
            qInfo() << "[action] SnipActiveWindow ignored: no foreground window";
            return;
        }
        if (auto* overlay = beginSnip()) {
            overlay->setPresetArea(frame);   // capture px, like a custom snip
            overlay->start();
        }
    });
    m_actions.registerAction(ActionId::Paste, [this] {
        if (m_activeSnip) return;     // a capture owns the screen — ignore unrelated hotkeys
        doPaste();
    });
    m_actions.registerAction(ActionId::HideAllPins, [this] {
        if (m_activeSnip) return;
        m_pins.setAllHidden(!m_pins.allHidden());
    });
    m_actions.registerAction(ActionId::CloseAllPins,   [this] {
        if (!m_activeSnip) closeAllPins(/*destroy=*/false);
    });
    m_actions.registerAction(ActionId::DestroyAllPins, [this] {
        if (!m_activeSnip) closeAllPins(/*destroy=*/true);
    });
    m_actions.registerAction(ActionId::ToggleClickThrough, [this] {
        if (m_activeSnip) return;
        // Toggle the pin under the cursor; if none, make all click-through pins clickable again.
        const QPoint c = QCursor::pos();
        for (PinWindow* w : PinWindow::windows())
            if (w->isVisible() && w->frameGeometry().contains(c)) { w->toggleClickThrough(); return; }
        for (PinWindow* w : PinWindow::windows())
            if (w->clickThrough()) w->setClickThrough(false);
    });
    m_actions.registerAction(ActionId::PickColor, [this] {
        // Standalone colour picker: open the snip overlay in colour-pick-only mode —
        // no region drag, detection off; the loupe follows the cursor and a click / C
        // copies the pixel colour and closes.
        if (auto* overlay = beginSnip()) {
            overlay->setColorPickOnly(true);
            overlay->setDetectMode(SnipOverlay::DetectMode::Off);
            overlay->start();
        }
    });
    m_actions.registerAction(ActionId::ScanBarcode, [this] {
        // Barcode scanner: a normal (dimmed) snip whose first confirmed region is decoded
        // for a barcode/QR — the payload is copied and the overlay closes, or a toast
        // reports that nothing was found. beginSnip() wires the barcode + notify backends.
        if (auto* overlay = beginSnip()) {
            overlay->setBarcodeScanOnly(true);
            overlay->start();
        }
    });
    m_actions.registerAction(ActionId::OpenPreferences, [this] { showPreferences(); });
    m_actions.registerAction(ActionId::Quit,         [] { QCoreApplication::quit(); });
}

SnipOverlay* Application::beginSnip()
{
    if (m_activeSnip) {            // already capturing — ignore re-entry (F1, tray, …)
        qInfo() << "[action] Snip ignored: capture already in progress";
        return nullptr;
    }
    qInfo() << "[action] Snip requested";
    // Capture the foreground window title now, before the overlay steals focus,
    // so the %title% filename token can expand to it.
    const QString fgTitle = platform::foregroundWindowTitle();
    auto* overlay = new SnipOverlay(m_platform.detect.get(),
                                    m_platform.capture.get());  // self-deletes on finish
    overlay->setSettings(&m_settings);                         // for the PaintBar colour picker
    overlay->setActions(&m_actions);                           // key-tips panel labels (keymap)
    overlay->setClipboard(m_platform.clipboard.get());         // Output→Clipboard extra formats
    overlay->setKeyFilter(m_platform.keyFilter.get());         // snip/keys/* LL-hook policies
    overlay->setOcr(m_platform.ocr.get());                     // OCR backend (null → button no-ops)
    overlay->setBarcodeDecode(m_platform.barcode.get());                 // barcode backend (null → no code hint)
    overlay->setRecorder(m_platform.recorder.get());            // recorder (null → record button disabled)
    overlay->setWheelWatch(m_platform.wheel.get());             // wheel hook (null → scroll button refuses)
    overlay->setWindowTitle(fgTitle);                          // %title% token source
    overlay->setNotifySink([this](const QString& title, const QString& body) {
        if (m_tray) m_tray->notify(title, body);               // quick-save toast
    });
    m_activeSnip = overlay;
    if (!m_inGestureSnip && m_platform.quickDrag)
        m_platform.quickDrag->disarm();      // no hold-drag gesture while a NON-gesture capture owns
                                             // the screen; the gesture's own snip keeps it armed so
                                             // the hook can still report the release
    connect(overlay, &QObject::destroyed, this, [this] {
        m_activeSnip = nullptr;
        applyQuickSnipTrigger();             // re-arm the gesture (if that's the configured trigger)
    });
    const QString mode = m_settings.value(QStringLiteral("snip/detectMode"),
                                          QStringLiteral("element")).toString();
    overlay->setDetectMode(mode == QStringLiteral("off")    ? SnipOverlay::DetectMode::Off
                         : mode == QStringLiteral("window") ? SnipOverlay::DetectMode::Window
                                                            : SnipOverlay::DetectMode::Element);
    // Magnifier readout config (formats / HEX case / no-#) is read by the
    // overlay itself from interface/magnifier/* in start().
    overlay->setPinSink([this](const QImage& img, const QRect& contentRect) {     // Ctrl+T / middle-click
        m_nextContentTopLeft = contentRect.topLeft();   // global-logical: where it was captured
        m_haveNextContent = true;
        // img carries the capture DPR (tagged in onPin) → fixes the pin's logical
        // size on a mixed-DPI multi-monitor layout.
        m_pins.addImage(img, QString(), img.devicePixelRatio());  // → onPinAdded() (synchronous)
        m_haveNextContent = false;
    });
    overlay->setHistorySink([this](const QImage& img, const QList<ScreenShot>& shots,
                                   const QRect& sel) {                          // crop + full freeze
        if (m_history) m_history->record(img, shots, sel);
    });
    overlay->setHistoryThumbs([this] {                                          // `,`/`.` strip
        QVector<QImage> thumbs;
        if (m_history)
            for (const History::Entry& e : m_history->entries())
                thumbs.append(e.thumbnail);
        return thumbs;
    });
    overlay->setHistoryPick([this](int i) { return m_history ? m_history->fullAt(i) : QImage(); });
    overlay->setHistoryRestore([this](int i) {                                  // `,`/`.` full-screen replay
        SnipOverlay::HistoryFrame f;
        if (m_history) {
            f.shots = m_history->screensAt(i);
            f.selection = m_history->selectionAt(i);
        }
        return f;
    });
    overlay->setAreaSink([this](const QRect& r) { m_areaHistory.record(r); });
    overlay->setAreaNav([this](int dir) { return m_areaHistory.step(dir); });   // `R`/`Shift+R` reuse
    m_areaHistory.resetCursor();          // fresh snip → first `R` recalls the newest area
    return overlay;
}

void Application::startCustomSnip()
{
    auto* overlay = beginSnip();
    if (!overlay)
        return;
    const QRect area(m_settings.value(QStringLiteral("snip/customSnip/x"), 0).toInt(),
                     m_settings.value(QStringLiteral("snip/customSnip/y"), 0).toInt(),
                     m_settings.value(QStringLiteral("snip/customSnip/width"), 800).toInt(),
                     m_settings.value(QStringLiteral("snip/customSnip/height"), 600).toInt());
    overlay->setPresetArea(area);   // capture px; ignored when empty/invalid
    overlay->start();
}

clip::PasteOptions Application::pasteOptions() const
{
    // Preferences → Paste → Conversion. Defaults mirror PreferencesDialog's own, so an
    // untouched config pastes what the dialog advertises (not PasteOptions' struct defaults).
    clip::PasteOptions opt;
    opt.textToImage =
        m_settings.value(QStringLiteral("paste/conversion/textToImage"), true).toBool();
    opt.ignoreHtml =
        m_settings.value(QStringLiteral("paste/conversion/ignoreHtml"), false).toBool();
    opt.pasteFilePaths =
        !m_settings.value(QStringLiteral("paste/conversion/neverPasteFilePaths"), true).toBool();
    opt.pageMargin = m_settings.value(QStringLiteral("paste/conversion/pageMargin"), 15).toInt();
    opt.maxWidth   = m_settings.value(QStringLiteral("paste/conversion/maxWidth"), 900).toInt();

    QFont f(QStringLiteral("Segoe UI"), 11);
    const QString fs = m_settings.value(QStringLiteral("paste/conversion/font")).toString();
    if (!fs.isEmpty()) f.fromString(fs);
    opt.textFont = f;

    const QColor bg(m_settings.value(QStringLiteral("paste/conversion/bgColor")).toString());
    const QColor fg(m_settings.value(QStringLiteral("paste/conversion/textColor")).toString());
    opt.bgColor   = bg.isValid() ? bg : QColor(Qt::white);
    opt.textColor = fg.isValid() ? fg : QColor(Qt::black);

    opt.uppercaseHex = m_settings.value(QStringLiteral("misc/uppercase_hex_color"), false).toBool();
    return opt;
}

void Application::wirePins()
{
    // Magnetic-snap threshold uses PinManager's built-in default. The recover-ring size is
    // driven by Paste → Behavior → "Max recoverable closed images" (live on change).
    m_pins.setMaxRecoverable(
        m_settings.value(QStringLiteral("paste/behavior/maxRecoverable"), 10).toInt());
    PinWindow::setPasteOptions(pasteOptions());
    connect(&m_settings, &Settings::changed, this, [this](const QString& key) {
        if (key == QStringLiteral("paste/behavior/maxRecoverable"))
            m_pins.setMaxRecoverable(m_settings.value(key, 10).toInt());
        // Cheap enough to just re-read: this can never go stale against the F3 path,
        // which builds its options fresh on every paste.
        PinWindow::setPasteOptions(pasteOptions());
    });
    // Track clipboard changes so a freshly-copied item pastes ahead of the recover ring once.
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            [this] { m_clipboardChanged = true; });
    connect(&m_pins, &PinManager::added, this, &Application::onPinAdded);
    connect(&m_pins, &PinManager::removed, this, [this](PinModel* model) {
        // Closing/destroying a pin re-baselines the clipboard as "stale": only a copy made
        // *after* the close jumps ahead of recovery, so F3 recovers the just-closed pins
        // first (see doPaste()).
        m_clipboardChanged = false;
        // detach() first: destroy() frees the model the moment this returns, but the window
        // only deleteLater()s, so anything still pointing at the model from the window (the
        // animation clock above all) would dereference freed memory on the next turn.
        for (PinWindow* w : PinWindow::windows())
            if (w->model() == model) { w->detach(); w->hide(); w->deleteLater(); break; }
    });
}

void Application::wireGroups()
{
    // The registry is the source of truth; the app reacts to switch/delete by moving or
    // discarding the actual pin windows. Load the group definitions here (always — they're
    // a durable user asset, independent of the session-restore preference). fromJson()
    // emits nothing, so no windows move at startup.
    // Showing/hiding a group is exactly load-from-disk / serialize-back-to-disk, one group
    // at a time and independent of the others — so the registry's signals drive them directly.
    connect(&m_groups, &GroupRegistry::shown, this, &Application::loadGroupIntoLive);
    connect(&m_groups, &GroupRegistry::hidden, this, &Application::putAwayGroup);
    connect(&m_groups, &GroupRegistry::removed, this, &Application::onGroupRemoved);
    // Keep the open manager's pin counts live: any pin appearing/disappearing (assign to a
    // hidden group, switch/show/hide a group, close or destroy a grouped pin) changes a
    // group's count. Queued so the refresh runs *after* close()/destroy() finish erasing the
    // model from m_active (removed() is emitted before the erase — see PinManager).
    connect(&m_pins, &PinManager::added, this, &Application::refreshGroupManager,
            Qt::QueuedConnection);
    connect(&m_pins, &PinManager::removed, this, &Application::refreshGroupManager,
            Qt::QueuedConnection);
    loadGroupRegistry();
}

void Application::onPinAdded(PinModel* model)
{
    auto* w = new PinWindow(model, &m_pins, &m_settings, &m_groups);
    connect(w, &PinWindow::preferencesRequested, this,
            [this] { m_actions.dispatch(ActionId::OpenPreferences); });
    connect(w, &PinWindow::assignToGroupRequested, this, &Application::assignPinToGroup);

    // A pin restored from the last session keeps its saved top-left (and saved opacity);
    // a snip pinned via Ctrl+T/middle-click lands on its capture spot; a clipboard paste
    // is centered under the cursor, clamped to that screen.
    if (m_haveNextGeometry) {
        w->showAt(m_nextGeometry.topLeft(), /*animate=*/false);   // restored pins don't flash
        return;
    }
    // A freshly created pin starts at the configured default opacity (restored pins above
    // keep their own). Set it before show() so the first paint uses it.
    const int op = m_settings.value(QStringLiteral("paste/display/defaultOpacity"), 100).toInt();
    model->setOpacity(qBound(10, op, 100) / 100.0);
    // Paste → Behavior → "Use snipping position": when on, a pinned snip lands on its capture
    // spot; when off, it falls through to the cursor-centred placement below.
    if (m_haveNextContent
        && m_settings.value(QStringLiteral("paste/behavior/useSnippingPosition"), true).toBool()) {
        w->showWithContentAt(m_nextContentTopLeft);
        return;
    }
    const QPoint cursor = QCursor::pos();
    w->showAt(cursor);
    QPoint tl = cursor - QPoint(w->width() / 2, w->height() / 2);
    if (QScreen* s = QGuiApplication::screenAt(cursor)) {
        const QRect a = s->availableGeometry();
        tl.setX(qBound(a.left(), tl.x(), a.right() - w->width()));
        tl.setY(qBound(a.top(), tl.y(), a.bottom() - w->height()));
    }
    w->move(tl);
}

void Application::doPaste()
{
    // F3 priority (Features.md §3):
    //   1. A *freshly-copied* clipboard item pastes first — but only once (so a new copy
    //      made mid-recovery jumps ahead of the ring). Content already on the clipboard
    //      before the last pin was closed is "stale" and does not (close re-baselines the
    //      flag — see the PinManager::removed handler in wirePins()).
    //   2. Otherwise recover closed pins oldest-first until the recover ring is empty (so
    //      closing a pin — or Close All Pins — is undone by pressing Paste).
    //   3. Once the ring is empty, paste the current clipboard content repeatedly.
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    const clip::Content c = md ? clip::classify(*md, pasteOptions()) : clip::Content{};

    if (m_clipboardChanged && c.isValid()) {          // (1) fresh copy — paste ahead of the ring
        m_clipboardChanged = false;
        pinClipboardImage(c);
        return;
    }
    if (PinModel* recovered = m_pins.recoverOldest()) {   // (2) drain the recover ring, oldest first
        // A recovered pin re-enters as ungrouped: it's a floating pin again, not part of any
        // group's live set (it was closed out of one, and paste-recovery isn't a group action).
        recovered->setGroup(0);
        qInfo() << "[action] recovered oldest closed pin";
        return;
    }
    if (c.isValid()) {                                // (3) ring empty — paste the clipboard
        m_clipboardChanged = false;
        pinClipboardImage(c);
        return;
    }
    qInfo() << "[action] Paste: nothing to pin or recover";
}

void Application::pinClipboardImage(const clip::Content& c)
{
    PinModel* model = m_pins.addImage(c.image, c.text);   // → onPinAdded(): the window exists now

    // Pasting a single copied image *file* keeps the path, so an animated one (GIF & friends)
    // plays rather than pinning as its first frame. Only a file path can carry frames: a
    // bitmap copied from another app reaches the clipboard already flattened.
    if (c.kind != clip::Kind::Image || c.files.size() != 1)
        return;
    PinWindow* win = windowForModel(model);
    model->setSourcePath(c.files.first());
    if (PinWindow::attachAnimation(model, c.files.first(), win) && win)
        win->refreshComposed();                          // starts the animation clock
}

void Application::closeAllPins(bool destroy)
{
    const QVector<PinModel*> pins = m_pins.activePins();
    if (pins.isEmpty())
        return;

    // Paste → Behavior → "Ask before closing/destroying multiple images": only prompt when
    // more than one pin is affected (closing a lone pin isn't "multiple").
    auto& tr = I18n::instance();
    const QString confirmKey = destroy
        ? QStringLiteral("paste/behavior/confirmDestroyMultiple")
        : QStringLiteral("paste/behavior/confirmCloseMultiple");
    if (pins.size() > 1 && m_settings.value(confirmKey, true).toBool()) {
        const QString stem = destroy ? QStringLiteral("paste.confirm.destroy_multiple")
                                     : QStringLiteral("paste.confirm.close_multiple");
        QMessageBox box(QMessageBox::Question, tr.t(stem + QStringLiteral(".title")),
                        tr.t(stem + QStringLiteral(".text"),
                             {{QStringLiteral("count"), pins.size()}}),
                        QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::No);
        if (box.exec() != QMessageBox::Yes)
            return;
    }

    for (PinModel* m : pins) {
        if (destroy) m_pins.destroy(m);
        else         m_pins.close(m);
    }
}

void Application::registerGlobalHotkeys()
{
    if (!m_platform.hotkeys)
        return;
    for (const auto& g : kGlobalHotkeys) {
        const QString seq = m_actions.keyFor(g.id);
        if (seq.isEmpty())
            continue;                                   // intentionally unbound
        const int id = m_platform.hotkeys->registerHotkey(seq, [this, a = g.id] { m_actions.dispatch(a); });
        if (id < 0)
            qWarning().noquote()
                << I18n::instance().t(QStringLiteral("error.hotkey_taken"), {{QStringLiteral("seq"), seq}});
        else
            qInfo().noquote() << "[app] registered hotkey" << seq << "->" << g.name;
    }
    // Apps whose foreground state should suppress our global hotkeys (so the key
    // reaches them). Re-applied here after every remap / Preferences close.
    m_platform.hotkeys->setIgnoredApps(
        m_settings.value(QStringLiteral("control/ignoreApps")).toStringList());
}

void Application::applyQuickSnipTrigger()
{
    // Quick snip is a hold-modifier + drag gesture. Its trigger is a modifier-only combo
    // (e.g. "Ctrl+Alt"); any stray key in the stored value is ignored (never a hotkey).
    // Idempotent — called at startup, after Preferences closes, and when a snip ends.
    if (!m_platform.quickDrag)
        return;                                     // no gesture backend (non-Windows)
    m_platform.quickDrag->disarm();

    const QString trigger = m_settings.value(QStringLiteral("snip/quickSnip/trigger")).toString();
    const bool copy = m_settings.value(QStringLiteral("snip/quickSnip/copy"), true).toBool();
    const bool pin  = m_settings.value(QStringLiteral("snip/quickSnip/pin"),  false).toBool();
    const Qt::KeyboardModifiers mods = KeySequenceCodec::modsFromString(trigger);
    if (mods == Qt::NoModifier || (!copy && !pin))
        return;                                     // disabled: no modifiers or no action

    platform::IQuickDragGesture::Callbacks cb;
    cb.onPress = [this](QPoint /*startCapturePx*/) {   // build the overlay NOW (freeze on press)
        if (m_activeSnip)
            return;                                    // a capture already owns the screen
        const bool c = m_settings.value(QStringLiteral("snip/quickSnip/copy"), true).toBool();
        const bool p = m_settings.value(QStringLiteral("snip/quickSnip/pin"),  false).toBool();
        if (!c && !p)
            return;
        m_inGestureSnip = true;                        // keep the gesture hook armed through this snip
        SnipOverlay* overlay = beginSnip();            // constructs + freezes, but does NOT show yet
        m_inGestureSnip = false;
        if (overlay)
            overlay->setQuickAction(c, p);
    };
    cb.onDrag = [this](QPoint startCapturePx) {        // dragged past threshold → reveal, seeded
        if (m_activeSnip)
            m_activeSnip->startQuickDrag(startCapturePx);
    };
    cb.onCancel = [this] {                             // released without dragging → discard
        if (m_activeSnip)
            m_activeSnip->cancelQuickDrag();           // never shown; safe teardown + re-arm on destroy
    };
    cb.onRelease = [this](QPoint releaseCapturePx) {   // released after dragging → finalize copy/pin
        if (m_activeSnip)
            m_activeSnip->finishQuickDrag(releaseCapturePx);
    };
    m_platform.quickDrag->arm(mods, std::move(cb));
}

void Application::showPreferences()
{
    if (!m_prefs) {
        m_prefs = new PreferencesDialog(&m_settings, &m_actions, m_platform.hotkeys.get(),
                                        m_platform.autostart.get());
        // While the dialog is open the app's own hotkeys are released so the
        // Control page can probe each combo against *other* apps only (a live
        // registration of our own would otherwise read as "taken"). We re-apply
        // the (possibly edited) bindings once the dialog closes.
        connect(m_prefs, &QDialog::finished, this, [this] {
            if (m_platform.hotkeys) {
                m_platform.hotkeys->unregisterAll();
                registerGlobalHotkeys();
            }
            applyQuickSnipTrigger();               // re-arm the (possibly edited) Quick snip gesture
        });
        connect(m_prefs, &PreferencesDialog::clearHistoryRequested, this, [this] {
            if (m_history) m_history->clear();
        });
    }
    if (m_platform.hotkeys)
        m_platform.hotkeys->unregisterAll();   // free combos for probing while open
    m_prefs->show();
    m_prefs->raise();
    m_prefs->activateWindow();
}

QString Application::configDir() const
{
    return QFileInfo(m_settings.filePath()).absolutePath();
}

void Application::setupHistory()
{
    QString dir = m_settings.value(QStringLiteral("general/historyDir")).toString();
    if (dir.isEmpty())
        dir = QDir(configDir()).filePath(QStringLiteral("history"));
    const int max = m_settings.value(QStringLiteral("snip/behavior/maxHistoryRecords"), 20).toInt();
    m_history = new History(dir, max, this);
    applyAreaHistorySettings();

    // React to live preference changes (record-ring size + reuse-area max/loop).
    connect(&m_settings, &Settings::changed, this, [this](const QString& key) {
        if (key == QStringLiteral("snip/behavior/maxHistoryRecords") && m_history)
            m_history->setMax(m_settings.value(key, 20).toInt());
        else if (key == QStringLiteral("snip/behavior/maxHistoryAreas")
              || key == QStringLiteral("snip/behavior/historyLoop"))
            applyAreaHistorySettings();
    });
}

void Application::applyAreaHistorySettings()
{
    m_areaHistory.setMax(m_settings.value(QStringLiteral("snip/behavior/maxHistoryAreas"), 8).toInt());
    m_areaHistory.setLoop(m_settings.value(QStringLiteral("snip/behavior/historyLoop"), false).toBool());
}

QString Application::sessionDir() const
{
    return QDir(configDir()).filePath(QStringLiteral("session"));
}

QString Application::groupDir(int id) const
{
    return QDir(sessionDir()).filePath(QStringLiteral("groups/%1").arg(id));
}

void Application::loadGroupRegistry()
{
    QFile f(QDir(sessionDir()).filePath(QStringLiteral("groups.json")));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isObject())
        m_groups.fromJson(doc.object());       // data only — emits nothing
}

void Application::saveGroupRegistry()
{
    if (!QDir().mkpath(sessionDir()))
        return;
    QFile f(QDir(sessionDir()).filePath(QStringLiteral("groups.json")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(m_groups.toJson()).toJson(QJsonDocument::Indented));
}

void Application::restoreSession()
{
    // (The group registry is loaded in wireGroups(), before this.)
    // The always-visible ungrouped set.
    SessionStore store(sessionDir());
    QVector<PinSnapshot> pins;
    if (store.hasSession()) {
        pins = store.load();
        for (const PinSnapshot& p : pins) {
            if (p.image.isNull())
                continue;
            auto model = std::make_unique<PinModel>(p.image);
            model->fromJson(p.transform);
            model->annotations()->fromJson(p.annotations);
            reattachAnimation(model.get());    // an animated pin plays again after a restart
            model->setGroup(0);                // session.json holds only the ungrouped set
            m_nextGeometry = p.geometry;
            m_haveNextGeometry = !p.geometry.isNull();
            m_pins.adopt(std::move(model));    // → added() → onPinAdded() (synchronous)
        }
        m_haveNextGeometry = false;
    }

    // Re-show whichever groups were shown at quit → "exactly last session's live set".
    for (int id : m_groups.shownIds())
        loadGroupIntoLive(id);

    qInfo() << "[app] restored" << pins.size() << "ungrouped pin(s) from last session";
}

void Application::saveSession()
{
    // Split the live windows: ungrouped (always-visible) vs. each shown group's pins.
    QVector<PinSnapshot> ungrouped;
    QHash<int, QVector<PinSnapshot>> shownPins;
    for (PinWindow* w : PinWindow::windows()) {
        if (!w->model())
            continue;
        const int g = w->model()->group();
        if (g == 0)
            ungrouped.append(snapshotFor(w));
        else if (m_groups.isShown(g))
            shownPins[g].append(snapshotFor(w));
    }

    const bool restore = m_settings.value(QStringLiteral("general/restoreSession"), true).toBool();
    SessionStore store(sessionDir());
    if (ungrouped.isEmpty() && !restore)
        store.clear();
    else
        store.save(ungrouped);

    // Only persist the shown groups' live pins when session-restore is on. With it off,
    // restoreSession() never loaded them, so writing here would wipe those groups' on-disk
    // pins. The registry (group names) is always saved — it's a durable user asset.
    // Iterate the shown ids, not shownPins: a shown group emptied of its last pin must
    // still be written (an empty save), or it would restore the pins it no longer has.
    if (restore)
        for (int id : m_groups.shownIds())
            SessionStore(groupDir(id)).save(shownPins.value(id));
    saveGroupRegistry();
}

void Application::putAwayGroup(int id)
{
    // Serialize the group's live pins to disk, then destroy their windows. Destroy (not
    // close) so they never enter the paste recover-ring — the data lives on disk instead.
    QVector<PinSnapshot> snaps;
    QVector<PinModel*> models;
    for (PinWindow* w : PinWindow::windows()) {
        if (w->model() && w->model()->group() == id) {
            snaps.append(snapshotFor(w));
            models.append(w->model());
        }
    }
    SessionStore(groupDir(id)).save(snaps);
    for (PinModel* m : models)
        m_pins.destroy(m);
}

void Application::loadGroupIntoLive(int id)
{
    // Paste → Behavior → "Flash after switching to another group": the group's pins flash as
    // they appear, so you can pick the newly-shown set out from any pins already on screen.
    // Restored pins keep showAt(animate=false) — this flash is triggered after the fact, so
    // it stays clear of the creation/activation path.
    const bool flash =
        m_settings.value(QStringLiteral("paste/behavior/flashOnGroupSwitch"), true).toBool();

    const QVector<PinSnapshot> pins = SessionStore(groupDir(id)).load();
    for (const PinSnapshot& p : pins) {
        if (p.image.isNull())
            continue;
        auto model = std::make_unique<PinModel>(p.image);
        model->fromJson(p.transform);
        model->annotations()->fromJson(p.annotations);
        reattachAnimation(model.get());
        model->setGroup(id);
        m_nextGeometry = p.geometry;
        m_haveNextGeometry = !p.geometry.isNull();
        PinModel* raw = model.get();          // adopt() moves the unique_ptr out from under us
        m_pins.adopt(std::move(model));
        if (flash)
            if (PinWindow* w = windowForModel(raw))
                w->flash();
    }
    m_haveNextGeometry = false;
}

void Application::onGroupRemoved(int id)
{
    // Delete (not put-away): discard any live pins of this group without serializing,
    // then wipe its on-disk store. Fires while the registry still reports it as shown.
    if (m_groups.isShown(id)) {
        QVector<PinModel*> models;
        for (PinWindow* w : PinWindow::windows())
            if (w->model() && w->model()->group() == id)
                models.append(w->model());
        for (PinModel* m : models)
            m_pins.destroy(m);
    }
    SessionStore(groupDir(id)).clear();
    QDir().rmdir(groupDir(id));
}

void Application::assignPinToGroup(PinModel* model, int groupId)
{
    if (!model || model->group() == groupId)
        return;
    model->setGroup(groupId);

    PinWindow* w = windowForModel(model);
    // Paste → Behavior → "Flash after switching to another group": the moved pin flashes to
    // show which one changed hands — whether or not it then leaves the screen.
    const bool flash = w
        && m_settings.value(QStringLiteral("paste/behavior/flashOnGroupSwitch"), true).toBool();

    // The pin stays on screen if its new group is the ungrouped set or a shown group;
    // otherwise it belongs to a hidden group — serialize it there and close its window.
    if (groupId == 0 || m_groups.isShown(groupId)) {
        if (flash)
            w->flash();
        refreshGroupManager();   // retag-in-place fires no added/removed, so refresh here
        return;
    }

    // Snapshot before the flash, not after: the pin is still live for the length of the
    // sweep, and the state the hidden group restores should be the state it had at the
    // moment of the move — not whatever a stray drag left behind 0.7s later.
    SessionStore store(groupDir(groupId));
    QVector<PinSnapshot> snaps = store.load();
    if (w)
        snaps.append(snapshotFor(w));
    store.save(snaps);

    // → removed() → window torn down (bypasses the recover ring). Deferred to the end of
    // the sweep when flashing, so the pin visibly flashes *then* disappears. The connection
    // is scoped to the window, so a pin closed mid-flash simply never fires it.
    if (!flash) {
        m_pins.destroy(model);
        return;
    }
    connect(w, &PinWindow::flashFinished, this, [this, model] { m_pins.destroy(model); });
    w->flash();
}

int Application::groupPinCount(int id) const
{
    // Ungrouped pins (id 0) are always live; a shown group's pins are live too. Count the
    // manager's live models, not PinWindow::windows() — counting models is the direct
    // question, and it needs no assumption about when a torn-down window leaves the window
    // list (PinWindow::detach() takes it off there before its deleteLater() runs). Any
    // hidden group is counted on disk.
    if (id == 0 || m_groups.isShown(id)) {
        int n = 0;
        for (PinModel* m : m_pins.activePins())
            if (m && m->group() == id)
                ++n;
        return n;
    }
    return int(SessionStore(groupDir(id)).load().size());
}

void Application::showGroupManager()
{
    if (!m_groupManager)
        m_groupManager = new GroupManagerDialog(
            &m_groups, [this](int id) { return groupPinCount(id); });
    m_groupManager->show();
    m_groupManager->raise();
    m_groupManager->activateWindow();
}

void Application::refreshGroupManager()
{
    if (m_groupManager && m_groupManager->isVisible())
        m_groupManager->refresh();
}

bool Application::isAnotherInstanceRunning(const QString& key)
{
    QLocalSocket sock;
    sock.connectToServer(key);
    const bool connected = sock.waitForConnected(200);
    if (connected)
        sock.disconnectFromServer();
    return connected;
}

void Application::startSingleInstanceServer(const QString& key)
{
    QLocalServer::removeServer(key);                    // clear stale socket
    m_server = new QLocalServer(this);
    if (!m_server->listen(key))
        qWarning() << "[app] single-instance server failed to listen:" << m_server->errorString();
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        if (QLocalSocket* c = m_server->nextPendingConnection())
            c->deleteLater();
        // A second launch pinged us — surface the app.
        m_actions.dispatch(ActionId::OpenPreferences);
    });
}
