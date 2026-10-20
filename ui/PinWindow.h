#pragma once

#include <QWidget>
#include <QImage>
#include <QColor>
#include <QList>
#include <QVector>
#include <QRect>
#include <QPoint>
#include <QJsonObject>
#include <QTransform>
#include <memory>

#include "core/ClipboardContent.h"

class QTimer;

class PinModel;
class PinManager;
class Settings;
class GroupRegistry;
class PinEditController;
namespace platform { class IPinSurface; class IClipboard; class IOcr; }

// A floating pinned-image window: frameless, translucent, always-on-top. Renders
// its PinModel's compose() and handles the pin-context keys/mouse (rotate, flip,
// scale, opacity, move, close/destroy, replace, copy-text) directly. Click-through
// + always-on-top go through platform::IPinSurface. A static registry lets
// siblings be found for magnetic snapping, hide-all, and the click-through toggle.
// See Features.md §3 / §4 (Paste). Stage 5.
class PinWindow : public QWidget {
    Q_OBJECT
public:
    PinWindow(PinModel* model, PinManager* manager, Settings* settings,
              GroupRegistry* groups = nullptr, QWidget* parent = nullptr);
    ~PinWindow() override;

    // animate=false suppresses the creation flash unconditionally (used for pins restored
    // from the last session, which must not all flash on startup). When true, the flash
    // still obeys the Paste → Behavior → "Flash a newly pasted pin" setting.
    void showAt(const QPoint& globalTopLeft, bool animate = true);
    // Show the pin so its image *content* (not the bordered window) lands at the given
    // global top-left — used to pin a snip back onto the exact spot it was captured.
    void showWithContentAt(const QPoint& contentTopLeftGlobal);
    PinModel* model() const { return m_model; }

    // The model is about to be freed (PinManager::destroy erases it right after emitting
    // removed(), and close() may evict it from the ring later) — go inert so nothing can
    // touch it again: stop both clocks, drop the external connections, delete the toolbar,
    // and leave the window list. The widget itself is still deleteLater()'d by the caller,
    // because this often runs from inside the window's own call stack (the pin's context
    // menu destroys it). model() returns null afterwards.
    void detach();

    // Width of the transparent margin reserved around the image for the neon border.
    static int borderMargin();

    // Maps a point in the model's SOURCE-image px to this widget's local (logical)
    // coords — PinModel::sourceToComposed() plus the dpr and the border margin. The pin
    // editor paints annotations/drafts through it and inverts it for mouse input.
    QTransform sourceToWidget() const;

    // Re-run compose() and repaint (the editor calls this when annotations change).
    void refreshComposed() { refreshFromModel(); }

    // Decode `path` as an animation (GIF & friends) and attach its frames to `m`, so the
    // pin plays instead of showing frame 0. Returns true when the model became animated.
    // A file too big to hold in memory pins as a still; that only raises a warning box when
    // `warnParent` is given — a session restore passes null so startup stays silent.
    // Static because all three entry points (replace-by-file, a pasted file path, session
    // restore) attach before or without a window of their own.
    static bool attachAnimation(PinModel* m, const QString& path, QWidget* warnParent);

    // Run the attention sweep unconditionally, then emit flashFinished(). Unlike the paste
    // flash (see showAt) this obeys no "Flash a newly pasted pin" gate and grabs no focus —
    // it's the group layer's flash, gated by its own Paste → Behavior → "Flash after
    // switching to another group" setting at the call site. Honours "Translucent when
    // flashing" like any other flash.
    void flash();

    // The annotation toolbar (Space / pin menu "Show toolbar"). Only one pin may have it
    // open at a time — opening it here closes whichever pin had it.
    void toggleToolbar();
    void closeToolbar();
    bool toolbarOpen() const { return m_edit != nullptr; }

    // Single source of the app's "theme color" used for the focused/selected outline.
    // TODO(theme): themes aren't implemented yet (Stage 7 / Interface tab). Until then
    // this returns a neon-cyan placeholder; route it through the theme manager/Settings
    // once themes land instead of hard-coding the colour at call sites.
    static QColor themeColor();
    // Outline colour for a pin that is in the Ctrl-click multi-selection — a distinct hue
    // from themeColor() so several selected pins read differently from the single focused one.
    static QColor selectionColor();

    bool clickThrough() const { return m_clickThrough; }
    void setClickThrough(bool on);
    void toggleClickThrough() { setClickThrough(!m_clickThrough); }

    static const QList<PinWindow*>& windows() { return s_windows; }

    // App-wide clipboard backend, shared by every pin's "Copy image" (Output→Clipboard
    // extra formats). Set once at startup; null falls back to Qt's QClipboard::setImage.
    static void setClipboard(platform::IClipboard* c) { s_clipboard = c; }
    // Paste → Conversion options for replaceFromClipboard(), kept in sync by Application
    // so pasting onto a pin looks identical to a fresh F3 paste.
    static void setPasteOptions(const clip::PasteOptions& o) { s_pasteOptions = o; }

    // App-wide OCR backend, shared by every pin's toolbar OCR mode. Set once at startup;
    // null (no backend built) makes the pin's OCR button a no-op.
    static void setOcr(platform::IOcr* o) { s_ocr = o; }
    static platform::IOcr* ocr() { return s_ocr; }

    // The 8 invisible resize grips along the image border (4 corners + 4 edge
    // midpoints). Dragging any of them scales the pin uniformly, anchored at the
    // opposite handle. They have no visible chrome — only a hit band + resize cursor.
    enum class Grip { None, Left, Right, Top, Bottom,
                      TopLeft, TopRight, BottomLeft, BottomRight };

signals:
    void preferencesRequested();
    // User picked a target group for this pin from its right-click menu (0 = ungrouped).
    // The app layer retags the model and, if the target isn't currently shown, serializes
    // the pin out and closes its window.
    void assignToGroupRequested(PinModel* model, int groupId);
    // The rainbow sweep just settled. Lets the app layer defer work until a pin has finished
    // flashing — a pin moved to a hidden group flashes first and is torn down on this.
    void flashFinished();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;   // right-click pin menu
    void changeEvent(QEvent*) override;   // repaint outline on window (de)activation
    void moveEvent(QMoveEvent*) override;      // keep the annotation bars anchored
    void resizeEvent(QResizeEvent*) override;

private:
    void refreshFromModel();              // recompute compose() + resize around center
    void applyHidden(bool hidden);
    void updatePlayback();                // start/stop the animation clock for the current state

    Grip gripAt(const QPoint& localPos) const;       // which border grip the point hits
    static Qt::CursorShape cursorForGrip(Grip g);    // resize cursor for a grip
    void beginResize(Grip g, const QPoint& localPos);
    void updateResize(const QPoint& globalPos);      // scale to follow the dragged grip
    void replaceFromClipboard();
    void copyPlainText();
    void copyImageToClipboard(bool asFile = false);   // asFile: Ctrl+Alt+C copy-as-file
    void toggleThumbnail();

    // Multi-select (Ctrl+click / Ctrl+A). Selection is a pure-UI notion held per window
    // (m_selected), so a destroyed pin drops out of it for free. opTargets() is the fan-out
    // set for every pin operation: the whole visible selection when this pin is part of it,
    // else just this pin — which is why the focused pin "counts as one selected pin".
    QList<PinWindow*> opTargets();
    void closeTargets(bool destroy);                 // snapshots models first (close deletes windows)
    static QList<PinWindow*> selectedWindows();      // visible + m_selected
    static void clearSelection();                    // deselect every pin
    static void selectAllVisible();                  // Ctrl+A: select every visible pin
    // Pin context-menu file actions. saveImageAs()/replaceByFile() record the chosen file
    // as the model's sourcePath, which is what viewInFolder() reveals (and what greys it
    // out when the pin has no file behind it).
    void saveImageAs();                   // compose() → a Save dialog (Output → Manual Save)
    void replaceByFile();                 // an Open dialog → new pin pixels
    void viewInFolder();                  // reveal sourcePath() in the file manager
    bool canViewInFolder() const;         // sourcePath() is set and still on disk
    void applyDisplaySettings();          // push Settings → this pin (max size, repaint)
    bool shadowEnabled() const;           // Paste → Display → Window shadow (unfocused pins)
    QColor shadowColor() const;
    void drawDropShadow(QPainter& p, const QRectF& imageRect) const;
    QVector<QRect> siblingGeometries() const;

    void startCreationAnimation();        // kick off the one-shot rainbow sweep
    void refreshAfterScreenChange();      // deferred recompose when the pin changes monitor
    QColor outlineColor() const;          // current border colour for the pin's state
    void drawNeonBorder(QPainter& p, const QRectF& imageRect, const QColor& c) const;
    void drawSpectrumBorder(QPainter& p, const QRectF& imageRect) const;  // fast-thumbnail flag

    PinModel* m_model = nullptr;
    PinManager* m_manager = nullptr;
    Settings* m_settings = nullptr;
    GroupRegistry* m_groups = nullptr;
    std::unique_ptr<platform::IPinSurface> m_surface;
    QImage m_composed;                    // cached compose() with devicePixelRatio set

    bool m_dragging = false;
    QPoint m_dragOffset;                  // cursor → window top-left, at press
    QPoint m_dragStart;                   // this pin's top-left at press (group-drag delta base)
    QPoint m_pressGlobal;                 // cursor at press (click-vs-drag slop test)
    bool m_collapseOnClick = false;       // a clean left-click will deselect all pins
    bool m_selected = false;              // in the Ctrl-click multi-selection
    bool m_clickThrough = false;
    bool m_screenHooked = false;          // QWindow::screenChanged connected once winId exists

    // Border-drag scaling state (active between press on a grip and release).
    Grip    m_resizeGrip = Grip::None;
    QPointF m_resizeAnchorGlobal;         // the opposite handle — stays fixed during the drag
    QPointF m_resizeAnchorFrac;           // that handle as a fraction (0..1) of the content rect
    QPointF m_resizeOrigVec;              // anchor → grabbed handle, in logical px, at press
    qreal   m_resizeStartScale = 1.0;

    bool m_thumbnail = false;
    qreal m_preThumbScale = 1.0;          // pre-thumbnail scale (non-reset path)
    QJsonObject m_thumbSnapshot;          // full pre-thumbnail state (reset-on-thumbnail path)

    QTimer* m_rainbowTimer = nullptr;     // drives the one-shot creation sweep
    int  m_rainbowStep = -1;              // index into the neon ROYGBIV palette; <0 = settled

    QTimer* m_frameTimer = nullptr;       // animation clock (single-shot: delays are per frame)
    bool m_paused = false;                // pin menu → Pause (an open toolbar pauses separately)

    PinEditController* m_edit = nullptr;  // the open annotation toolbar, or null

    static QList<PinWindow*> s_windows;
    static platform::IClipboard* s_clipboard;
    static platform::IOcr* s_ocr;
    static clip::PasteOptions s_pasteOptions;
    static PinWindow* s_editing;          // the one pin whose toolbar is open, or null
};
