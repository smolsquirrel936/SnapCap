#pragma once

#include <QObject>
#include <QRect>
#include <QFont>

#include "core/Settings.h"
#include "core/action/ActionRegistry.h"
#include "core/PinManager.h"
#include "core/GroupRegistry.h"
#include "core/ClipboardContent.h"
#include "core/AreaHistory.h"
#include "platform/platform.h"

class TrayIcon;
class QLocalServer;
class QWidget;
class PinModel;
class History;
class PreferencesDialog;
class GroupManagerDialog;
class SnipOverlay;
class QImage;

// Composition root: owns Settings, the ActionRegistry, the platform backend,
// the tray, and the single-instance server; wires global actions + hotkeys.
// See claude-plan/Architecture-FileMap.md ("Startup & wiring").
class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject* parent = nullptr);
    ~Application() override;

    // True if another instance already owns the single-instance server.
    static bool isAnotherInstanceRunning(const QString& key);

private:
    void loadKeymap();
    void applyInterfaceFont();     // apply interface/font (empty -> the baseline below)
    void applyGpuAcceleration();   // general/gpu_acceleration -> core/render blur dispatch
    void applyColorScheme();       // interface/theme -> Qt's app-wide light/dark scheme
    void wireActions();
    void wirePins();
    void wireGroups();
    void registerGlobalHotkeys();
    // Arms the Quick snip hold-and-drag gesture from snip/quickSnip/trigger: a modifier-only
    // combo ("Ctrl+Alt") arms it; empty / no action selected disarms it. Call at startup,
    // after Preferences closes, and when a snip ends (to re-arm).
    void applyQuickSnipTrigger();
    void showPreferences();
    // Builds a fully-configured overlay (settings, sinks, detect mode) but does NOT
    // start() it, so each snip action can add its own twist first. Returns nullptr
    // when a capture is already in progress.
    SnipOverlay* beginSnip();
    void startCustomSnip();            // CustomSnip hotkey, after any configured delay
    void startSingleInstanceServer(const QString& key);
    void doPaste();
    // Pin one classified clipboard item, attaching the animation when it came from a file.
    void pinClipboardImage(const clip::Content& c);
    void closeAllPins(bool destroy);   // tray "Close/Destroy All Pins" (+ optional confirm)
    void onPinAdded(PinModel* model);
    clip::PasteOptions pasteOptions() const;

    // Stage 6: history + session backup/restore.
    QString configDir() const;
    void setupHistory();
    void applyAreaHistorySettings();   // pull max/loop from snip/behavior/* into m_areaHistory
    void restoreSession();
    void saveSession();

    // Pin groups: the always-visible ungrouped set lives in session.json; each group's
    // pins live in session/groups/<id>/; the registry (names + shown ids) in groups.json.
    // Any number of groups can be shown at once — live windows are the ungrouped set plus
    // every shown group's pins.
    QString sessionDir() const;
    QString groupDir(int id) const;
    void onGroupRemoved(int id);                        // discard live pins + on-disk store
    void assignPinToGroup(PinModel* model, int groupId);
    void putAwayGroup(int id);                          // serialize + destroy the group's live pins
    void loadGroupIntoLive(int id);                     // recreate the group's pins from disk
    int  groupPinCount(int id) const;                  // live count if shown, else on-disk count
    void showGroupManager();
    void refreshGroupManager();                        // re-read pin counts if the dialog is open
    void loadGroupRegistry();
    void saveGroupRegistry();

    Settings m_settings;
    QFont m_defaultUiFont;         // baseline UI font: platform family @ kDefaultUiPointSize
    ActionRegistry m_actions;
    platform::Platform m_platform;
    PinManager m_pins;
    GroupRegistry m_groups;
    History* m_history = nullptr;
    AreaHistory m_areaHistory;     // session-only reuse-area ring (`R`/`Shift+R`)
    TrayIcon* m_tray = nullptr;
    QLocalServer* m_server = nullptr;
    PreferencesDialog* m_prefs = nullptr;
    GroupManagerDialog* m_groupManager = nullptr;

    // The live capture overlay, if any. Non-null while a snip is in progress; used
    // to block re-entry (a second F1) and unrelated global hotkeys mid-capture.
    SnipOverlay* m_activeSnip = nullptr;

    // True only while beginSnip() runs FOR the hold-drag gesture, so it keeps the gesture
    // hook armed (it must still report the button-up). Every other snip disarms it.
    bool m_inGestureSnip = false;

    // Set just before PinManager::adopt() so onPinAdded() places a restored pin at
    // its saved geometry instead of under the cursor (added() fires synchronously).
    QRect m_nextGeometry;
    bool  m_haveNextGeometry = false;

    // Set just before PinManager::addImage() when pinning a snip so onPinAdded() places
    // the pin's image content at the capture spot (added() fires synchronously).
    QPoint m_nextContentTopLeft;
    bool   m_haveNextContent = false;

    // True once the clipboard changes; `doPaste` uses it to paste a freshly-copied item
    // ahead of the recover ring exactly once. Reset to false at launch and whenever a pin is
    // closed/destroyed, so only a copy made *after* the last close is treated as "fresh"
    // (older clipboard content is "stale" → recover-first). See doPaste().
    bool   m_clipboardChanged = false;
};
