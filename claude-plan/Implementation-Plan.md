# SnapCap — Implementation Plan (Staged Roadmap)

## Context

This is the build roadmap for SnapCap, derived from `Features.md` (what to build) and
`Tech-Stack-Plan.md` (how/with what). It breaks the work into sequential stages. Each stage
lists its **Objectives**, **Components & responsibilities** (with key method signatures),
the **Key bindings handled** (cross-referenced to `Features.md §4`), **Data/settings**,
**i18n keys** introduced, **Algorithms & notes**, and **Testing** (automated + manual) plus
**Exit criteria** — so each stage is independently verifiable before the next begins.

Stack recap (`Tech-Stack-Plan.md`): Qt 6 + C++ (Widgets), yaml-cpp i18n, CMake + Ninja with
Qt's **MinGW** kit (Qt 6.11.1; the planned MSVC/vcpkg setup was replaced in Stage 0 — see below),
Qt linked **dynamically** under **LGPLv3**. Layered dirs: `app/ ui/ core/ platform/ lang/`.

> **Per-file responsibilities & interaction flows:** see `Architecture-FileMap.md` — it
> documents what every file (current + planned) does, the inward-only dependency rule, and
> the primary call/data flows (startup, snip, paste/pin, i18n switch, settings/hotkey remap).
> The "Components & responsibilities" subsections below name the same files; that doc is the
> cross-cutting view, this one is the per-stage build order.

---

## Shared infrastructure & conventions (applies to every stage)

### Action/Command layer — the spine for all key bindings

Every user-triggerable behavior in `Features.md` is a named **Action**; hotkeys, tray menu,
and overlay/pin keyboard handlers all dispatch through one registry. This is
what makes hotkeys **remappable** (`Features.md §5`).

```cpp
// core/action/ActionId.h  — stable identifiers, never user-facing strings
enum class ActionId {
    Snip, SnipCopy, CustomSnip, Paste, HideAllPins, ToggleClickThrough,   // global
    Snip_Copy, Snip_Save, Snip_QuickSave, Snip_Pin, Snip_Abort,
    Snip_ToggleDetect, Snip_Fullscreen, Snip_RepeatLast, Snip_HistPrev, Snip_HistNext,
    Pin_RotateCW, Pin_RotateCCW, Pin_FlipH, Pin_FlipV, Pin_Reset, Pin_Thumbnail,
    Pin_Close, Pin_Destroy, Pin_SelectAll, Pin_ReplaceFromClipboard, Pin_CopyText,
    Edit_Undo, Edit_Redo, Edit_ClearAll, Color_Copy, OpenPreferences, /* ... */
};

// core/action/ActionRegistry.h
class ActionRegistry {
public:
    void registerAction(ActionId id, std::function<void()> handler);
    bool dispatch(ActionId id);                       // returns false if unbound
    // Keymap: portable sequence string ("Ctrl+Shift+S") <-> ActionId, persisted.
    void bindKey(ActionId id, const QString& sequence);
    QString keyFor(ActionId id) const;
    std::optional<ActionId> actionFor(const QString& sequence, KeyContext ctx) const;
};
enum class KeyContext { Global, Snip, Pin, Edit };     // disambiguates context-local keys
```

Default keymap is loaded from a committed `res/keymap.default.yaml` (mirrors `Features.md §4`
exactly) and overridden by user settings. Context matters: e.g. `Ctrl+A` = *fullscreen* in
`KeyContext::Snip` but *select-all-pins* in `KeyContext::Pin` (per `Features.md`).

### Settings — one typed store

`core/Settings` wraps `QSettings` (INI format → a `config.ini`). Stage sections below list the keys each stage adds. Conventions:
group names mirror Preferences tabs (`[General] [Snip] [Paste] [Control] [Annotate]
[Interface] [Output] [Misc] [Log]`, per `Features.md §5`).

### i18n — every string is a key

No literal UI text in code (`Tech-Stack-Plan.md`). Each stage's "i18n keys" subsection lists
the keys to add to `lang/en.yaml` (+ translations). Namespacing matches the feature areas:
`menu.*`, `snip.*`, `paste.*`, `edit.*`, `prefs.*`, `tray.*`, `about.*`. `tools/i18n_check.py`
gates every PR.

### Scope policy — no paid tier

Every feature in `Features.md` is **in scope** (decided 2026-07-12) — SnapCap has no paid tier. (Those features were once tagged `[PRO]`;
the tag was retired 2026-10-20.) The architecture already leaves room for them (e.g. the Action
layer + per-tool render allow adding grayscale/invert/hot-corners). Each stage's "Deferred extras"
list below is therefore that area's remaining **backlog**, not an exclusion.

---

## Cross-cutting testing strategy

- **Unit tests — QtTest** (`test/`, one `tst_*.cpp` per component, registered with
  `qt_add_test`/`add_test` + CTest). Target *platform-agnostic* `core/`: action keymap, i18n,
  selection math, annotation doc, color conversion, clipboard classification, history,
  session store.
- **Golden-image tests** for rendering: render to `QImage`, compare to committed reference
  PNGs with a tolerance (`qFuzzyCompare` per-pixel within ε) to absorb AA differences.
- **i18n gate** — `tools/i18n_check.py`, run by hand on every change touching UI strings; fails
  on missing/orphan keys.
- **Platform pieces** that can't run headless (hotkey, layered window, element detect) are
  verified by the manual checklists below. *(The planned `test/manual/` smoke harnesses were
  never built.)*
- **Manual checklists** — the GUI/platform rows are each stage's exit gate.
- **No CI.** The planned GitHub Actions pipeline was never set up: builds, `ctest` and
  `i18n_check` run locally, and the release ZIP comes from `cmake --build build --target package`
  (`docs/Development.md` → Building a release).
- **Per-stage DoD:** code + unit tests (where logic is testable) + manual checklist green +
  i18n keys added (no hard-coded strings) + builds & packages clean.

## Stage dependency order

`0 → 1 → 2 → {3, 4} → 5 → 6 → 7`  (Stages 3 and 4 parallelizable after 2.)

---

## Stage 0 — Foundation & i18n  *(DONE — scaffolded)*

**Objectives:** compilable Qt skeleton; out-of-code YAML i18n with live switching; platform
abstraction interfaces defined.

**Components:** `CMakeLists.txt`, `core/i18n/I18n`, `platform/platform.h`, `app/main.cpp`
(demo), `lang/en.yaml`, `lang/zh_cn.yaml`, `lang/zh_tw.yaml`, `tools/i18n_check.py`.

**Testing:**
- ✅ `tools/i18n_check.py` → "i18n consistent: 3 locale(s), 14 keys each" (en/zh_cn/zh_tw;
  now skips `test/`,`build/`).
- ✅ `test/tst_i18n.cpp` wired into CTest (`snapcap_core` lib shared by app + tests):
  fallback chain, `{placeholder}` interpolation, locale discovery, unknown-locale rejection,
  single `localeChanged`. **Built & run green — `100% tests passed` (10 cases).**
- ✅ Build + `windeployqt` packaging verified; `SnapCap.exe` smoke-launches without crash;
  `lang/*.yaml` deploy beside the exe (en + zh_cn + zh_tw). ☐ *Remaining manual:* visually
  confirm live switch among English / 简体中文 / 繁體中文 in the demo window (needs a human;
  app launches fine with all three).

**Toolchain note (deviation from `Tech-Stack-Plan.md`):** the installed Qt is the **MinGW**
kit (Qt 6.11.1, `C:\Qt`, g++ 13.1 + bundled CMake/Ninja), **not MSVC 2022**. Stage 0 is
pure logic so this is harmless; revisit before Stage 5/7 (GPU path, layered-window Win32) —
either keep MinGW or add an MSVC Qt kit. yaml-cpp is built from source via CMake
`FetchContent` (`-DSNAPCAP_FETCH_YAMLCPP=ON`) since no vcpkg/compatible system copy exists
(a conda MSVC build on PATH must be bypassed).

**Build command (reproducible):**
```
set PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;%PATH%
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 -DCMAKE_CXX_COMPILER=g++ ^
      -DSNAPCAP_FETCH_YAMLCPP=ON
cmake --build build && ctest --test-dir build --output-on-failure
```

**Exit:** ✅ compiles, ✅ packages via `windeployqt`, ✅ i18n unit test green. **Stage 0 done**
(bar the one human-eyes live-switch confirmation).

---

## Stage 1 — App Shell & Windows Platform Backbone  *(DONE)*

**Status: ✅ implemented, built, tested.** All 4 unit tests pass (`tst_keyseq_codec`,
`tst_action_registry`, `tst_settings`, + Stage 0's `tst_i18n`); `i18n_check` green
(15 keys × 3 locales). Smoke-verified: `splog.txt` shows `registered hotkey F1 -> snip`
and `F3 -> paste`; the single-instance guard makes a 2nd launch exit 0 (~fast); the
`error.hotkey_taken` i18n path fired live (Shift+F3 was already owned on the test machine).

**Deviations from the original plan text (intentional):**
- **Single-instance** uses `QLocalServer`/`QLocalSocket` only (no named mutex) — sufficient
  and cross-platform-friendly. A 2nd launch pings the server (which raises the app) and exits.
- **i18n keys:** tray labels reuse the existing `menu.*` keys (DRY) instead of new `tray.*`;
  only `error.hotkey_taken` was added.
- **KeySequenceCodec** parses to a neutral `KeySequence{Qt::KeyboardModifiers, Qt::Key}` (not
  `QKeySequence` → keeps `core/` free of Qt-GUI); the Qt::Key→Win32 VK mapping lives in
  `WinGlobalHotkey`.
- **Default keymap** is the single source of hotkey defaults (`res/keymap.default.yaml`);
  `config.ini` stores only user *overrides* (`hotkey/<action>`), so defaults aren't duplicated.
- **Preferences:** a temporary placeholder window (reuses the Stage 0 i18n live-switch demo)
  is shown by `OpenPreferences` until the real dialog (Stage 6).
- **Manual hotkey harness** (`test/manual/hotkey_smoke`) was skipped — verified via the real
  app + `splog.txt` instead.

**Objectives:** a tray-resident app with remappable global hotkeys, the Action layer, typed
settings, and the first concrete `platform/win` backend.

**Components & responsibilities:**
- `app/Application` — owns lifecycle, the `ActionRegistry`, `Settings`, tray, and platform
  factory. **Single-instance guard** via named mutex + `QLocalServer` (second launch sends
  its CLI args to the running instance, then exits).
  - `void wireActions()` — registers global `Snip`/`SnipCopy`/`CustomSnip`/
    `SnipActiveWindow`/`Paste`/`HideAllPins`/`ToggleClickThrough` + `OpenPreferences`
    handlers (stubs that log until later stages fill them). The four snip actions share
    `beginSnip()`, which builds a configured overlay without starting it: `SnipCopy` adds
    `setOneShotCopy`, `CustomSnip` adds `setPresetArea` after its `snip/customSnip/delay`,
    and `SnipActiveWindow` adds `setPresetArea(platform::foregroundWindowRect())` — resolved
    **before** the overlay exists, since the overlay then owns the foreground. That hotkey is
    unbound by default and does nothing (logs) when no foreign window is on screen.
- `ui/TrayIcon : QSystemTrayIcon` — menu items dispatch ActionIds; labels from `tray.*` i18n
  keys; left-click→Snip, middle-click→Paste (per `Features.md §5`); rebuilds on
  `localeChanged()`.
- `platform/win/WinGlobalHotkey : IGlobalHotkey` — `RegisterHotKey` + a `QAbstractNative
  EventFilter` translating `WM_HOTKEY` → ActionId dispatch. Parses portable sequence strings
  → `MOD_*`/VK codes.
- `core/Settings` — typed getters/setters over `QSettings(IniFormat)`; emits `changed(key)`.
- `core/Logging` — `qInstallMessageHandler` → rotating `splog.txt` (mirrors `[Log]` config).

**Key bindings handled:** global `F1` Snip, `Ctrl+F1` SnipCopy, `Shift+F1` CustomSnip,
`F3` Paste, `Shift+F3` HideAll, click-through toggle (configurable, default unbound —
`Features.md §3`).

**Data/settings (`[Control]`, `[General]`):** `hotkey/snip=F1`, `hotkey/snipCopy=Ctrl+F1`,
`hotkey/customSnip=Shift+F1`, `hotkey/paste=F3`, `hotkey/hideAll=Shift+F3`,
`hotkey/clickThrough=` (empty), `snip/customSnip/{x,y,width,height,delay}`,
`general/historyDir`, `general/hideTray=false`, `log/file=splog.txt`,
`log/rotateSizeMb=2.0`.

**i18n keys:** `tray.snip`, `tray.paste`, `tray.preferences`, `tray.quit`,
`error.hotkey_taken`, `app.name`.

**Algorithms & notes:** hotkey-string parser is pure (testable without Win32); a
`KeySequenceCodec` converts `"Ctrl+Shift+S"` ⇄ `{mods, vk}` and ⇄ `QKeySequence` for the
overlay/pin contexts. Registration failures (key already owned) surface `error.hotkey_taken`.

**Testing:**
- Unit: `tst_keyseq_codec` (round-trips, modifiers, function/punctuation keys, invalid input);
  `tst_settings` (round-trip, defaults, invalid coercion); `tst_action_registry`
  (bind/dispatch, unbound returns false, context disambiguation of `Ctrl+A`).
- Manual harness `test/manual/hotkey_smoke`: register F1 globally, print on press from an
  unfocused state.
- Manual checklist: F1/F3 fire when unfocused; second launch focuses first; tray menu acts;
  settings survive restart; duplicate-hotkey error shown.

**Exit:** F1 anywhere → logged "Snip action"; tray works; settings + keymap persist; codec &
registry unit tests green.

**Deferred extras:** arbitrary-action binding (the registry already supports it — just no UI).
(Per-app hotkey ignore-list shipped — Preferences → Control → App exclusions mutes global hotkeys
while a listed app is foreground; see the `platform/win/WinGlobalHotkey` row of
`Architecture-FileMap.md`.)

---

## Stage 2 — Snip Capture Core  *(DONE — signed off 2026-07-25)*

**Status:** ✅ **complete and signed off by the user 2026-07-25** — the white-band fix across the
bezel was the last open item and is confirmed fixed. Six originally-planned items were **dropped as
won't-do** at sign-off (user's call, listed at their bullets below): the `X` abort key, hold-`Space`
to reposition while sizing, a QuickSave button in the toolbar overflow, the `snip.saved_to`
success message, `snip/lastRegion` persistence, and `snip/anchorsThreshold`. The **DXGI Desktop
Duplication** backend remains deferred to Stage 7, where it is planned as an **opt-in** backend
with `BitBlt` staying the default (decided 2026-07-26; see the Stage 7 DXGI bullet).
Implemented + building + tested (`100% tests passed`, 24 suites repo-wide as of 2026-07-16):
- ✅ `core/SelectionModel` — unit-tested (`tst_selection`, 8 cases).
- ✅ `core/ImageExport` — clipboard + file (PNG/JPG/BMP/…); unit-tested (`tst_image_export`).
- ✅ `platform/win/WinScreenCapture` — GDI BitBlt of the virtual desktop (physical px).
- ✅ `ui/SnipOverlay` — full-desktop dim + drag-select + **magnifier (size/color readout)** +
  Copy/Save/Close toolbar; keyboard (Esc/Enter/Ctrl+C/Ctrl+S, arrows move, Ctrl/Shift+arrows
  grow/shrink), double-click copy, right-click abort. Wired to the **Snip** action (F1).
- ✅ Smoke-launched: app constructs the capture backend, no crash.

**Scope notes / deferrals to keep this increment tight:**
- **`ui/Magnifier` and `ui/SnipToolbar` were never created, and won't be** — the planned "split
  them out if they grow" never happened, and the work landed elsewhere instead. As the code stands
  today: the loupe is `SnipScreenView::drawMagnifier` (its readout is the Stage 3 multi-format
  colour table, not the original size+colour line), and the toolbar is **`ui/PaintBar`** — the
  shared Stage 4 annotation bar, which carries the snip Copy/Save/Pin/Close buttons rather than a
  snip-only widget. Both host files are large now, so treat a future split as a refactor of
  `SnipScreenView`/`PaintBar`, not as building the two planned files. Colour **readout** lives in
  the magnifier; the `core/ColorPicker` copy-workflow + `Tab` window/element detection are
  **Stage 3** (done).
- ✅ **Since wired:** quick-save (`Ctrl+Shift+S` → `SnipOverlay::quickSave()`, driven by
  `output/quickSave/*`), pin-to-screen (`Ctrl+T`/middle-click, in Stage 5), and `` ` ``/`!`
  cursor-capture (`snip/captureCursor`, freeze-time `grabCursor()` snapshot).
  **Print was removed** — the never-wired `snip_print`/`Ctrl+P` `ActionId::Snip_Print` and the
  `print` combo value are gone (removed rather than wired, like the Snip page's `hdrAdjust`).
- **Capture model (multi-monitor):** F1 freezes **every** monitor (per-screen
  `QScreen::grabWindow`, each DPI-correct) and shows one full-screen `ui/SnipScreenView` per
  monitor, coordinated by the `ui/SnipOverlay` **controller** over one global `SelectionModel`.
  A drag can **span monitors**; the whole snip pipeline works in **capture (physical) pixel
  space** (`core/ScreenLayout` built from `platform::monitorLayout()`), so mixed-DPI monitors
  pack gap-free; the result is stitched by `core/ScreenComposite::compositeSelection` (capture
  space, 1:1, unit-tested with patterned images in `tst_screen_composite`). Each overlay window
  is placed deterministically on its physical monitor (`SnipScreenView::showOnScreen`, replacing
  `showFullScreen`, which retargeted under mixed DPI). The model is `core/ScreenLayout`.
- ✅ **Cross-monitor capture confirmed working** by the user (drag spans both monitors,
  Copy/Save produces a stitched image).
- ✅ **Fixed-ratio capture (2026-07-21):** modifier locks on a **corner** grip drag —
  `Shift` keeps the region's width:height ratio at grip-press, `Ctrl` forces a square; `Ctrl`
  also squares the *initial* drag-out (`Shift` can't — a region drawn from scratch has no
  ratio to keep yet). Edge grips are unconstrained (no fixed corner). Math is
  `ShapeConstraints::ratioCorner()` (pure, `tst_shifted_shapes`), which caps the scale rather
  than each axis so a drag into the desktop edge stops with the ratio intact instead of being
  cropped by `SelectionModel`'s clamp. Modifiers are sampled on mouse *motion*, so the lock
  engages on the next pixel of movement, not on the keypress itself.
  **Note the deliberate split:** `Shift` = square when *drawing an annotation shape*, but
  `Shift` = keep-ratio when *resizing the region* (user's call, 2026-07-21).
- ✅ **Refresh screenshot (2026-07-23):** the frozen desktop can be re-grabbed without leaving
  the overlay — `F5` or the ⟳ button that `hintLayout()` places right of the `w x h` readout
  (its own plate, not grouped with it; shown under the same `isReadOnly()` gate as the
  click-to-edit numbers, while `F5` also works before a region exists). A tap refreshes once; a
  press held past 400 ms repeats every 33 ms (`kRefreshIntervalMs`, ~30 fps) until released, so the
  view only re-locks on release — a tick landing mid-grab is dropped by `refreshFreeze()`'s
  re-entrancy guard, so a slow desktop simply refreshes less often. The ⟳ icon's spin has its own
  16 ms ticker (`kSpinIntervalMs`) that repaints nothing but the button plate, so the animation
  stays smooth regardless of grab cost. `SnipOverlay::refreshFreeze()` re-runs the (factored-out) `grabScreens()` and pushes
  the new pixels through `SnipScreenView::setBackground`. The overlays are kept out of the grab
  by **capture exclusion** (`SnipScreenView::setExcludedFromCapture` →
  `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`, applied to every view in `start()` for the
  whole snip), so nothing is hidden and **nothing blinks** — the first attempt hid the overlays
  per grab (zero opacity + `DwmFlush()`) and the strobe that produced during a continuous hold
  was rejected on sight (2026-07-23). That hide path survives only as the pre-Win10-2004
  fallback, selected by `m_captureExcluded`. Selection,
  annotations and the undo stack survive; the barcode payload / detection candidate are
  re-derived and a `,`/`.` history replay ends. The freeze-time **cursor** snapshot is *not*
  re-grabbed (mid-snip the live pointer is our own crosshair).
- ✅ **Rounded corners (2026-07-23):** the region can be captured as a round-cornered rectangle.
  Opt-in — Preferences → Snip → Display → *"Show rounded corner option while snipping"*
  (`snip/display/roundedCorners`, off by default, so nothing changes for anyone who doesn't ask
  for it). It reveals a chip that `hintLayout()` places immediately right of the `w x h` box (the ⟳
  button moves along after it), on its own plate: a hand-drawn corner icon (`drawCornerGlyph` — an L whose top-left join is either sharp or
  a quarter arc, mirroring what the region's own corner will look like) plus the bending level.
  Scroll over the chip to step the level, click it to toggle right-angle ↔ rounded. Two deliberate
  interaction calls: the level is **kept** across a toggle (so clicking twice returns to the same
  roundness — the user's stated requirement) though its *number* is shown only in rounded mode
  (right angles have no bend to report, so the plate shrinks to the icon), and scrolling **also
  switches the mode on**, since a
  wheel that silently changed a number with nothing to show for it would read as broken. Both
  `cornerRounded` and `cornerRadius` persist, so the shape survives across snips.
  The level is stored in **logical px** and scaled to capture px by `effectiveCornerRadius()` via
  `ScreenLayout::scaleAt(selection.topLeft())`, exactly as `captureStyle()` pre-scales pen sizes —
  so the corner is the same visual size on any monitor — then clamped to half the region's shorter
  side (the *stored* level is never clamped, so shrinking and re-enlarging the region restores the
  user's number). All four export paths (`copy`/`save`/`quickSave`/`onPin`) run
  `RoundedCorners::apply` **after** `flatten()`, so an annotation running into a corner is trimmed
  with the pixels under it. The preview repaints the four corner slivers with the **dim mask**
  rather than clipping the bright slice: QPainter's clip paths are aliased and looked ragged,
  while an antialiased fill of the same mask makes the cut corners read exactly like the
  surrounding dimmed desktop. Two gotchas worth keeping: painting the rounded rect with
  `CompositionMode_DestinationIn` leaves the corners **opaque** (that mode only touches pixels the
  source covers — the inverse slivers must be `Clear`ed instead), and transparency written to an
  alpha-less format lands on **black**, so `ImageExport::toFile` now flattens onto white for
  `jpg`/`bmp`/`p[bgp]m` (`flattenOnto`). Tests: `tst_rounded_corners`, plus two cases in
  `tst_image_export`.
- ✅ **FIXED (2026-07-12) — mixed-DPI multi-monitor:** both reported bugs resolved by the
  capture-pixel coordinate model (`core/ScreenLayout`): (1) only one monitor
  dimming — user-verified both now dim; (2) the huge white band in cross-monitor results — the
  phantom logical-gap band is gone (only genuine offset corners stay transparent).
  **User-verified across the bezel and signed off 2026-07-25** — no white band remains.

**Objectives:** capture the screen and let the user select/adjust a pixel-accurate region,
then copy/save it. Correct on **multi-monitor + HiDPI**.

**Components & responsibilities:**
- `platform/win/WinScreenCapture : IScreenCapture`
  - `QImage grabVirtualDesktop()` — BitBlt of the union of all screens; **per-monitor-DPI
    aware** (process declared `PerMonitorV2`); returns device-pixel image + a
    `QVector<ScreenInfo>` (geometry, devicePixelRatio) for mapping.
  - `QImage grabRect(const QRect& logical)` — DPI-mapped sub-grab.
  - *(DXGI Desktop Duplication backend deferred to **Stage 7**, behind this same interface — still
    unbuilt. Planned there as **opt-in with `BitBlt` as the default** and automatic fallback to
    `BitBlt` on any DDA error, so this interface and its callers do not change; see the Stage 7
    DXGI bullet.)*
- `ui/SnipOverlay : QObject` — **a controller, not a widget** (the plan's original
  `: QWidget` single-window design was replaced by the multi-monitor model): it freezes every
  screen and owns one `ui/SnipScreenView` per monitor, plus the one global `SelectionModel`, the
  `AnnotationDocument`, and the snip-context keymap. Each `SnipScreenView` is the frameless
  full-screen window that paints its frozen screen + dimming mask with the selection punched
  bright, and hosts the magnifier and (Stage 4) paint bar.
- `core/SelectionModel` — the selection rect + interaction state machine.
  - `void beginAt(QPoint)`, `void dragTo(QPoint)`, `void commit()`.
  - `void nudge(Edge, dx, dy)` for keyboard adjust; `void moveBy(dx,dy)`,
    `enlarge/shrink(dir)`; all **clamp to virtual-desktop bounds**.
  - emits `changed()` (drives magnifier + size readout).
- **Toolbar — shipped as `ui/PaintBar`**, not a separate `ui/SnipToolbar`: Copy/Save/Pin/Close →
  ActionIds, labels from `snip.toolbar.copy/save/pin/close`. **Won't do (2026-07-25, user's call):**
  the planned **QuickSave button in the toolbar overflow** — quick-save stays keyboard-only
  (`Ctrl+Shift+S`), so there is no `snip.toolbar.quicksave` key and none is needed.
- **Magnifier — shipped as `SnipScreenView::drawMagnifier`**, not a separate `ui/Magnifier`: loupe
  near the cursor with nearest-neighbour zoomed pixels, a full crosshair, live `snip.hint_size`
  ("{w} x {h}") and the Stage 3 colour readout.
- `core/ImageExport`
  - `bool toClipboard(const QImage&)`, `bool toFile(const QImage&, path, Format)`,
    `bool quickSave(const QImage&)` (auto-named into the quick-save folder).
  - Formats: `toFile` writes by extension, so **PNG/JPG/BMP/TGA/ICO/TIFF/GIF/PDF**
    (`Features.md §5`) all work, and `core/ManualSave::filterFor` surfaces every one of them in
    the Save dialog — see the Stage 7 **Save formats** entry for how (own TGA/GIF/PDF encoders +
    the `supportedSaveFormats()` runtime probe). Optional **auto alpha-channel removal** when fully opaque
    (`[Output] auto_alpha_channel_removal`).

**Key bindings handled (`KeyContext::Snip`, per `Features.md §4`):** `WASD` move cursor by 1px
(`SnipOverlay::nudgeCursor` — works during a drag too, which is the hold-`Mouse-left`+`WASD`
gesture); arrows move area; `Ctrl+arrows` enlarge; `Shift+arrows` shrink; `Ctrl+A` fullscreen;
`R`/`Shift+R` reuse an older/newer area; `Ctrl+C`/`Enter`/double-click copy; `Ctrl+S` save;
`Ctrl+Shift+S` quick-save; `Ctrl+T`/middle-click pin (handed to Stage 5); `Esc`/right-click
abort; `` ` ``/`!` toggle captured cursor. `Space` toggles the paint bar (Stage 4).

**Won't do (2026-07-25, user's call):**
- **`X` as an abort key** — `Esc` / right-click are enough for leaving snip mode, so no abort
  entry exists in `res/keymap.default.yaml`'s `snip:` group and none will be added.
- **Hold-`Space` to move the region while sizing** — `Space` is the paint-bar toggle
  (`SnipOverlay.cpp`, `Qt::Key_Space`); the reassignment stands and the hold gesture is dropped.

**Data/settings (`[Snip]`,`[Output]`) — as the code actually reads them:**
`snip/captureCursor=false` and `output/auto_alpha_channel_removal=true` shipped under their
planned names. The other three were **superseded**, not skipped:
- quick-save → `output/quickSave/pathTemplate` + `output/quickSave/showNotification` (a path
  *template*, not the planned plain `output/quickSaveDir`).
- save format → `output/manualSave/rememberExtension` + `output/manualSave/lastExtension` +
  `output/manualSave/fileName` (the dialog remembers the last extension used instead of holding a
  single `output/defaultFormat=png`).
- grip visibility → the boolean `snip/display/showAnchors` (+ `anchorColor`/`anchorBorderColor`).

**Won't do (2026-07-25, user's call):**
- **`snip/lastRegion`** — the last region is not persisted to disk. `R` reuses areas from the
  **session-only** history ring (`maxHistoryAreas`), so repeat-last deliberately does not survive
  an app restart.
- **`snip/anchorsThreshold=80`** — no size threshold; grips never auto-hide on a small region.
  `snip/display/showAnchors` is a plain on/off and that is the whole feature.

**i18n keys:** `snip.toolbar.copy/save/pin/close`, `snip.hint_size`.
**Won't do (2026-07-25, user's call):** `snip.saved_to` ("Saved to {path}") — a successful manual
save stays **silent**; `ui/SaveFeedback` only ever warns (`error.save_failed`,
`error.ico_downscale_title`). Quick-save's toast is a separate path
(`output/quickSave/showNotification`). Also dropped: `snip.toolbar.quicksave` (no such button).

**Algorithms & notes:** freeze one grab **per monitor** at snip-start for stable painting. **The
pipeline works in capture (native physical) px throughout** — this reverses the plan's original
"map all interaction in logical coords, convert to device pixels only at export", which the
mixed-DPI fix made wrong: `core/ScreenLayout` is the model, and logical↔capture conversion happens
only at the view boundary (`localLogicalToCapture` / `captureRectToLocalLogical`).
Selection math stays pure → unit-testable without a screen.

**Testing:**
- Unit: `tst_selection` (clamp to bounds, enlarge/shrink/move arithmetic, multi-rect
  virtual-desktop mapping, DPI scaling); `tst_image_export` (each format round-trips;
  alpha-removal only when opaque; quick-save naming uniqueness).
- Manual checklist ✅ **run and signed off 2026-07-25**: pixel-exact capture per monitor at
  100/125/150/200%; `Ctrl+C`/`Enter`/double-click → clipboard; `Ctrl+S` dialog → file; quick-save
  folder; magnifier tracks & shows size+color; keyboard nudges by exactly 1px; `Ctrl+A`
  fullscreen; `R` repeats; abort paths; **no white band across the bezel** in a cross-monitor
  capture.

**Exit:** ✅ **met** — F1 → select → byte-correct region in clipboard and as a saved file on the
multi-monitor HiDPI rig; selection unit tests green.

**Deferred extras:** none left — **rounded corners shipped 2026-07-23** (see the ✅ bullet above).
*Shipped since:* **region-size input** (click-to-edit `( x , y )` / `w x h` readout —
`SnipScreenView::openHintEdit` → `SnipOverlay::setSelectionField`), **fixed-ratio** corner
resize (`Shift` keeps the ratio, `Ctrl` squares — `core/annotation/ShapeConstraints`),
**refresh screenshot** (`F5` / ⟳ — `SnipOverlay::refreshFreeze`, see above) and **Quick snip**
— the modifier-only hold + drag gesture (`platform/win/WinQuickDragGesture`), which works with any
Ctrl/Alt/Win combo.

---

## Stage 3 — Window/Element Detection & Color Picker  *(DONE — signed off 2026-07-25)*

**Status:** ✅ **complete and signed off by the user 2026-07-25.** Color picker + magnifier done;
**browser element detection RESOLVED 2026-07-20**. Two originally-planned items were **dropped as
won't-do** at sign-off (user's call — see the Data/settings and Algorithms notes below): the
per-process **detection exclusion / crashes lists**, and the **hard timeout** on a detection query.
Built on top of the Stage 2 multi-monitor overlay:
- ✅ `core/ColorPicker` — pure HEX/RGB formatting (+ uppercase option), format toggle,
  `sampleAt`, clipboard copy; unit-tested (`tst_color`, 7 cases).
- ✅ **`platform/win/WinElementDetect`** — `windowAt` (top-level Z-order walk excluding our own
  overlay, skipping invisible/DWM-cloaked windows, DWM extended-frame bounds) + `elementAt`,
  which resolves the target window via that same walk and then hit-tests it with **MSAA
  `IAccessible::accHitTest`** (strategy 1, clamped to the window frame), falling back to the UI
  Automation Control-View descent (strategy 2) and finally `windowAt`. Registered in the Win
  platform bundle.

  **Two measured root causes** (instrumented behind `SNAPCAP_ELEMENT_DIAG=1`; the same
  measurements disproved the first diagnosis — Control View filtering and the 12-level depth
  cap):
  1. Firefox reports web content with full *scrollable* bounds (7315px body in a 1278px
     viewport), so the `area <= parentArea` guard discarded the document body and the descent
     died at the Document node. No geometric descent can work against that — Raw View with no
     guards at depth 64 still landed on a 6705px-tall container.
  2. `winmon::physRectToLogical` resolved the *exclusive* bottom-right corner as a point, so any
     rect ending on a monitor boundary snapped to the neighbouring monitor and mixed two scales
     — the source of the "shifted box" symptom below. Affected `windowAt()` and every other
     `winmon` caller too.

  UIA cannot own this at full scope: `ElementFromPoint` is global and returned our own overlay
  in 207/207 samples. `accHitTest` is subtree-scoped, so the overlay is structurally excluded.

  Diagnostics remain available behind `SNAPCAP_ELEMENT_DIAG=1` (off by default; logs geometry
  only). **Historical investigation record — the leads below are now explained by the two causes
  above; kept because attempts 1–3 shaped the current code** (the standalone
  `Stage3-Fixes-Plan.md` that held this was deleted in 5f44453):
  - Symptom: in Chromium apps, element mode highlights the **wrong** sub-element and the dashed
    region is **shifted** off the cursor (it doesn't even contain it).
  - Attempt 1 — replaced the original `ElementFromPoint` + `WS_EX_TRANSPARENT` self-exclusion
    with the Z-order walk + ControlView descent described above.
  - Attempt 2 — **bogus-rect guard**: a child only counts as more-specific if its rect has
    positive area and is no larger than its parent (`area > 0 && area <= parentArea`), stopping
    the descent from diving into stale/virtualized Chromium nodes. Plus a **cursor-containment
    sanity guard**: if the descended rect doesn't `contains(globalPos)` after mapping, fall back
    to the top-level `windowAt` rect rather than draw a shifted box.
  - Attempt 3 — the **monitor-pairing root cause** (found via a throwaway `tools/dpi_probe.cpp`,
    since removed): `buildTable()` paired `QScreen::name()` (a *friendly* string like
    `"MAG 272URDF"`) against `MONITORINFOEXW.szDevice` (`"\\.\DISPLAY1"`), so pairing **always**
    failed and every monitor fell back to `physical = logical × dpr` — which gets a scaled
    non-primary monitor's origin wrong. Fixed by pairing on **physical size** (`logical × dpr`),
    with an origin estimate only as a tie-break. Verified on the real 4K@150% + 1440p@100% rig.
  - **Resolved 2026-07-20.** The "at least one unaccounted cause" was two: the scrollable-bounds
    mismatch that made attempt 2's own area guard the proximate blocker, and the exclusive-corner
    bug in `physRectToLogical`. Note attempt 2's guards were self-defeating — the bogus-rect
    guard is precisely what discarded the correct node, and the containment guard then masked it
    by degrading to the window rect. The instinct to suspect "rects in an unreconcilable space"
    was half right: the space was fine, our mapping of it was not.
- ✅ **Hierarchy element detection** (`Features.md` §1, added 2026-07-20): in Element mode,
  the **wheel** steps outward to the parent element / back inward to the child, ending at the whole
  window. `IElementDetect` gained `elementChainAt` (innermost-first, frame-terminated) alongside the
  cheap `elementAt`. **The chain is resolved lazily on the first wheel step, never while hovering** —
  the MSAA `accParent` walk costs ~40ms per level, and building it per hover measured p50 59ms /
  p90 516ms / max 762ms on the GUI thread. Lazy resolution puts hovering back at p50 1ms / p90 2ms,
  with the one-off build (max 167ms) on a deliberate scroll. If that first tick ever needs to go,
  the remaining lever is the MTA worker described under the **hard timeout** won't-do below.
- ✅ Overlay wiring: `Tab` cycles Off→Window→Element; hovering highlights the detected region
  (dashed); a bare **click** adopts it; `C` copies the pixel color; `Shift` toggles HEX/RGB
  (magnifier readout updates live). Detection throttled (~30 ms) with a forced fresh query on
  click. Detect mode + `uppercase_hex_color` read from settings by `Application`.
- ✅ Magnifier reworked: odd-sized zoom window with an exact center
  pixel, a highlight box marking the sampled pixel (black halo + white core), and a larger
  two-line readout (coords + swatch + color) sized so RGB values don't clip.
- ✅ **Magnifier pass** (`ui/SnipScreenView.cpp::drawMagnifier`):
  a full translucent-blue crosshair through the loupe (with the hollow center-pixel marker), a
  centered panel attached directly under the loupe matching its width, showing coords, swatch +
  color value, and two key-hint lines ("Press **C** to copy color" / "Press **Shift** to switch
  RGB/HEX") with the action key bolded. New keys `snip.mag.copy_hint`/`snip.mag.format_hint`.

**Coordinate caveat (checked at sign-off):** logical↔physical mapping pairs each `QScreen` with its
`HMONITOR` by **physical size** (`logical×dpr`), since `QScreen::name()` is a friendly string on
Windows and never matches the GDI device name (the earlier name-pairing always failed → bad
`logical×dpr` fallback — see the monitor-pairing lead above). `physRectToLogical` additionally
picks the bottom-right monitor from the last *inclusive* pixel — mapping the exclusive corner as
a plain point resolves it to the neighbouring monitor on a boundary. Element bounds on the
mixed-DPI multi-monitor rig were part of the 2026-07-25 sign-off; keep this note as the
explanation for anyone touching `winmon`.

**Future enhancement — magnifier polish (deferred; enhancement, not a bug).** Stage 3 is functional
and signed off; the remaining ideas are uncommitted scope, tracked here so they aren't
mistaken for defects. Two of the three magnifier directions are **since done** (verified in
`ui/SnipScreenView.cpp::drawMagnifier` 2026-07-25): the **crosshair treatment** — a full
translucent theme-blue cross one source-pixel wide with `FlatCap` ends, plus the hollow
sampled-pixel marker — and a **simultaneous multi-format readout** that goes past the original
"RGB+HEX" idea: one label:value row per *enabled* format (all 7 available), with the
`Shift`-selected row carrying an accent plate to show which one `C` copies. Still open, and the
only unbuilt magnifier item: a **pixel grid** inside the loupe (zoomed pixels are crisp
nearest-neighbour blocks, but no grid lines are drawn between `kMagCell` cells). Also open:
smoother detection highlight and finer element parity. (The **HSV display** is since
**done** — `ColorPicker::Format` covers Hex/Rgb/Hsv/Hsl/Hsi/Cmyk/Dec, selectable via the Interface →
Magnifier "Color values" grid and cycled with `Shift`. Hierarchy scroll-parent/child
detection is **since done** too — see the `elementChainAt` bullet above.)

**Objectives:** smart auto-region snapping and the full color-picker workflow.

**Components & responsibilities:**
- `platform/win/WinElementDetect : IElementDetect`
  - `QRect windowAt(QPoint)` — top-level window via a z-order walk, skipping the overlay itself
    and invisible/DWM-cloaked windows.
  - `QRect elementAt(QPoint)` — **MSAA `IAccessible::accHitTest`** on the window that walk
    resolves, falling back to a UIA Control-View descent and then `windowAt`. (The original plan
    here — UIA `ElementFromPoint` → `CurrentBoundingRectangle` — was **abandoned**: that call is
    global and returned our own overlay in 207/207 samples. See the Status bullets above.)
  - `QRect elementChainAt(QPoint)` — the innermost-first, frame-terminated parent chain backing
    the wheel step-out/step-in, resolved lazily on the first wheel tick.
  - Detection results feed `SelectionModel::suggest(rect)`.
- `core/ColorPicker`
  - `QColor sampleAt(QPoint)` from the frozen desktop image; `QString format(QColor, Format)`
    for all 7 formats (Hex/Rgb/Hsv/Hsl/Hsi/Cmyk/Dec); `void copy()` puts text on clipboard; ties
    to paste-as-color-card (Stage 5, shipped — `core/ClipboardContent::renderColorCard`).
  - honors `[Misc] uppercase_hex_color`.

**Key bindings handled:** `Tab` cycle detection off→window→element (snip); `G` toggle aux
guide lines (moved off `Alt`, which is now the quick-mark modifier — Stage 4); `C` copy
color; `Shift` cycle color format (`Features.md §4 Snip only`).

**Data/settings (`[Snip]`,`[Misc]`,`[Interface]`):** `snip/detectMode=element`,
`misc/uppercase_hex_color=false`, and `interface/magnifier/formats` — a comma-joined list of the
**enabled** colour formats (saved by `PreferencesDialog::saveColorFormats`, cycled with `Shift`),
which replaced the single-value `snip/colorFormat=hex` this stage originally planned.

**Won't do (decided 2026-07-25, user's call):** `snip/excludeDetection` and `snip/crashesList` —
a per-process exclusion list for detection and a "known-crashy app" coarse-fallback list.
Neither has a plausible use case here, so they are dropped
rather than deferred; nothing in the code ever referenced them. Note the Control tab's **App
exclusions** list is a *different* feature — it mutes **global hotkeys** while a listed app is
foreground (`platform/win/WinGlobalHotkey`) and does not affect detection.

**i18n keys:** `snip.detect.window`, `snip.detect.element`, `snip.color_copied`,
`snip.color.format_hex/rgb`.

**Algorithms & notes:** detection runs on a throttled mouse-move (~30ms debounce) to stay
responsive, and the parent chain is built only on the first wheel tick (never while hovering).
**Won't do (decided 2026-07-25, user's call):** the planned **hard timeout** on a detection query.
There is no timeout in `WinElementDetect` — the `QElapsedTimer`s in it are diagnostics behind
`SNAPCAP_ELEMENT_DIAG=1`. The fallback chain (`accHitTest` → UIA descent → `windowAt`) covers a
*failed* query but not a *hung* one, so a genuinely unresponsive target would block the GUI
thread; accepted, since no hang has been observed in practice. If one ever shows up, the lever is
one dedicated MTA worker owned by `WinElementDetect` (Microsoft's advice for desktop-wide UIA
clients): coalesce hover requests to the newest, post results back to the GUI thread, and
discard any result older than the latest request or whose target `HWND` died. Prefer an
async request/callback API over a timed wait — a timed-out COM call keeps running and
accumulates stuck threads.

**Testing:**
- Unit: `tst_color` (RGB↔HEX↔HSV conversions, format toggling, uppercase option, 0–255 vs
  0–1 parsing parity with Stage 5 classifier).
- Manual checklist ✅ **run and signed off 2026-07-25**: hover highlights correct window;
  `Tab` switches to correct child element;
  auto-snap matches element bounds across Explorer/browser/IDE; in Element mode the **wheel**
  steps out to the parent element / back in to the child and stops at the whole window, and
  **hovering stays responsive** (the chain resolves only on the first wheel step, so a one-off
  pause there is expected — a pause while merely hovering is a defect); `C` copies exact
  pixel color; `Shift` cycles formats. (The "excluded process doesn't hang detection" row is
  moot — process exclusion is won't-do, see Data/settings.)

**Exit:** ✅ **met** — element detection snaps accurately on common apps without hangs; color
pick/copy/format round-trips; conversion unit tests green (`tst_color`).

**Deferred extras:** none left — hierarchy detection (scroll parent/child) and HSV display
were both since implemented (see the ✅ bullets above).

---

## Stage 4 — Annotation Engine  *(DONE — signed off 2026-07-25)*

**Status:** ✅ **complete and signed off by the user 2026-07-25.** Green in CI (repo-wide ctest
**29/29**), and the earlier blockers are closed: the **number label** tool (landed 2026-07-20,
`Features.md §2`) is visually signed off, and the standing flag that *"several annotation
features need enhancement and some have bugs"* is closed at sign-off — several of the suspects
were fixed along the way (the dead `Shift` 45° snap, the pin's invisible line control points,
the `Ctrl`/`Shift` draw-modifier rebind, and on-canvas shape editing).

A 2026-07-25 audit against the code closed the stage's paper gaps too: **last-colour memory**
and the **per-shape golden tests** were built (both verified — the colour memory by the user in
the live app), and five planned items were dropped as **won't-do** (last-*tool* memory, `1`/`2`
pen-width keys, on-canvas editing on pins, the `auto_select_on_creation` setting, and the
never-existent `tst_mosaic_blur` suite) — each recorded at its own bullet below.

*(Doc note: this section used to point at `Stage4-Annotation-Testing-Guide.md` for the manual
matrix + running issue log. That file was **deleted in `8812515`** and the pointer was left
dangling until this sign-off; the per-tool manual checklist below is what remains.)*

Polish candidates carried **past** sign-off — uncommitted scope, not blockers and not defects:
- **PaintBar placement/interaction** over the always-on-top fullscreen overlay (position when the
  selection hugs a screen edge or spans monitors; focus stealing so `Space`/draw keys go to a
  focused button instead of the canvas; modal `QColorDialog`/`QInputDialog` z-order vs. the
  topmost overlay).
- **Live redaction preview** (Mosaic/Blur) sampling per-view `m_s.shot` vs. the flattened result
  — verify they match, especially on HiDPI and across the multi-monitor / mixed-DPI seam that the
  (now-fixed) Stage 2 mixed-DPI bug also touched.
- **Drawing feel**: pencil/marker smoothing, arrowhead sizing, text anchor/baseline, eraser stroke
  width, hit-test slack — tune by feel.
- **Edit-mode entry/exit**: the old note here — "re-dragging a new selection silently discards
  the annotations; no on-canvas selection/handle UI" — is **out of date on both halves**
  (re-checked 2026-07-25). On-canvas editing shipped (see its own paragraph below). And the
  discard claim doesn't match the code: while editing, a click outside the region does **nothing**
  on a normal snip; only on a restored *historical* background does a drag start a fresh region
  (`ui/SnipOverlay.cpp:1344–1354`), and `m_doc` is never `reset()` on that path. Re-characterize
  this from a live test before recording it as a defect.

Built on the Stage 2/3 multi-monitor overlay:
- ✅ `core/annotation/Annotation` — geometry-based model in global-logical coords: `Style`
  (color+alpha, penWidth, fontPx), `ToolId`, a `create(tool, style)` factory, and concrete
  shapes **Rect, Ellipse, Line, Arrow, LineStrip, Pencil, Marker, Mosaic, Blur, Text,
  NumberLabel, Eraser**.
  Each does `paint(QPainter&, PaintCtx)` + `hitTest` + `bounds` + `translate` + JSON. A
  `PaintCtx{base, originLogical, scale}` lets the painter stay in global-logical coords while
  redaction tools sample original pixels; Eraser paints the clean base back over earlier
  annotations along its stroke.
- ✅ `core/annotation/AnnotationDocument` — ordered list, topmost hit-test, **QUndoStack** (Qt6
  Gui) commands Add/Remove/Move/Clear, deterministic `flatten(base, selectionLogical)`, and
  `toJson()/fromJson()`. Unit-tested (`tst_annotation_document`, 16 cases: counts, degenerate
  reject, undo/redo, clear-all undo, remove undo, move undo, z-order hit-test, JSON round-trip
  incl. Text + the mirrored-Text flag, flatten no-op/draws-shape/antialiases/**ignores-base-dpr**,
  the redact-brush round-trip + flatten below, the **number-label** centred-bounds/hit-test +
  JSON round-trip, and `reset()` dropping items **and** undo history).
- ✅ `core/render/Raster` — block-average `mosaic` + separable `gaussianBlur` (CPU; GPU
  path stays Stage 7). Unit-tested (`tst_render`, 6 cases: average, determinism, block-1/radius-0
  no-ops, uniform preserved, edge smoothing).
- ✅ `core/annotation/ShapeConstraints` — pure square/circle + 45°-line snap math.
  Unit-tested (`tst_shifted_shapes`).
- ✅ **Draw-modifier rebind (2026-07-21):** **`Ctrl`** now squares the rectangle-drag family
  (Rect, Ellipse, Mosaic, Blur, EraserRect, MarkerRect) — it was `Shift`, which is now a no-op
  for those tools. Aligns with the snip region's `Ctrl`-squares rule. Pencil/Marker keep `Shift`.
  The dispatch lives in one place, `ShapeConstraints::constrainedEnd(tool, anchor, cur, mods)`,
  called by **both** `SnipOverlay::updateDraft` and `PinEditController::updateDraft`, which
  previously carried duplicate copies that could drift.
- ✅ **FIXED (2026-07-21) — `Shift` 45° snap on line/arrow never worked.** The branch in both
  `updateDraft`s was **dead code**: the Line tool is routed through its own `handleLine*`
  state machine (`isLineTool()`), which never read modifiers, and `ToolId::Arrow` is never the
  active tool (the arrowhead is an `ArrowStyle` on the Line tool). Modifiers are now threaded
  into the state machine at all three points that position a vertex — the Building preview,
  the click-placed vertex, and the drag-release segment — so the preview and the committed
  line agree. Control-point drags snap only on a **single-segment** line, where the opposite
  end is an unambiguous anchor (user's call, 2026-07-21).
- ✅ **FIXED (2026-07-21) — pin line tool drew no control points.** `PinEditController::paintDraft`
  painted the draft shape but never the Controlling-phase vertex handles, so on a pin the
  control points were draggable but **invisible** (`lineControlPoints()`/the chrome pass existed
  only on `SnipOverlay`/`SnipScreenView`). Pre-existing, surfaced by the user while testing the
  45° snap. Now drawn in widget coords at a fixed 8px on-screen size, matching the overlay.
- ✅ `ui/PaintBar` — **unified icon toolbar** (reworked from the original text
  bar). Row 1: tool GROUP icon buttons (shapes / lines / pencil / marker / redact / text /
  eraser) ‖ undo · redo ‖ cancel · pin · save · copy. Row 2: the selected group's VARIANT
  buttons (rect/ellipse, line/arrow/linestrip) + pen-width spin + color board + custom color
  (alpha via `QColorDialog`). Icons load from `<exe>/icons/*.svg` (deployed by CMake from the
  top-level `icons/`; PNG until 2026-10-20); the bar is light so the black line-art icons read. This **absorbed the old
  per-monitor Copy/Save/Close toolbar** that lived in `SnipScreenView` (now removed).
  Tooltips/labels from `edit.*` keys (+ `edit.group.*`); i18n gate green.
- ✅ **Redaction sub-row** — the `redact` group swaps row 2 for a dedicated
  panel: **brush-size** (point.png + number, scroll to adjust) vs **rectangle-region** toggle
  (mutually exclusive application mode), a **blur↔mosaic type toggle** whose icon is a *live
  preview* generated by the real `render::mosaic`/`gaussianBlur` at the current degree
  (blockier/blurrier as it rises), and a custom **`RedactSlider`** degree control whose handle is
  **round for blur, square for mosaic**. Backed by new `ToolId::MosaicBrush`/`BlurBrush` (brush
  variants reuse the Eraser clip-to-stroke technique) and `Style.redactStrength` (decoupled from
  `penWidth`; both serialized). New keys `edit.redact.*`; unit-tested (`tst_annotation_document`
  redact-brush JSON round-trip + flatten, 14 cases).
- ✅ Overlay wiring: committing a selection enters edit mode and shows the PaintBar above it;
  picking a tool makes the mouse draw (drag for shapes, freehand for pencil/marker/eraser,
  an in-place `TextBoxEditor` box for text — I-beam preview, type live, corner-drag/scroll to
  scale the font+box about its centre, a **WYSIWYG rotate** handle that rotates the whole box +
  text live, auto-growing height for multi-line text, commit on right-click/click-outside/Esc and
  discard via the × button; a rich text sub-panel adds bold/italic/hollow + font family/size.
  **Committed** text is re-editable: with the Text tool, hovering a baked text box shows a dashed
  outline and clicking it pulls the `TextShape` back into a `TextBoxEditor` (restoring text +
  style + box + rotation) for another edit). `Ctrl` squares the rectangle-drag family and
  `Shift` constrains pencil/marker + the line's 45° snap (see the 2026-07-21 rebind bullet above
  — `Shift` is a **no-op** for the rect family); `Space` toggles the
  PaintBar; `Ctrl+Z`/`Ctrl+Y`/`Ctrl+Shift+Z`
  = undo/redo/clear. Views render the document + in-progress draft clipped to the selection;
  `copy`/`save` flatten annotations onto the composited image.

**On-canvas editing of an existing shape — done**: the
**Text** box has in-place WYSIWYG scale/rotate handles via `TextBoxEditor` and re-opens on
hover-dashed-outline + click; and the **non-text** rectangle-drag shapes (Rect/Ellipse/Mosaic/
Blur/MarkerRect) are now the same — hover shows a shape-matched dashed outline, a click selects
(8 grips + a spin handle), and the shape can be moved / resized (opposite grip pinned, in the
box's un-rotated frame) / **rotated** / restyled (paint bar) / deleted (`Del`), each one undo
step. Base2P gained rotation + `outlinePath()`; the document gained `ReshapeCmd`/`RestyleCmd`
(`reshapeApplied`/`restyle`), all unit-tested in `tst_annotation_document`.

**Won't do — on-canvas editing on *pins* (2026-07-25, user's call).** All of the above is
**snip-only**: `ui/PinEditController` has no selection state at all (no hover outline, no grips,
no reshape/restyle/rotate — its API is draw-only). Pins stay draw-only by design; a committed
shape on a pin is not re-editable. This also closes the `auto_select_on_creation` question
below, since there is no pin-side selection to enter.

**Deferrals:** none left — **per-shape golden-image references shipped 2026-07-25**
(`test/tst_annotation_golden`, see Testing).

**Objectives:** full editing toolbar on a snip (and re-enterable on a pin), with undo/redo,
a serializable model, and golden-tested rendering.

**Components & responsibilities:**
- `core/annotation/` — model:
  - `Annotation` base (id, z, bounds, style{color+alpha, penWidth, …}). `ToolId` has **16**
    values: `Rect`, `Ellipse`, `Line`, `Arrow`, `LineStrip`, `Pencil`, `Marker`, `MarkerRect`,
    `Mosaic`, `Blur`, `Text`, `Eraser`, `EraserRect`, `MosaicBrush`, `BlurBrush`, `NumberLabel`
    (+`None`). Each implements `void paint(QPainter&, const PaintCtx&)` and
    `bool hitTest(QPoint)`. Note `Arrow` is never the *active* tool — the arrowhead is an
    `ArrowStyle` on the Line tool.
  - `AnnotationDocument` — ordered list, hit-testing, `QUndoStack`-based **command stack**
    (`AddCmd`/`RemoveCmd`/`MoveCmd`/`FontSizeCmd`/`ReshapeCmd`/`RestyleCmd`/`SetTextsCmd`/
    `ClearCmd`); `QImage flatten(const QImage& base, const QRect& selectionLogical)`;
    `toJson()/fromJson()` for pin re-edit + session restore (Stage 6).
- `ui/PaintBar : QWidget` — tool selector + color board + bigger custom-color button (sets
  **alpha/translucency**) + pen-width control. Buttons map to a `ToolId`; labels from
  `edit.tool.*`. Shown/hidden by `Space`.
- `core/render/Raster` — `Mosaic` (block-average) and `GaussianBlur` (separable kernel) over
  the region beneath the shape; CPU now, **GPU path in Stage 7 for `GaussianBlur` only** behind
  the same call (`Mosaic` measured faster on CPU, so it stays there).
- Interaction controllers (in overlay): drawing state machine per tool; **shifted shapes**
  (`Shift` → square/circle, line/arrow→horizontal/orthogonal/45°, pencil→straight); text
  scale (corner drag) / rotate (top dot); **quick-mark** — with a rect-family tool,
  `Alt`+hover previews the Stage 3 detected element as a dashed box (`SnipOverlay::
  refreshQuickMark`, drawn unclipped by `SnipScreenView` so overflow past the region shows)
  and `Alt`+right-click fills it (`applyQuickMark`, one undo step). A modifier is required
  because a plain right-click already aborts; `Alt` over `Ctrl` because `Ctrl` already
  squares the rect tools mid-drag. Detection ignores the `Tab` detect mode. To keep `Alt`
  single-purpose the **aux-line toggle moved to `G`**; `nativeEvent`'s `WM_SYSKEYUP` hook (needed
  because Windows eats a lone Alt release) now calls `onAltReleased()` to drop the preview.

**Key bindings handled (`KeyContext::Edit`):** `Space` toggle paint bar; right-click finish
shape; **scroll** pen width; `Ctrl+Z` undo; `Ctrl+Y` redo;
`Ctrl+Shift+Z` clear all (`Features.md §4`). Free re-edit = undo-until-gone then redo.
**Won't do (2026-07-25, user's call):** `1`/`2` as pen-width keys — scroll and the width box are
enough, and the digits are already claimed by number-label renumbering.

**Data/settings — group `[edit]`, not the planned `[Annotate]`:** nothing has ever lived under
`annotate/`. The real keys are `edit/width_<tool>` (**per-tool** remembered pen width, better
than the planned single `annotate/penWidth`), `edit/label_size` (number-label badge size), and
`edit/last_color` (**added 2026-07-25** — the last colour picked, restored by
`PaintBar::setSettings` through `applyColor()` so the palette opens on it; both hosts inherit it
because they seed `m_style.color` from `PaintBar::currentColor()`).

**Won't do (2026-07-25, user's call):**
- **`annotate/lastTool`** — the tool is not remembered; every snip starts with no tool selected.
  Only the *colour* is remembered.
- **`annotate/auto_select_on_creation`** — no setting. A freshly drawn selectable shape is
  auto-selected **unconditionally** (`SnipOverlay::commitDraft`), so its grips appear without a
  second click; the planned opt-out key is dropped rather than implemented.

**i18n keys:** `edit.tool.rect/ellipse/line/arrow/linestrip/pencil/marker/mosaic/blur/text/
eraser`, `edit.color.custom`, `edit.penwidth`, `edit.undo/redo/clear`.

**Algorithms & notes:** annotations store geometry, not pixels, so they re-render on resize
and serialize compactly; redaction tools snapshot the base region at flatten time. Rendering
is fully deterministic for fixed input → golden tests.

**Testing:**
- Unit (high value, pure logic): `tst_annotation_document` (add/remove/z-order, undo/redo
  invariants incl. clear-all, hit-testing, JSON round-trip, single-pixel flatten assertions);
  `tst_render` — **6 deterministic property tests** of the raster primitives (block average,
  determinism, block-1/radius-0 no-ops, uniform preserved, edge smoothing), *not* a golden
  compare; `tst_shifted_shapes` (square/circle/45° constraint math).
- **`tst_annotation_golden` (new 2026-07-25)** — the per-shape golden-image compare. Flattens one
  shape at a time onto a code-generated 160×100 base and diffs against a committed PNG in
  `test/golden/` (read via `SNAPCAP_SOURCE_DIR`), tolerating a per-channel delta of ≤2 for
  antialiasing jitter; on failure it writes `<shape>_actual.png` beside the golden. 16 cases:
  rect hollow/filled, ellipse, line plain/solid-arrow/tapered-arrow, linestrip, pencil, marker,
  marker-rect, mosaic, blur, mosaic-brush, blur-brush, eraser, eraser-rect. Regenerate with
  `SNAPCAP_UPDATE_GOLDEN=1`. **Text and NumberLabel are deliberately excluded** — their output
  depends on font rasterization, which varies by machine/Qt build, so goldens for them would be
  flaky rather than protective; they keep their JSON/geometry coverage in
  `tst_annotation_document`. (The old plan named a `tst_mosaic_blur` suite; it never existed and
  is not needed — `tst_render` covers that ground.)
- Manual checklist ✅ **run and signed off 2026-07-25**: draw every tool; undo/redo +
  `Ctrl+Shift+Z`; pen width & color/alpha (incl. the colour surviving into the next snip via
  `edit/last_color`); line-strip vs single line; text scale/rotate; shifted shapes; on-canvas
  select/move/resize/rotate/restyle of a committed shape; the number-label badge; quick-mark
  (`Alt`+hover preview tracks elements, `Alt`+right-click marks, plain right-click still
  aborts, and `G` still toggles the aux lines).

**Exit:** ✅ **met** — all tools usable; the document serializes/undoes correctly; model **and**
golden tests green (`tst_annotation_document`, `tst_annotation_golden`, `tst_render`,
`tst_shifted_shapes`).

**Deferred extras:** magnify, rounded-rect, dashed, ellipse/circle brush, bidi/more arrows,
filled/outlined text, free-select, multi pen sizes, color-button reorder/opacity-scroll,
per-tool colors, sharable palette.
*Since shipped (removed from the list above):* **counting numbers** — the Number label tool,
2026-07-20, auto-numbering badge with three looks (`Features.md` already strikes it out);
**all-shape rotation** and **direct re-edit** — both landed with on-canvas shape editing.

---

## Stage 5 — Paste / Pin Floating Windows  *(DONE — signed off 2026-07-25)*

**Status:** ✅ **complete and signed off by the user 2026-07-25** — re-tested by hand and
everything behaved as expected, clearing the earlier "bugs found during manual testing" hold on
the GUI/Win32 pin pieces. Green in CI (repo-wide ctest 24/24 as of 2026-07-16); core fully
unit-tested. The section was re-verified against the code the same day — the ✅/deferral lines
below reflect that pass, and the surviving deferrals are enhancements, not defects.
*(Housekeeping: the old `Stage5-Paste-Pin-Testing-Guide.md` — ~70 manual cases plus the running
issue log — no longer exists in `claude-plan/`. Nothing depends on it now that the stage is signed
off; the checklist under **Testing** below is the remaining record.)*

🐛 **Post-sign-off crash fix (2026-07-29) — use-after-free tearing down animated pins.**
Found by the Stage 7 50-pin memory measurement (see that stage's Testing block), which crashed
its own GIF scenario. `PinManager::destroy()` frees the model as soon as it returns from emitting
`removed()`, but the handler in `Application::wirePins()` only did `hide(); deleteLater();` — and
`PinWindow` has **no `hideEvent`**, so a hidden pin's animation clock kept ticking and called
`m_model->advanceFrame()` on freed memory. Probabilistic by design (it needs a tick to land in
that gap), so it scaled with the number of *playing* pins: **0/3 runs at ≤4 animated pins,
1/3 at 6, 3/3 at 8, 3/3 at 10** — always `0xC0000005`. Never with stills (their clock is idle),
never when hidden first (`updatePlayback()` had already stopped the clock), and never on
**close**-all, because `close()` moves the model into the recover ring where it stays alive —
which is what identified the freed model as the cause rather than the window teardown.
Fixed by `PinWindow::detach()`, called before `hide()`/`deleteLater()`: it stops both clocks,
drops the external connections, deletes the toolbar, leaves `s_windows` and nulls `m_model` (see
the `ui/PinWindow` row in `Architecture-FileMap.md`). The window is still `deleteLater()`d — a
synchronous `delete` is *not* an option, because the pin's own context menu is one of the callers
of `m_manager->destroy(m_model)`. Verified 0 crashes in **16 runs** (8×10 pins, 3×8, 5×6) against
a matrix that previously failed 7/9, plus close-all → `F3` recover → destroy-all still clean for
both still and animated pins, and ctest 29/29. No unit test: the bug is in `ui/` and needs an
interactive desktop, so it can't be a `ctest` case.
- ✅ `core/ClipboardContent` — `classify(QMimeData, PasteOptions)` → Image / Color / PlainText /
  Html / FilePaths, plus `parseColor` (HEX, `r,g,b` ints 0-255, three 0-1 floats — a `.` makes
  the triple float-valued) and `renderText` / `renderColorCard` / `renderFilePaths`. A
  file-path clipboard with `pasteFilePaths` off is ignored. Unit-tested (`tst_clipboard_content`,
  11 cases; runs under a `QGuiApplication` main for the font database).
- ✅ `core/PinModel` — rotation/flip/scale/opacity + **grayscale/invert** state + an owned
  `AnnotationDocument` (source-px geometry, baked by `annotatedSource()`), plus deterministic
  `compose()` (colour ops → annotations → rotate→flip→scale; opacity is window-level),
  clamping, `reset()`, `setSource()` (Ctrl+V), `setMaxLongSide()`, `sourceToComposed()` and the
  two-level smooth-scaling override. Unit-tested (`tst_pin_model`, 28 cases).
- ✅ `core/PinManager` — owns active pins + the bounded recover ring (close→recoverable,
  oldest evicted past max; destroy→permanent; `recoverOldest()` = FIFO), hide-all, and magnetic-snap
  math; emits `added()/removed()/hiddenChanged()` so the app layer drives window lifecycle
  (keeps core widget-free). Unit-tested (`tst_recover_ring`, 6 cases incl. signal emission).
- ✅ `platform/win/WinPinSurface : IPinSurface` — always-on-top (`HWND_TOPMOST`), click-through
  (toggle `WS_EX_TRANSPARENT` on Qt's already-layered translucent window), opacity. New
  `platform::createPinSurface(WId)` factory (declared in `platform.h`, built in `WinPlatform.cpp`).
- ✅ `ui/PinWindow` — frameless `WA_TranslucentBackground` always-on-top window rendering
  `compose()` (devicePixelRatio-aware); pin keys `1/2` rotate, `3/4` flip, `+/-`+scroll scale,
  `Ctrl±`/`Ctrl+scroll` opacity, middle-click reset, `Esc`/dbl-click/`Ctrl+W` close (recoverable),
  `Shift+Esc` destroy, `Ctrl+V` replace, `Ctrl+Shift+C` copy-text, `Ctrl+Shift+P` prefs, arrows
  nudge, `Shift`-drag magnetic snap, `Shift+dbl-click` thumbnail. A static window registry backs
  snapping / hide-all / click-through-under-cursor.
- ✅ App wiring: `F3` (`doPaste`) prioritises (1) a *freshly-copied* clipboard item (a change since
  the last press, tracked via `QClipboard::dataChanged`) — pasted once, ahead of recovery; then
  (2) recovers closed pins **oldest-first** until the ring drains (so Close/Close-All is undone by
  Paste); then (3) re-pastes the clipboard. `Shift+F3` toggles hide-all;
  click-through toggle resolves the pin under the cursor (else makes all clickable again);
  `Ctrl+T` / middle-click in the snip overlay pins the flattened selection via a pin-sink callback.
- ✅ **Pin placement + neon outline (post-sign-off polish, 2026-06-28):** a snip pinned via
  `Ctrl+T`/middle-click now reappears **exactly over its capture spot** (the pin-sink threads the
  capture rect; `Application` anchors via `PinWindow::showWithContentAt`), not under the cursor;
  clipboard `F3` paste still centers under the cursor. Each pin draws a **neon outline** in a
  `kMargin` (16px) transparent border (no image pixel covered; the margin also hosts the
  unfocused **drop shadow**): a one-shot **rainbow sweep** on
  creation (neon ROYGBIV, 0.1s/colour) settling to `PinWindow::themeColor()` (neon-cyan
  **placeholder**, single source w/ `TODO(theme)` until Stage 7 themes — not hard-coded);
  `themeColor()` while focused/selected/dragging, neon grey while unfocused.

**Deferrals — still open (verified absent in code 2026-07-25; enhancement, not defect):**
- **Interactive region-thumbnail.** `PinWindow::toggleThumbnail()` only fits the whole image into
  the `paste/display/thumbWidth`×`thumbHeight` box (160 fallback) and back; there is no
  drag-a-region-to-show interaction. (*free-size thumbnail* below is the same gap.)
- **GIF frame stepping.** Playback itself shipped (see below); the animation menu offers only a
  **Pause** checkbox — no step forward/back.
- **`[Paste] include_active_window_for_multiselection`.** Named in the `PinManager` bullet below;
  **no code references it at all** — the setting does not exist.
- **Magnetic-snap threshold is not user-configurable.** `PinManager::m_magnetic` is a hard-coded
  `20` px default and `Application` never overrides it (`app/Application.cpp:401` says so
  outright) — the `paste/magneticThreshold` key below is unimplemented.
- **`test/manual/layered_window`** (the `WinPinSurface` isolation harness in *Testing* below) was
  never built — `test/manual/` does not exist.

✅ **Since wired:** on-pin annotation editing via `Space` / the menu's **Show toolbar** —
`ui/PinEditController` drives `ui/PaintBar` (`Host::Pin`) over the `PinModel`'s own
`AnnotationDocument` (one pin at a time; annotations persist through `SessionStore`) — and the
**grayscale `5` / invert `6`** colour ops (`PinModel::toggleGrayscale`/`toggleInvert`,
also checkable items on the pin menu's Process submenu). **Rich HTML paste** is wired too —
`ClipboardContent::renderHtml` renders source markup via `QTextDocument` when the clipboard has
HTML and `PasteOptions::ignoreHtml` is off (only the Preferences Paste-tab **live preview** stays
on the plain-text path).
✅ **Two former deferrals have since shipped** (they were still listed as deferred until the
2026-07-25 re-verification):
- **Animated GIF playback** — **not** `QMovie`-backed as planned: `core/codec/AnimatedImage`
  (`anim::load`) decodes frames + per-frame delays with a size guard (`Status::TooLarge` warns
  through `PinWindow::attachAnimation`), `PinModel::setAnimation`/`isAnimated` hold them, and
  `PinWindow::updatePlayback()` drives a single-shot timer re-armed per frame. Playback freezes
  while the pin is being annotated and resumes after, and pauses when hidden.
- **`Ctrl`-click multi-select UI** — shipped; see the **Multi-select (done)** paragraph below.

✅ **FIXED + verified manually (2026-07-21) — mixed-DPI pin sizing.** The manual check this caveat
asked for found a real defect: dragging a pin across a mixed-DPI seam intermittently left the image
**visibly cropped**, self-correcting on any resize. Cause: `refreshFromModel()` is the only thing
that sizes the window from the composed image, and nothing called it on a monitor change —
`moveEvent`/`resizeEvent` only reposition the edit bars and `changeEvent` handled `ActivationChange`
only, so Windows resized the window for the new DPI and the stale image extent got clipped by it
("sometimes" = Windows only switches DPI once the majority of the window crosses). Fixed by
recomposing on **both** `QEvent::DevicePixelRatioChange` and `QWindow::screenChanged`, **deferred**
through `QTimer::singleShot(0)` — inline recomposition was overwritten by the rect Windows applies
around the notification, which is why the first attempt fixed only the low→high direction. Pin
**sizing** was made monitor-independent at the same time: `PinModel::sourceDpr` is now **latched**
(clipboard pins latch the creation monitor's dpr in `showAt()`, from the *destination* screen since
the first compose predates `move()`) instead of re-reading the live screen dpr, so a clipboard pin
now behaves like a snip-sourced one and holds its apparent size across the seam. Verified by hand
on a two-monitor mixed-DPI setup in both crossing directions, snip- and clipboard-sourced. Per the scope policy above, the remaining extras
(crop, background modes, topmost/solo toggles, web-image paste, editable color cards) are
**backlog, not exclusions**.

**Objectives:** pin clipboard content as always-on-top windows with full manipulation,
recovery, and click-through.

**Components & responsibilities:**
- `core/ClipboardContent`
  - `enum Kind { Image, Color, PlainText, Html, FilePaths, None }`.
  - `static PinContent classify(const QMimeData&, const PasteOptions&)` per the `Features.md` §3
    paste rules: image; color (RGB three 0–255 ints **or** three 0–1 floats, or HEX `#…`); text
    (plain/HTML); file paths (image→image; else path→image; `paste-file-paths` can be off).
  - `QImage renderText(...)`, `renderHtml(...)` (force-wrap to `[Paste] force_html_wrap`,
    `max width`), `renderColorCard(QColor)`.
- `ui/PinWindow : QWidget` — frameless, `WA_TranslucentBackground`; holds a `PinModel`
  (image, rotation, flipH/V, scale, opacity, group id) + an optional `AnnotationDocument`
  for re-edit (`Space`). Renders via `PinModel::compose()`.
- `platform/win/WinPinSurface : IPinSurface` — `setAlwaysOnTop`, `setOpacity`,
  `setClickThrough` via `WS_EX_LAYERED | WS_EX_TRANSPARENT` (and `UpdateLayeredWindow` for
  true per-pixel alpha when Qt's flags are insufficient).
- `core/PinManager` — owns all pins; the **recover ring** (closed pins recoverable by paste
  hotkey; max from `paste/behavior/maxRecoverable`, **default 10**), destroy (permanent),
  hide-all/show-all,
  select-all/multi-select, **magnetic snapping** (align when border within
  `[Paste] magnetic_attach_threshold` — *shipped as a fixed 20 px default, not a setting*), and
  `[Paste] include_active_window_for_multiselection` (*not implemented*).
- GIF: playback with frame stepping — *shipped as `core/codec/AnimatedImage` playback + a Pause
  toggle, no `QMovie` and no frame stepping*.

> ✅ **Pin groups (implemented post-S6).** Named groups landed as a standalone
> `core/GroupRegistry` (id↔name + a set of shown ids, `tst_group_registry`) rather than inside
> `PinManager`. Model: a pin carries one group id in `PinModel::group()` (0 = ungrouped, always
> visible); **any number of groups can be shown at once**. Live windows = ungrouped + every shown
> group's pins. `Application` orchestrates it — the ungrouped set persists in `session/`, each
> group in `session/groups/<id>/` (both via `SessionStore`), the registry in `session/groups.json`.
> Showing a group loads its pins from disk and hiding one serializes them back (via
> `PinManager::destroy`, bypassing the recover ring), each group independent of the others;
> assigning a pin to a hidden group serializes+closes it; deleting a group discards its pins.
> UI: tray **Group** submenu (**independent checkboxes**, one per group — not radios) +
> right-click **Move to group** + `ui/GroupManagerDialog` (New/Rename/Delete + pin counts).
> No group hotkeys.

**Key bindings handled (`KeyContext::Pin`, per `Features.md §4`):** `1`/`2` rotate; `3`/`4`
flip; `5`/`6` grayscale/invert; scroll/`+`/`-`/border-drag scale; `Ctrl+scroll`/`Ctrl±` opacity; middle-click reset;
`Shift+dbl-click` thumbnail; `Esc`/dbl-click/`Ctrl+W` close (+recover); `Shift+Esc` destroy;
`Shift+F3` hide-all; `Ctrl+A` select-all; `Ctrl+V` replace; `Ctrl+Shift+C` copy text;
`Ctrl+Shift+P` preferences; `Space` re-edit; `Shift`-drag magnetic snap.
Plus the global click-through toggle ("no window under cursor → all unclickable become
clickable", `Features.md §3`).

**Multi-select (done):** `Ctrl`-click toggles a pin into an amber-outlined selection; `Ctrl+A`
selects every visible pin. Any non-clipboard operation on a selected pin fans out to the whole
selection (`PinWindow::opTargets()`); dragging a selected pin group-moves the rest; a plain
click on any pin (no drag) deselects all, leaving just that pin focused. Held per window
(`m_selected`), so no registry wiring and `Pin_SelectAll` stays dormant.

**Data/settings — as actually shipped** (the keys are grouped `paste/display/*`,
`paste/behavior/*`, `paste/conversion/*`, not the flat names this plan originally listed):
- `paste/display/` — `windowShadow`, `shadowColor`, `mnemonics`, `defaultOpacity`,
  `maxWindowSize`, `thumbWidth`, `thumbHeight`.
- `paste/behavior/` — `maxRecoverable`, `useSnippingPosition`, `zoomAtMouse`, `smoothScaling`,
  `resetOnThumbnail`, `translucentWhenFlashing`, `flashOnGroupSwitch`, `confirmCloseMultiple`,
  `confirmDestroyMultiple`, `pasteFlash`, `pasteActivate`.
- `paste/conversion/` — `textToImage`, `ignoreHtml`, `neverPasteFilePaths` (the inverse of the
  planned `pasteFilePaths`), `pageMargin`, `maxWidth`, `font`, `bgColor`, `textColor`.
- **Planned but never created:** `paste/magneticThreshold` (hard-coded 20) and
  `paste/force_html_wrap` (HTML wrapping rides `conversion/maxWidth`).

**i18n keys (as shipped in `lang/en.yaml`):** `paste.menu.*` is the bulk of it (38 leaves —
copy_image, save_image_as, show_toolbar, pause, shadow, the `zoom_*` presets + zoom_smoothing,
the `image_processing` items incl. grayscale/invert, paste, replace_by_file, move_to_group /
group_ungrouped, view_in_folder, close, destroy, and the read-only `info_*` rows), plus
`paste.save_title`, `paste.replace_title`, `paste.hint.replaced`,
`paste.confirm.close_multiple.*` / `destroy_multiple.*`, `paste.anim.too_large.*` and
`paste.color_card`.

**Algorithms & notes:** `compose()` applies color ops→annotations→rotate→flip→scale in a fixed
order for predictable output (opacity is applied by the window, not baked in); recover ring is a bounded deque of detached `PinModel`s. The
click-through hotkey resolves the pin under the cursor via hit-testing in screen space.

**Testing:**
- Unit: `tst_clipboard_content` (classify each MIME kind; color parsing for ints/floats/HEX
  shares the Stage 3 path; text/HTML→image non-empty + wrap width); `tst_recover_ring`
  (close/recover/destroy counting honors max; destroy is permanent); `tst_pin_model`
  (rotate/flip/scale/opacity compose order & reset; grayscale/invert keep alpha, spare
  annotations, and round-trip).
- ❌ Manual harness `test/manual/layered_window` (a layered click-through window with scroll
  opacity, to validate `WinPinSurface` in isolation) — **never built**; `test/manual/` doesn't
  exist. `WinPinSurface` is only exercised through the app.
- Manual checklist ✅ **run and signed off 2026-07-25**: pin image/color/text/HTML/file-path; all
  transforms; click-through toggle + fallback; close→recover; destroy stays gone; `Shift+F3`
  hide/show; magnetic snap; GIF animates; re-edit via `Space`; multi-monitor placement.

**Exit ✅ met:** a pinned image is always-on-top, fully transformable, click-through-toggleable,
and recoverable after close — across monitors; classify + ring + model tests green.

**Backlog** *(re-checked 2026-07-25 — none of these are present; the pin context menu has
copy/save/toolbar/pause/shadow/zoom/processing/paste/replace/group/view-in-folder/close/destroy/info
and nothing else)*: crop, free-size thumbnail, background modes (transparent/chessboard), topmost
toggle, solo mode, web image paste/drag, editable color cards. Mirrored in
`Unimplemented-Features.md` §3. *(`Ctrl`-click multi-select UI was removed from this list — it's
**done**; group management UI is **done** — `ui/GroupManagerDialog`; grayscale `5`/invert `6` are
**done** — `PinModel`.)*

---

## Stage 6 — History, Backup/Restore & Preferences UI  *(DONE — signed off 2026-07-26)*

**Status:** ✅ **complete and signed off by the user 2026-07-26.** Green in CI (repo-wide ctest
29/29 at sign-off) and manually verified by the user against the **Testing** checklist at the foot
of this stage. Nothing is left open: both former deferrals (re-editable replay, the QuaZip session
archive) are **won't-do**, and the magnifier-styling item was **closed the same day** with
the Grid lines / Coordinates / Magnifier-size controls. *(The ~50-case `Stage6-Testing-Guide.md`
that used to back this stage was **deleted in `8812515`** and its content was not moved anywhere;
the checklist below is the surviving record of what sign-off covers.)* All three pillars are in
place:
- ✅ `core/History` — bounded ring of the last *N* successful snips; each `record()` persists the
  cropped PNG + a 96px thumbnail **and the full-screen freeze it was cut from** (one
  `snip_<id>_full<k>.png` per monitor + a `snip_<id>.json` sidecar with the selection rect +
  per-monitor geo/dpr) under the history folder (newest-first, oldest evicted past max incl. its
  freeze/sidecar files, survives restart by re-discovering files on disk); `prev()/next()` cursor
  + `screensAt(i)`/`selectionAt(i)` back the in-snip `,`/`.` replay, which now **restores the past
  desktop across all monitors and re-selects its area** so a past screen can be
  re-snipped even though it can't be reproduced. *Full-screen PNGs make each entry much larger on
  disk — the `snip/behavior/maxHistoryRecords` cap (default 20) still governs the ring.* Pure →
  unit-tested (`tst_history`, 8 cases: prepend order, max eviction, persistence across reopen,
  max-0 disables, replay-cursor walk, clear, full-screen round-trip, eviction removes freeze files).
- ✅ `core/SessionStore` — backup/restore of the open pins to a folder: a `session.json`
  manifest + one sidecar PNG per pin, capturing pixels + `PinModel::toJson()` transform state +
  window geometry + (optional) annotation JSON. `save()` clears stale sidecars first; `load()`
  round-trips losslessly. Pure → unit-tested (`tst_session_store`, 4 cases incl. transform +
  geometry round-trip, empty-save clears, stale-sidecar cleanup). New `PinModel::toJson/fromJson`
  (transform/group/text, clamped on load) + `PinManager::adopt(unique_ptr)` to re-surface a
  restored model. **This resolves the Stage 5 deferral where a restored pin lost its
  position** — restored pins reopen at their saved geometry.
- ✅ `ui/PreferencesDialog` — **redesigned (2026-07-01) from a `QTabWidget` into a flat-light
  "settings page"**: a branding header (logo + "SnapCap") over a left **sidebar** of the 8
  categories ‖ an optional (currently hidden) **sub-category column** ‖ a stacked content area of
  **rich rows** (bold label + helper description, control on the right, dividers) inside cards.
  Styled by the project's **first QSS theme**, `res/preferences.qss` (accent `#2d7dff`, loaded at
  runtime, deployed by the existing `res/` copy step). The sub-category column is wired generically
  (`Category{subKeys,stackIndices}`) but stays hidden until a category declares >1 sub-category.
  All behavior is preserved (live-persist, language live-switch, config relocation, hotkey
  conflict detection, Restore Defaults). New i18n keys `prefs.subtitle.*` / `prefs.desc.*` /
  `prefs.nav_header` (en only — user owns translations). A throwaway visual harness lives at
  `tools/prefs_demo.cpp` (built under `-DSNAPCAP_BUILD_DEMOS=ON`).
- ✅ **Interface tab built out (2026-07-02)** — its two subcategories render **inline** as
  `#SectionTitle`/`#SectionDesc` headings (new `addSectionHeader`) each above their own card:
  **General** (interface font → the project `ui/FontPickerDialog`, which lists families under their
  **native** name via `core/FontNameTable` — it replaced `QFontDialog` here and at the Paste
  conversion font; theme color → project `ColorDialog`; tray-icon Color/Custom radios — the
  original **Auto** mode was retired and any stored `auto` migrates to `color` on load) and **Magnifier** (a "Color values" checkbox grid persisting
  `interface/magnifier/formats` = RGB/HEX/HSL/HSV/HSI/CMYK/Dec, a divider, then `No '#'` /
  `Show HEX values in uppercase` / `Usage tips`). All controls persist to `interface/*` Settings;
  the **Restore Defaults** button here is **page-scoped** (`restoreInterfaceDefaults()` resets
  only `interface/*`). ✅ **Since wired (was deferred to Stage 7):** the chosen font applies
  app-wide via `Application::applyInterfaceFont()`, the theme colour drives the QSS dialogs
  (`ui/PrefsStyle`), `ui/PaintBar` and the snip overlay, the tray-icon mode drives `ui/TrayIcon`
  (**incl. Custom**: the swatch beside the radio opens `ColorDialog`, persists
  `interface/trayIconColor` and selects Custom; `TrayIcon::iconForMode()` tints the icon with it),
  and the magnifier readout consumes
  `interface/magnifier/*` via `SnipOverlay::applyMagnifierSettings()`. New i18n keys under
  `prefs.iface.*` (en only).
- ✅ **Snip page fully built** (2026-07-03): three inline subcategories **Display / Behavior /
  Keys** + an **Actions** group, in the same card/section idiom. All controls persist under
  `snip/*`. They were originally **all deferred** (UI + persistence only) bar the **Auto detect
  windows** + **Auto detect UI elements** pair (which derives `snip/detectMode`, consumed by
  `Application` → `SnipOverlay`); ✅ **most are now wired** — the Display border/mask/anchors +
  `crossLines`/`noAuxLines`/`keyTips`, the Behavior `hide1x1`/`quitOnFocusLoss`/`captureCursor`
  + the history trio, the Keys `disableScroll`/`confirmEsc`, and the double/middle/Enter action
  combos, all read via `SnipOverlay::applySnipSettings()`. The Keys `ignoreSimCopy` +
  `ignoreWinLeft`/`Right` are wired through the snip-time LL keyboard hook
  (`platform::IKeyboardFilter` / `platform/win/WinKeyboardFilter`). The former `hdrAdjust`
  checkbox was **removed** (GDI capture already returns OS-tone-mapped SDR); nothing on the Snip
  page is deferred now. (The `Deferred-Settings.md` ledger this used to point at was **retired**
  in `1249ee2` once every Preferences page was wired; the surviving ledger is
  `Unimplemented-Features.md`, which tracks missing **`Features.md` items** rather than unwired
  controls.)
  Page-scoped **Restore Defaults**
  (`restoreSnipDefaults`/`refreshSnipControls`). New i18n keys under `prefs.snip.*` +
  `prefs.desc.snip.*` (en only); the old `prefs.detect_mode`/`prefs.capture_cursor` flat keys
  were removed. *Original tabbed dialog notes:*
  Tabs mirror `Features.md §5` (General/Snip/Paste/Control/Annotate/Interface/Output/Misc/Log). The
  **Language** dropdown (auto-populated from `lang/*.yaml` display names) drives
  `I18n::setLocale` → the whole dialog + tray retranslate live. The **Control** tab has a
  `QKeySequenceEdit` per global hotkey with **reset-to-default** + **conflict detection** (a
  clashing binding isn't persisted; a red hint shows); edits emit `hotkeysChanged()` so the app
  unregisters + re-registers the Win32 hotkeys live. Folder pickers (history + quick-save),
  restore-session toggle, format defaults, and the misc/log knobs all read/write `Settings`
  live. Every label/tab title is an i18n key (`prefs.*`, added to en/zh_cn/zh_tw; gate green at
  78 keys × 3).
- ✅ **Output page fully wired** — `output/imageQuality` + `output/manualSave/fileName` feed
  `SnipOverlay::save()`/`quickSave()` via `core/FilenameTemplate` (`$…$` date/time tokens, `%…%`
  env vars, and the `%title%`/`%title:N%` foreground-window-title token from
  `platform::foregroundWindowTitle()`) + `core/ImageExport::toFile` (incl.
  `output/auto_alpha_channel_removal` and the remembered Save-dialog extension). Quick Save and
  Auto Save write silently to their expanded path templates (toast via `TrayIcon::notify`), and
  the clipboard trio (`dibScreenshot`/`dibImageWindows`/`copyAsFile`) is honoured by
  `platform::IClipboard` (`platform/win/WinClipboard`). Unit-tested by `tst_filename_template` +
  `tst_image_export`. Nothing on this page is deferred. The page's foot carries a **Naming
  Rules** button opening `ui/NamingRulesDialog` — a read-only reference of the token vocabulary
  above (env vars · `%title%` specials · `$…$` date/time), built to `KeyBindingsDialog`'s grammar
  so the two popups stay in lockstep.
- ✅ App wiring: `Application` builds `History` (folder from `general/historyDir`, else a
  `history/` beside `config.ini`), records every successful snip (copy/save/pin) through it, and
  serves `,`/`.` replay; restores the last session's pins on launch (gated by
  `general/restoreSession`) and persists the open set on `aboutToQuit`; `OpenPreferences` now
  opens the real dialog; a settings `changed()` handler applies max-history / pin limits live.
  Added `IGlobalHotkey::unregisterAll()` (Win backend) for the live remap path.
- ✅ **In-snip visual history strip** — the `,`/`.` replay opens an **in-overlay thumbnail row**
  (`core/HistoryStrip` windows it to fit the screen; pure → `tst_history_strip`) anchored
  bottom-centre of the screen under the cursor and each landed entry **restores
  its whole frozen desktop across all monitors and re-selects the finalized area**
  (`SnipOverlay::applyHistoryFrame` + `SnipScreenView::setBackground`), so the past screen can be
  re-snipped; `.` past the newest returns to the live freeze. Starting a fresh drag dismisses the
  strip so it doesn't cloud the view; `,`/`.` move the highlight and Enter copies the highlighted
  crop and closes, while a click on a thumbnail picks that one directly
  (`SnipOverlay::handleStripClick`); Esc/right-click closes just the strip.

**Won't do (2026-07-26, user's call):** both of this stage's former deferrals are now **dropped,
not deferred** — (1) *re-editable replay* (loading a recalled snip back into the annotation
editor): the history strip **copies-to-clipboard on pick and closes**
(`SnipOverlay::handleStripClick` → `pickHistoryStrip`) and that stays the behaviour; (2) the
*QuaZip session archive*: `SessionStore` stays an **on-disk folder** permanently, so QuaZip is
not a dependency of this project at all (it is also struck from `Tech-Stack-Plan.md`'s dependency
+ licence lists). *(The Interface tab's font/theme/tray-icon application + the magnifier's
color-format consumption were listed here as Stage 7 work — both are **now wired**, the Custom
tray icon included, so nothing survives from that note.)* **✅ Verified manually (2026-07-21):** restored pin geometry is saved/restored in logical px from
`frameGeometry()` (`Application.cpp` save → `SessionStore` → `showAt()`), and the logical-px
round-trip holds across a mixed-DPI seam — two pins placed on two different-DPI monitors both came
back at the **same position and the same size** after a quit + relaunch. Per the scope policy
above, this stage's deferred extras are **backlog, not exclusions**.

**Objectives:** session persistence and the full settings surface (including live language
switch wired through real Preferences).

**Components & responsibilities:**
- `core/History` — bounded ring of last *N* **successful** snips (`Features.md §1`); `,`/`.`
  replay during snipping; thumbnails persisted under `general/historyDir`.
- `core/SessionStore` — **auto backup & restore** of open pins across restarts: serialize
  each `PinModel` (+ annotations via `AnnotationDocument::toJson`) + geometry/group; restore
  on launch. On-disk: **a folder, permanently** — the once-planned QuaZip archive is **won't-do**
  (2026-07-26).
- `ui/PreferencesDialog` — tabs mirroring `Features.md §5`: **General, Snip, Paste,
  Control/Hotkeys, Annotate, Interface, Output, Misc, Log**. Includes:
  - **Language dropdown** auto-populated from `lang/*.yaml` (reads `meta.name`); selection →
    `I18n::setLocale` → whole-UI live retranslate (validates `Tech-Stack-Plan.md` i18n goal).
  - **Hotkey capture widgets** per ActionId with **reset-to-default**; conflict detection.
  - Quick-save/history folder pickers; auto-abort-on-focus-loss toggle; format defaults.

**Key bindings handled:** `,`/`.` history prev/next + `R`/`Shift+R` reuse older/newer snip
area (snip context); `Ctrl+Shift+P` opens this dialog from a pin.

**Data/settings:** consumes every group defined in Stages 1–5; the record ring is sized by
`snip/behavior/maxHistoryRecords=20` (replaces the old `general/maxHistory`), the session-only
reuse-area ring by `snip/behavior/maxHistoryAreas=8` + `historyLoop`; also
`general/restoreSession=true`, `general/locale` (selected language).

**i18n keys:** `prefs.tab.*` (general/snip/paste/control/annotate/interface/output/misc/log),
`prefs.language`, `prefs.hotkey.reset`, `prefs.quicksave_dir`, `prefs.history_dir`,
`prefs.restore_session`, `prefs.auto_abort`.

**Testing:**
- Unit: `tst_history` (ring eviction/order/persistence; only successful snips recorded);
  `tst_session_store` (serialize→deserialize a set of pins incl. annotations is lossless).
- Manual checklist ✅ **run and signed off 2026-07-26**: `,`/`.` cycles history during snip;
  close app with pins → relaunch restores them (positions/transforms/annotations); every
  preference persists & applies; remap a hotkey & confirm new binding + reset; switch language →
  entire UI updates live; add a throwaway `lang/xx.yaml` → it appears in the dropdown (no
  recompile).

**Exit ✅ met:** pins survive restart; all preferences persist & apply; hotkeys remappable +
resettable; language switch + no-code locale addition both demonstrated; history/session
tests green.

**Deferred extras: none left.** ✅ **Magnifier styling — done 2026-07-26 (user's call).**
Preferences → Interface → Magnifier carries three loupe-appearance controls: **Grid lines**
(`interface/magnifier/gridLines`, default on — a translucent grey grid between the magnified
pixels), **Coordinates** (`interface/magnifier/coordinates`, default on — the `( x , y )` line
atop the readout) and **Magnifier size** (`interface/magnifier/sizePercent`, 50–200 % in 25 %
steps, default 100 — scales the loupe **and** its readout panel together), all read by
`SnipOverlay::applyMagnifierSettings()` and consumed by `SnipScreenView::drawMagnifier()`. The
user closed the item here: the loupe's remaining fixed traits — panel background/text colours,
crosshair + centre-cell marker colours, readout fonts, and the panel's layout (swatch shape, row
order, panel-under-loupe placement) — stay hardcoded **by decision, not as a backlog**; don't
reopen them as a deferral. *(Three loupe-rendering bugs surfaced by those controls were fixed
2026-07-26, user-verified, all in `SnipScreenView`: (1) the size-scaled readout font **leaked**
out of `drawMagnifier`, so the Magnifier-size setting also resized the key-tips panel and the
history-strip footer — both derive their fonts from the painter's current font; `paintEvent` now
calls `drawMagnifier` inside `p.save()`/`restore()`. (2) The `Press {key} …` hint lines ran off
the panel's sides; they now **word-wrap** to the panel width via `wrapHint()`/`drawHintLines()`
(character-breaking for space-less CJK locales), with `panelH` counting the wrapped rows. (3) A
readout wider than the loupe (CMYK) left a stepped silhouette; the loupe now **grows with the
panel** by whole source-pixel columns, rounded up to the next **odd** count so the crosshair and
sampled-pixel marker stay centred, making loupe and readout exactly the same width.)* (Per-app hotkey ignore UI shipped — Control → App exclusions.)

**Key-tips panel occlusion — fixed 2026-07-29.** `drawKeyTips` fed `TipsPlacement::place()` only
the committed selection, so before any drag existed — exactly when a new user is reading the
hints — the cursor and the magnifier sat on top of the panel, and after a commit the PaintBar and
readout row did too. `place()` now takes a **`QVector<QRect>` of obstacles** instead of one rect
(skipping invalid ones, so callers pass an absent obstacle as a default `QRect` rather than
branching) and `SnipScreenView` feeds it five sources: selection, `hasSuggestion()` candidate, a
±24 px cursor box, the magnifier, and the readout row + every visible child widget. Two things
worth knowing before touching it: the magnifier's rect is **recorded, not re-predicted** — the
loupe's geometry falls out of ~100 lines of font measurement inside `drawMagnifier`, so it stores
`m_magRect` (cleared each `paintEvent`) and `drawKeyTips`, which `paintEvent` runs afterwards,
reads it; and the child-widget sweep is the *right* rule rather than a shortcut, since children
paint over the panel and would clip it. Placement stayed **stateless** (recomputed per paint, no
hysteresis) by the user's call: the panel returns to bottom-left the instant it is clear, the
accepted cost being visible corner-hopping as the loupe crosses the bottom of the screen.
The corner order (BL→BR→TL→TR) is unchanged, but the **all-blocked fallback had to change** and
that is the subtle part: user testing found the cursor/magnifier cases still not moving the panel
while the other three worked. Cause — `hasSuggestion()` yields `windowAt()`, a **whole window**,
so on any pre-drag hover over a large or maximized window that one obstacle covers all four
corners, `place()` hit its fixed BL fallback, and the small obstacles that motivated the fix were
swamped. The fallback now returns the **least-covered corner by overlap area** (ties to the
earlier corner), which makes a screen-filling obstacle cost every corner alike and leaves the
cursor/magnifier decisive. Covered by `tst_tips_placement` (10 cases, two of them regression
guards for exactly this).

---

## Stage 7 — Polish, Performance, Packaging & Compliance  *(DONE — signed off 2026-07-26)*

**Status:** ✅ **complete and signed off by the user 2026-07-26** (repo-wide `ctest` green —
29/29). Shipped across four phases, three of them built in this stage and each signed off against
its own Testing block:
- **Save formats** + **GIF playback on pins** (earlier, 2026-07-21/23).
- **Phase A — compliance artifacts** ✅ licence texts, `THIRD_PARTY_NOTICES.txt`, the expanded
  Preferences → About page.
- **Phase B — GPU blur** ✅ `render::blur()` dispatch over `core/render/GpuBlur` + the CPU box
  blur, `general/gpu_acceleration`, the General-page combo.
- **Phase C — portable ZIP** ✅ `install()` + CPack, `windeployqt` folded into the build, the
  bundled English OCR pack; verified running on a clean VM.
- **Phase D — DXGI** ⏸️ **deferred past the stage** (user, 2026-07-26). It is a *correctness*
  feature, not an exit criterion, so it did not gate closing the stage. Tracked in
  `Unimplemented-Features.md` §5; its design survives in full at the Phase D block below.

**Exit criteria: all three met**, bar a single release-time step — the portable ZIP runs on a
clean VM, the render perf target is met (4K blur 10.1 ms, inside the 16 ms budget), and the
LGPLv3 checklist is ticked except for swapping the placeholder `SNAPCAP_QT_SOURCE_URL` for our own
hosted `qt-everywhere-src` archive, which **cannot** be done until a release page exists to host
it on.

✅ **RESOLVED 2026-08-04 — the obligation carried out of this stage is discharged.**
`SNAPCAP_QT_SOURCE_URL` now points at `SnapCap-thirdparty-src`'s `qt-6.11.1` release, hosting
`qt-everywhere-src-6.11.1.tar.xz` (1,017,723,080 bytes, upstream MD5 verified before upload).
`-DSNAPCAP_RELEASE=ON` passes for the first time. The record of what was done and verified is
`Licence-Compliance-Plan.md` §4; the paragraphs below are kept as the history of *why* the
obligation existed and how it was gated in the meantime.

*Precision on the licence, corrected 2026-07-29:* an earlier revision of this bullet said a bare
upstream link "leaves us non-compliant". That overstates it. GPLv3 §6(d) explicitly permits the
Corresponding Source to live on a **third-party** server. What §6(d) does *not* do is transfer the
obligation — it closes with *"Regardless of what server hosts the Corresponding Source, you remain
obligated to ensure that it is available for as long as needed to satisfy these requirements."* So
the defect is not the link itself; it is that our compliance would rest on The Qt Company's
retention policy for 6.11.1, which we do not control and are not notified about.

**Hardened 2026-07-29** — two changes so this cannot be lost to memory:
- `SNAPCAP_QT_SOURCE_URL` is now **derived** from `${Qt6_VERSION}` in `CMakeLists.txt` rather than
  hardcoding `6.11/6.11.1`, so bumping the Qt kit can no longer leave the link pointing at source
  for a Qt we no longer ship. (About reports Qt from runtime `qVersion()`, so the two would
  otherwise drift silently.) This becomes vestigial once the URL is a release page, which carries
  no Qt version — it is an interim guard, not a lasting fix.
- A **packaging gate** (`option(SNAPCAP_RELEASE)`, `CMakeLists.txt`, before the CPack block) runs
  at install time and reports whenever the URL still matches `download.qt.io`: a **warning** on a
  normal `--target package` (so clean-VM test ZIPs still build), promoted to **`FATAL_ERROR`**
  under `-DSNAPCAP_RELEASE=ON`. That flag is how the published artifact is built, so a placeholder
  cannot reach a release.

**Screen recording (2026-07-30 → 08-04)** is a net-new subsystem that no stage owns; it has its
own phases 0–5, all complete. (Its plan was retired 2026-10-20; `Features.md` §3b, the recording rows
of `Architecture-FileMap.md` and `Licence-Compliance-Plan.md` hold what it established.) Two things
from it change this stage's picture:

- **A second LGPL corresponding-source obligation.** SnapCap now builds and ships FFmpeg
  (`tools/build_ffmpeg.ps1`), so `SNAPCAP_FFMPEG_SOURCE_URL` sits beside `SNAPCAP_QT_SOURCE_URL`
  and the **same packaging gate covers both** — resolving one cannot mask the other.
- **The release-day sequence below now has an FFmpeg half.** Both must be published *before or
  with* the first ZIP: the obligation attaches at conveyance.

**Release-day sequence** (one atomic action, now backed by the gate above). **Superseded in detail
by `Licence-Compliance-Plan.md` §4 (2026-08-04)**, which owns the executable version of this — the
same steps plus per-step verification, the retention duty, and the H.26x patent decision. The
summary below is kept because this stage is where the Qt obligation arose:
1. Download `qt-everywhere-src-<version>` matching the Qt built against (~1 GB — verify the actual
   size; GitHub's per-asset cap is 2 GB).
2. Create the GitHub release on the app repo and attach
   **both** that archive and `SnapCap-<version>-win64.zip`.
3. Repoint `SNAPCAP_QT_SOURCE_URL` at that release page (edit the `set()` default, so git records
   what shipped).
4. Run `tools/fetch_ffmpeg_source.ps1`, publish its output as a Release on
   `SnapCap-thirdparty-src` tagged `ffmpeg-<commit>`, and set `SNAPCAP_FFMPEG_SOURCE_URL` to it.
   (Unlike Qt's, this source is *ours* — we compile our own configure line, so no upstream
   archive would ever be the Corresponding Source. See `Licence-Compliance-Plan.md` §4.)
5. Rebuild with `-DSNAPCAP_RELEASE=ON`; **both** gates must pass.

Note that `THIRD_PARTY_NOTICES.txt` and `lang/en.yaml`'s `about.qt_source_desc` **already** state
that the source is hosted alongside the binary. That is deliberately left as-is: it describes the
arrangement at the moment of conveyance, and nothing has been conveyed yet. Softening it now would
add a second thing to remember to restore. The one caveat is that the existing
`build/package/SnapCap-0.1.0-win64.zip` carries that claim, so it should not go to a tester before
the release page exists.

Settings status at close: `general/gpu_acceleration` is live; **`general/capture_backend` still
has no consumer** — it waits on Phase D. *(`general/process_priority` was in this sentence too,
as RESERVED; it and `misc/double_tap_interval` became **won't-do** on 2026-07-29 — see the
Data/settings note below.)*

**Scope settled 2026-07-26** (a decision round with the user; details at each bullet):
- **Deferred past this stage** — the **theme system** (and with it `general/theme`) *(since
  **shipped 2026-08-07** — see the Appearance bullet; it landed as **`interface/theme`**, and
  `general/theme` was never implemented)*; the **Update page**; and the **50-pin memory
  measurement** *(since run — 2026-07-29, passed; see Testing)*.
  **`misc/double_tap_interval`** was deferred here too, as a reserved name;
  it was **dropped outright on 2026-07-29** (see the Data/settings note below).
- **Narrowed** — the GPU path is **blur only** (mosaic measured *slower* on GPU; "shadow" was
  never a `core/render` op).
- **Decided** — portable **ZIP, no installer**; `general/gpu_acceleration` = `auto`|`cpu`,
  default **`auto`**; `general/capture_backend` = `bitblt`|`dxgi`, default **`bitblt`**;
  ~~`process_priority` / `double_tap_interval` are **`config.ini`-only**, documented in
  `docs/Usage.md` §4a~~ *(superseded 2026-07-29 — both keys are **won't-do**, and §4a was deleted
  with them)*; About expands the existing **Preferences → About** page rather than adding
  an `AboutDialog`; the **Qt source obligation** is met by **hosting the source ourselves**
  (LGPLv3 §4(d)(0), not a written offer).
- **Corrected** — **startup-on-login** already shipped early under Stage 6's General tab, and
  **copy-image-as-file** was already implemented despite being listed as a deferral.

✅ **No open decisions remain in Stage 7** — everything still outstanding is implementation work.

**Implementation decisions taken 2026-07-26 (second round, user)** — these settle questions the
scope round above did not reach, all raised while planning the build order:
- **GPU blur lives in `core/render/GpuBlur.{h,cpp}`**, with `Qt6::OpenGL` linked into
  `snapcap_core`. Rejected: keeping GL out of `core/` and injecting a backend from `ui/` —
  a function-pointer indirection for a single implementation. Consequences to respect: every
  headless `tst_*` links GL, `SnapCap.exe` ships `Qt6OpenGL.dll`, and the dispatch mode must
  **default to CPU** until `app/Application` sets it, so `QTEST_GUILESS_MAIN` suites
  (`tst_render`, `tst_annotation_document`) never try to create a context without a
  `QGuiApplication`.
- **Both blur paths run a 3× box blur** — the GPU shader does box passes too, not the
  Gaussian the benchmark's reference implementation used. This makes blur output identical on
  every machine, so the regenerated `test/golden/blur*.png` cover what users actually see; the
  alternative (GPU Gaussian + CPU box fallback) would have left output machine-dependent at a
  max per-channel delta of 4–6 with the goldens able to pin only one of the two paths. Cost is
  nil: the GPU path is transfer-bound, not shader-bound.
- **The Qt-source link ships as a placeholder** (`SNAPCAP_QT_SOURCE_URL`, a CMake compile
  definition on `snapcap_core`) pointing at the upstream Qt archive, plus a release-checklist
  step. Nothing is distributed yet, so no §4(d)(0) obligation is live; the URL **must** be
  swapped for our own hosted archive before the first public release.
- **Not built, by omission from the scope round:** no GUI control for
  `general/capture_backend` (it stays `config.ini`-only, so DXGI is hand-edit opt-in).
  *(This bullet also kept `general/process_priority` as RESERVED / no consumer — dropped
  2026-07-29, see the Data/settings note below.)*

> **Phase structure.** Stage 7 is built in four phases, **A → B → C → D**, and *each phase has
> its own Testing block* (automated gate + manual checklist + exit gate) that must be green
> before the next phase starts. Ordering rationale: **A** first because it needs no new
> machinery and carries the licensing obligation; **B before C** so the packaged ZIP ships the
> final DLL set (the GPU path adds `Qt6OpenGL.dll`); **D last** as the highest-risk, most
> isolated piece.

### Stage 7 — Phase A (compliance artifacts) — ✅ DONE & SIGNED OFF 2026-07-26

**Status: ✅ complete and signed off by the user 2026-07-26.** Built and verified: repo-wide
`ctest` **29/29**, `tools/i18n_check.py` clean for `en`.
- ✅ `LICENSE.LGPLv3.txt` + `LICENSE.GPLv3.txt` at repo root (canonical gnu.org text, 165 / 674 lines).
- ✅ `THIRD_PARTY_NOTICES.txt`, written from the **verified** link set rather than from this
  plan's list — each entry checked against the dependency's own license file and pinned
  version: Qt 6.11.1 (LGPLv3, dynamic), yaml-cpp 0.8.0 (MIT), ZXing-C++ 2.2.1 (Apache-2.0),
  Tesseract 5.5.0 (Apache-2.0), Leptonica 1.85.0 (BSD-2), all four static.
  ⚠️ **The audit found four redistributables this plan's notice list had missed**, all of which
  ship in the deployed tree and are now attributed: the **MinGW GCC runtime**
  (`libgcc_s_seh-1.dll`, `libstdc++-6.dll` — GPLv3 **with GCC Runtime Library Exception**,
  which is what makes shipping a non-GPL binary built with GCC lawful), **libwinpthread-1.dll**
  (mingw-w64, permissive), **`opengl32sw.dll`** (Mesa llvmpipe, MIT) and **`D3Dcompiler_47.dll`**
  (Microsoft redistributable).
  ⚠️ **It also widened the Qt-source obligation**: `windeployqt` deploys **Qt SVG**
  (`Qt6Svg.dll`, `imageformats/qsvg.dll`, `iconengines/qsvgicon.dll`) and **Qt Image Formats**
  on top of the four modules in `find_package(Qt6 …)` — confirming that the full
  `qt-everywhere-src` archive, not a per-module subset, is the archive to mirror.
- ✅ **Preferences → About expanded in place** (`PreferencesDialog::buildAboutTab`), no
  `ui/AboutDialog`: three cards — identity (tagline + `Version {version}`), Qt/LGPLv3 (the
  `uses_qt` statement, the relinking explanation, and an **Open download page** button on
  `SNAPCAP_QT_SOURCE_URL`), and licenses/notices (**three buttons — LGPLv3 / GPLv3 /
  Third-party notices — each opening that document directly** in the OS text viewer).
  `prefs.about_text` was **reused** as the tagline rather than
  deleted — retiring it would have orphaned the key across all 11 translated locales for no gain.
  The version and Qt lines interpolate `{placeholders}`, which `retranslate()`'s plain
  `t(key)` loop over `m_labels` cannot do, so they are driven by a new `updateAboutLabels()`
  (same pattern as `updateThemeButton()`). Qt's version comes from **`qVersion()`** (runtime),
  not `QT_VERSION_STR` — the honest number, since the Qt DLLs are user-replaceable.
- ✅ New i18n keys in `lang/en.yaml` only: `about.version`, `about.uses_qt`, `about.qt_relink`,
  `about.qt_source`, `about.qt_source_desc`, `about.qt_source_button`, `about.licenses`,
  `about.licenses_desc`, `about.license_lgpl`, `about.license_gpl`, `about.license_notices`.
  **Deviation from this stage's planned key list:** `about.title` was **not** added — the page
  title is already `prefs.tab.about`, so the key would have been dead on arrival;
  `about.licenses` and `about.uses_qt` landed as planned.
- ✅ `SNAPCAP_VERSION` + `SNAPCAP_QT_SOURCE_URL` as **PUBLIC** compile definitions on
  `snapcap_core`, so the `prefs_demo` harness (which compiles `PreferencesDialog.cpp` too)
  sees them and keeps building.
- ✅ The three shipped documents are copied beside the exe by the existing POST_BUILD step, and
  all three carry a **`.txt` extension** (`LICENSE.LGPLv3.txt`, `LICENSE.GPLv3.txt`,
  `THIRD_PARTY_NOTICES.txt`) — renamed from the conventional extensionless/`.md` names
  **deliberately**, because that is what lets the OS open them in a text viewer. Extensionless
  `LICENSE.*` files make Windows raise its "how do you want to open this file?" picker, which
  is why the first cut of the page shipped a single **Open folder** button; with `.txt` names
  the direct-open buttons work, and that folder button was **removed** as redundant.
  `THIRD_PARTY_NOTICES` is also **plain text, not Markdown** — rule-line headings, indented
  license blocks, hard-wrapped at 80 columns and ASCII-only ("section 4(d)(0)", not "§4(d)(0)"),
  so it reads correctly in any viewer regardless of encoding.
**Phase A — Testing** ✅ *all green; user sign-off 2026-07-26*
- ✅ **Automated gate:** repo-wide `ctest` **29/29**; `tools/i18n_check.py` clean for `en` (the
  11 new `about.*` keys all resolve — the 11 non-English locales reporting them missing is the
  user's standing translation to-do, not a failure); build clean **including `prefs_demo`**,
  which is the real guard on the "PUBLIC compile definitions on `snapcap_core`" decision — a
  PRIVATE define would have broken that target and nothing else.
- ✅ **Content verification** (done by hand during the build, deliberately not automated — this
  is a licensing audit, not a code path): every dependency's license *and* pinned version read
  from its own `LICENSE`/`COPYING` file under `build/_deps/*-src` and `third_party/src/*`
  rather than from this plan's list, and the shipped redistributable set enumerated from
  `build/*.dll` + the plugin folders. That is what surfaced the four missed redistributables
  and the two extra Qt modules recorded above; a notice list written from the plan alone would
  have been wrong in six places.
- ✅ **Manual checklist (user):** Preferences → About shows the version and the Qt/LGPLv3
  statement with a real Qt version; all four buttons work — Qt download page opens in the
  browser, and LGPLv3 / GPLv3 / Third-party notices each open in a text viewer.
- ✅ **Exit gate:** licence texts + notices ship at repo root *and* beside the exe; About is a
  real page; `ctest` + `i18n_check` green. **Met.**
- ⚠️ **Known untested coupling, carried forward:** the document filenames exist in **three**
  places that must agree — `ui/PreferencesDialog.cpp`'s `kDocs` table, the `CMakeLists.txt`
  POST_BUILD copy, and the files themselves. Nothing fails the build if they drift; the buttons
  would just silently do nothing, and the `.txt` extension is load-bearing (see the bullet
  above). A ~20-line `tst_compliance_files` asserting the three names exist and are non-empty
  under `SNAPCAP_SOURCE_DIR` would close it. **Not built** — offered and not taken up; revisit
  if the names ever change again.

### Stage 7 — Phase B (CPU box blur + GPU blur) — ✅ DONE & SIGNED OFF 2026-07-26

**Status: ✅ complete and signed off by the user 2026-07-26.** Automated gate green (`ctest`
**29/29**, `i18n_check` clean for `en`, `tst_render_bench` GPU-vs-CPU deltas 0–1) and the manual
checklist below run and accepted.

Shipped:
- ✅ `render::boxBlur` + `render::boxRadii` in `core/render/Raster.cpp` — 3× running-sum box
  passes, radius-independent, Kutskir box sizes.
- ✅ `core/render/GpuBlur.{h,cpp}` — offscreen `QOpenGLContext` + FBO, running the **same** box
  passes via `boxRadii()`, six ping-ponged draws. `Qt6::OpenGL` is linked into `snapcap_core`
  (so `find_package` now requests it unconditionally, not just for tests).
- ✅ `render::blur()` + `setBlurMode()`; `app/Application::applyGpuAcceleration()` reads
  `general/gpu_acceleration` at startup and on `Settings::changed`.
- ✅ Callers repointed: `Annotation.cpp:339`, `:675`, `PaintBar.cpp:311`. `gaussianBlur` kept as
  the fidelity reference.
- ✅ Preferences → General **GPU acceleration** combo (Auto / CPU only).
- ✅ `test/golden/blur.png` + `blur_brush.png` regenerated.

**Findings worth keeping:**
- ⚠️ **The CPU box blur had a systematic darkening bias, found by the GPU comparison.** The first
  implementation truncated (`sum / n`) at each of the six passes, losing ~0.5 per pass in one
  direction; against the GPU (which rounds when writing RGBA8) that compounded to a **max
  per-channel delta of 4**. Fixed by rounding on the CPU (`(sum + n/2) / n`), which dropped the
  delta to **0–1** and makes the CPU blur independently more accurate. **This is the concrete
  payoff of the "both paths run the same passes" decision** — with a Gaussian GPU shader the
  4-level bias would have been invisible inside the expected 4–6 box-vs-Gaussian difference, and
  would have shipped.
- ⚠️ **`windeployqt` had to be re-run** — the new `Qt6OpenGL.dll` dependency is exactly the
  snapshot trap in `docs/Development.md`, and the app only appeared to work because Qt's `bin` was on
  PATH. Verified afterwards by launching with **Qt deliberately off PATH**. Phase C's
  `install(CODE)` step removes this hazard permanently.
- **i18n key-name deviation:** the plan named `prefs.gpu` for the row label plus `prefs.gpu.auto`
  / `prefs.gpu.cpu` for the items. YAML cannot make one key both a scalar and a map, so the label
  is **`prefs.gpu_accel`** and `prefs.gpu` is the parent map — the same shape the existing
  `prefs.log_level` + `prefs.log.*` pair already uses.
- **Measured on this machine** (Release/MinGW, Ryzen 9 9950X3D + RTX 5090), shipped paths:
  region 960×540 blur **14 ms CPU → 0.9 ms GPU**; 4K blur **235 ms CPU → 10.1 ms GPU**, and the
  GPU stays flat from radius 12 to 80. The true Gaussian, for contrast, is 960 ms / 3 775 ms at
  4K — i.e. the box change alone is a ~4–16× win before the GPU does anything.
- ✅ **Benchmark folded onto `render::boxBlur` (2026-07-26).** `tst_render_bench.cpp`'s prototype
  `boxBlur3`/`boxPass` (~60 lines) is **deleted**; `blurAlgorithmAlternative` now measures the
  shipped function, so there is exactly **one** CPU box-blur implementation to keep correct.
  The prototype had done its job — it existed to answer "is the CPU slow because of the algorithm
  or the lack of a GPU?", the answer was "the algorithm", and it was then promoted into
  `core/render`. Two things confirmed the fold: timings are unchanged (14.07 / 235.44 ms vs
  14.11 / 235.48 ms), and box-vs-Gaussian fidelity **improved** from 4/6 to **3/5** per channel,
  because the promoted version rounds where the prototype truncated — independent evidence that
  the rounding fix was a real accuracy gain, not just GPU-matching.

**Original build order** (kept for reference):

Build order within the phase — the CPU fallback lands **first**, because it is what a machine
failing the GL probe drops to; shipping the GPU path without it would leave those machines on
the 1 117 ms/4K separable blur.
1. `render::boxBlur(src, radius)` — 3× running-sum box passes, radius-independent.
2. `core/render/GpuBlur.{h,cpp}` — offscreen `QOpenGLContext` + FBO, the shader running **box**
   passes (not Gaussian — see the decision above), lifted from `tst_render_bench.cpp`'s
   `GpuRaster` (~L316–510). Lazy one-time capability probe, cached context/FBOs, GUI-thread only.
   **The probe must reject software GL** by `GL_RENDERER` string, not merely on context-creation
   failure: `windeployqt` ships `opengl32sw.dll` (Mesa llvmpipe), so creation *succeeds* on a
   machine with no usable GPU, and llvmpipe running a blur shader is slower than the CPU code we
   already have. Without that check the setting is a pessimisation on exactly the machines it is
   meant to help.
3. `render::blur(src, radius)` dispatch + `render::setBlurMode(Auto|Cpu)`. **Library default is
   `Cpu`** until `app/Application` sets it from `general/gpu_acceleration`, so `QTEST_GUILESS_MAIN`
   suites never try to create a GL context without a `QGuiApplication`.
4. Repoint the three callers: `core/annotation/Annotation.cpp:339`, `:675`, `ui/PaintBar.cpp:311`.
   `render::gaussianBlur` **stays** — it is the fidelity reference the benchmark measures box
   blur against, not dead code.
5. Preferences → General **GPU acceleration** combo (Auto/CPU) + `prefs.gpu*` i18n keys — built
   **last**, so it never ships without a consumer. Its description must say *blur*: mosaic is
   CPU-only by measurement and ignores the setting entirely.

**Phase B — Testing**
- ✅ **Automated — new `tst_render` cases:** `boxBlur` uniform-image preserved, `radius <= 0`
  no-op, deterministic across runs, edge pixels monotonic; plus `boxRadii` returning three
  positive, monotonically-growing box sizes (a zero would silently make a pass a no-op).
- ✅ **Automated — software-GL classifier:** `render::isSoftwareRenderer()` unit-tested against
  real `GL_RENDERER` strings — llvmpipe / Basic Render Driver / GDI Generic / softpipe / SWRast
  → **true**; NVIDIA / AMD / Intel adapter names and an empty string → **false**. The one
  genuinely testable half of the probe, and the half most likely to be got wrong.
- ✅ **Automated — headless safety:** `tst_render` (`QTEST_GUILESS_MAIN`) asserts
  `blurMode() == Cpu` by default **and** that forcing `Auto` with no `QGuiApplication` falls back
  to the identical CPU result rather than crashing or returning null.
- ✅ **Automated — goldens:** `blur.png` + `blur_brush.png` regenerated; `tst_annotation_golden`
  18/18. Only those two of eighteen shapes changed, which confirms the blur work stayed scoped.
- ✅ **Benchmark (by hand):** `tst_render_bench::shippedGpuBlurMatchesCpu` runs the *shipped*
  `render::blur` on both backends over noise + vertical-split + horizontal-split images at
  radius 2/12/80 — nine cases, **max per-channel delta 0–1**. The split images are deliberate:
  a flat colour blurs to itself and would pass even if every sample clamped to texel 0, which is
  the exact bug the earlier reference path hit.
- ✅ **Manual checklist (user):** the **GPU acceleration** combo appears on Preferences → General
  with **Auto** / **CPU only**; a Blur annotation and a blur *brush* stroke drawn on **Auto** and
  again on **CPU only** look **identical** and both stay responsive while dragging; `splog.txt`
  names the backend chosen and the reason for any fallback. **Signed off by the user 2026-07-26.**
- ✅ **Exit gate:** `ctest` green (29/29); goldens regenerated and eyeballed; the combo
  demonstrably switches backend; no GL error on a real-GPU machine and a clean CPU fallback when
  forced. **Met — Phase B is complete.**

### Stage 7 — Phase C (packaging — portable ZIP) — ✅ DONE & SIGNED OFF 2026-07-26

**Status: ✅ complete and signed off by the user 2026-07-26.** `cmake --build build --target
package` produces `build/package/SnapCap-0.1.0-win64.zip` (**32.3 MB**), which unzips to a single
`SnapCap-0.1.0-win64/` folder and runs; the automated gate and the clean-VM manual checklist are
both green.

Shipped: `install(TARGETS SnapCap RUNTIME DESTINATION .)` + `install(DIRECTORY lang/ res/ icons/)`
+ the three licence documents + the bundled OCR pack; an `install(CODE)` that runs
`windeployqt --release` against the **staged** exe at install time; and `CPACK_GENERATOR ZIP` +
`include(CPack)` writing to `build/package/`. Keeping the file list in `install()` rather than a
Windows-only script is also what keeps a future macOS/Linux bundle cheap.
- `SNAPCAP_WINDEPLOYQT` is resolved from `Qt6::qmake`'s own location, **not** PATH, so a second
  Qt installation on PATH cannot poison the deployment.
- Deliberately **no** `--no-compiler-runtime`: this is a MinGW kit, so `libgcc_s_seh-1.dll`,
  `libstdc++-6.dll` and `libwinpthread-1.dll` are genuine dependencies (and are attributed in
  `THIRD_PARTY_NOTICES.txt`).

**Findings worth keeping:**
- ✅ **The staging tree is what makes this safe.** Zipping `build/` would have shipped
  `config.ini`, `splog.txt`, `history/`, `session/`, 29 `tst_*.exe`, `prefs_demo.exe` and three
  extra OCR language packs. The `install()` whitelist excludes all of them — verified by an
  explicit **negative** manifest check, not just a positive one.
- ✅ **`install(CODE)` retires the "windeployqt is a snapshot" hazard for packaging.** Phase B hit
  exactly that trap by hand (the new `Qt6OpenGL.dll` was missing until `windeployqt` was re-run).
  Because packaging re-derives the set from the staged binary on every run, a newly linked Qt
  module can no longer be silently absent from the ZIP. The hazard **remains for the dev `build/`
  tree**, which is still populated by hand — the `CMakeLists.txt` comment now says exactly that.
- ⚠️ **The bundled OCR language forced a licensing correction.** The OCR design said SnapCap
  ships English, but Phase A's `THIRD_PARTY_NOTICES.txt` stated that `*.traineddata` files are
  *not* redistributed — and the first ZIP shipped none, since the packs in `build/tessdata/` were
  runtime downloads. Resolved by the user 2026-07-26: **bundle `eng.traineddata`, fetched at
  configure time** from the same `tessdata_fast` URL the in-app downloader uses, with the notices
  gaining a **tessdata_fast (Apache-2.0)** entry that distinguishes the one pack we redistribute
  from the ones the user downloads. The download failure path is a **warning, not an error**, so
  a proxy-blocked build still succeeds and simply ships no bundled language. `install(FILES …)`
  names that one file rather than the directory, which is why the three other packs on this dev
  machine did not leak into the ZIP.
- ⚠️ **`build/package/` added to `.gitignore`.** This repo tracks `build/` wholesale (496 files),
  so the 32 MB ZIP would otherwise be swept into version control by `git add -A`, once per
  release. It is reproducible from source at any time, so it is the one deliberate exception.
  *(`build/tessdata/*.traineddata` were already tracked before this work.)*

**Phase C — Testing**
- ✅ **Automated — package builds:** `cmake --build build --target package` exits 0 and produces
  `SnapCap-0.1.0-win64.zip` containing a single top-level folder.
- ✅ **Automated — positive manifest:** `SnapCap.exe`, `Qt6Core/Gui/Widgets/Network/OpenGL.dll`,
  `platforms/qwindows.dll`, `styles/qmodernwindowsstyle.dll`, `imageformats/qtiff.dll`, the three
  MinGW runtime DLLs, `lang/en.yaml`, `res/keymap.default.yaml`, `res/preferences.qss`, `icons/`
  (25 files), the three licence `.txt` documents and `tessdata/eng.traineddata` all present.
- ✅ **Automated — negative manifest:** `config.ini`, `splog.txt`, `history/`, `session/`,
  `tst_*.exe`, `prefs_demo.exe`, `CMakeCache.txt` and the non-English OCR packs all absent.
- ✅ **Automated — self-contained:** the extracted app launches with `PATH` cut to
  `C:\Windows\system32;C:\Windows` — no Qt, no MinGW, no toolchain — and writes `config.ini` and
  `splog.txt` **beside the exe**, so the portable model survives packaging.
- ✅ **Automated — LGPLv3 evidence:** `objdump -p` on the packaged exe shows all five `Qt6*.dll`
  and the three MinGW runtime DLLs as **dynamic imports** (nothing statically linked); the exe is
  **unsigned** and the shipped DLLs are **not read-only**, so nothing seals the tree; and
  substituting `Qt6Core.dll` in the unpacked folder leaves the app running.
  ⚠️ **Honest limit:** that substitution used the *same* Qt 6.11.1 build, so it demonstrates the
  tree is unsealed and that Qt resolves from the app's own folder — **not** that a differently
  compiled Qt works. A genuinely different build remains a user-side check, and the plan should
  not claim more than was observed.
- ✅ **Manual checklist (user) — run on a clean Windows VM** with no dev tools and no Qt
  installed, since the whole claim is "unzip and run": app launches to the tray, `F1` snips,
  Preferences opens, About's three buttons open the documents, OCR offers **English without a
  download**, and `config.ini` is created beside the exe (no `Program Files`, no virtualised
  install dir). **Signed off by the user 2026-07-26.**
- ✅ **Exit gate:** the ZIP unpacks and runs on a clean VM. **Met — Phase C is complete.**

### Stage 7 — Phase D (DXGI Desktop Duplication) — ⏸️ DEFERRED PAST STAGE 7 (user, 2026-07-26)

**Not started, and no longer stage scope.** Stage 7 was closed out with A/B/C signed off; Phase D
is **deferred, not dropped** — the design below stands and is implementation-ready, so picking it
up later needs no fresh decisions.

**Why it was safe to close the stage without it:** none of Stage 7's three exit criteria depend
on it. DXGI is a **correctness fix** for hardware-overlay capture (the black-video case), not a
packaging, performance or compliance item. Its value is also **unproven on this hardware** — if
the black-rectangle bug is never reproduced against SnapCap, the bullet buys little, which is a
poor reason to hold a stage open.

It is tracked in `Unimplemented-Features.md` §5 so it does not become invisible now that no stage
owns it. The plan and Testing block below are preserved verbatim for whoever picks it up.

`platform/win/WinDxgiCapture`: per-output `IDXGIOutputDuplication` (D3D11 device created on the
adapter that *owns* each output — duplicating an iGPU-attached display from a dGPU device fails
outright), `AcquireNextFrame` with wait-and-retry, staging-texture map → `QImage`, stitched via
the existing monitor table. Selected by `general/capture_backend` (`config.ini`-only), with
fallback to `BitBlt` on **any** DDA error inside the Win32 layer, so `core/` and `ui/` are
untouched. Links `d3d11`/`dxgi` — system libraries, no LGPLv3 impact.

**Phase D — Testing**
- ☐ **Automated:** the mixed-DPI model must be untouched — `tst_screen_layout` and
  `tst_screen_composite` stay green. Any backend-selection logic that can be factored pure gets
  a unit test; the DDA path itself needs hardware and cannot be covered headlessly.
- ☐ **Manual — default unchanged (regression):** with `capture_backend=bitblt` (the default),
  every snip behaves exactly as today. This is the most important row: the majority of users must
  see no change at all.
- ☐ **Manual — the actual point:** with `capture_backend=dxgi`, snip **hardware-overlay content**
  (accelerated video playback) and confirm it is *not* black. If this cannot be reproduced on the
  user's hardware, say so plainly — the whole bullet is a correctness fix for a bug that may not
  affect this machine, and it must **not** be sold as faster capture.
- ☐ **Manual — mixed-DPI regression:** a cross-monitor drag on the 4K@150% + 1440p@100% rig still
  stitches correctly with **no white band**. DDA is per-output and returns raw physical px, which
  matches the capture-px model, but this is the Stage 2 fix most at risk here.
- ☐ **Manual — forced failure paths, since fallback is the design:** RDP session, screen lock /
  fast-user-switch, and a resolution change mid-session each fall back to `BitBlt` with a logged
  reason and no crash. `ACCESS_LOST` is routine, not exceptional.
- ☐ **Exit gate:** `dxgi` fixes the black-overlay case where reproducible, falls back cleanly and
  visibly in `splog.txt` everywhere else, and the `bitblt` default is behaviourally unchanged.

---

**Objectives:** ship-ready quality, performance, and LGPLv3-compliant distribution.

**Components & responsibilities:**
- ✅ **Save formats** (`Features.md §5`) — **done 2026-07-21, all 7 live** (verified writable +
  decodable by `tst_image_export::everyAdvertisedFormatActuallyWrites`, which logs the set it
  covered). Qt alone could write only PNG/JPG/BMP/**ICO** here: its TGA and GIF plugins are
  read-only, and `qtiff` lives in the optional `qtimageformats` module. So `core/codec/`
  ships a **TGA** writer and a **GIF** writer (`Quantize` = exact palette ≤256 colours, else
  median cut + Floyd–Steinberg), and `ImageExport::toFile` dispatches to them by extension.
  `ManualSave::filterFor` builds the Save dialog from `ImageExport::supportedSaveFormats()`, a
  **runtime** probe — so the dialog can never offer a format that would then fail, and **TIFF
  needed no code at all**: installing the Qt module (Qt Maintenance Tool → Qt 6.11.1 →
  Additional Libraries → **Qt Image Formats**) and **re-running `windeployqt`** (the
  install alone is not enough — `cmake --build` never refreshes `build/imageformats/`) made it
  appear by itself. Two silent failures were fixed alongside: a failed save still
  recorded a history entry, and Qt's ICO writer downscales anything over 256px while returning
  success (`ui/SaveFeedback` now warns with the real target size). Tests: `tst_image_codecs`,
  plus `tst_image_export`/`tst_manual_save` extensions; the GIF path was cross-checked against
  two independent decoders (Qt `qgif`, Pillow) and a noise fixture forcing 66 LZW dictionary
  resets.
  **An 8th format, PDF, was added post-stage on 2026-08-16** — `core/codec/PdfEncoder` writes a
  one-page document with the image as a lossless `/FlateDecode` XObject, sized by
  `output/pdf/pageSize` (72 DPI / 96 DPI / fit-A4). Qt's own `QPdfWriter` was measured and
  rejected: it always re-encodes as JPEG. Evidence and known limits: `core/codec/PdfEncoder.h`.
- ✅ **GIF playback on pins** (`Features.md §3`) — **done 2026-07-23**. `core/codec/AnimatedImage`
  decodes every frame + per-frame delay (any format whose plugin reports animation, not just
  `.gif`); `PinModel` holds the frames and `advanceFrame()` swaps **only** the source pixels, so
  the whole existing pipeline — zoom, rotate/flip, grayscale/invert, annotations, save/copy/OCR —
  applies per frame with no animation awareness anywhere else. `ui/PinWindow::attachAnimation()`
  is the single attach point for all three entry points (menu → Replace by file, `F3` on a copied
  image *file*, and session restore, which re-reads the frames from the already-persisted
  `sourcePath`). A single-shot frame timer gated by `updatePlayback()` pauses playback while the
  annotation toolbar is open, while the pin is hidden, or on the menu's **Pause** item.
  A bitmap copied from another app can never animate — Windows flattens it to `CF_DIB` before
  SnapCap sees it. Oversized animations pin as a still with a warning (`kMaxAnimationPixels`).
  Tests: `tst_pin_model` (frame swap preserves transforms/annotations/path, wrap, delays,
  `setSource` clears), `tst_image_codecs` (`anim::load` on a hand-built 2-frame GIF).
- ❌ **`core/render` GPU path — GPU *blur* only.** Not started: `core/render/Raster.cpp` is still
  the CPU block-average/separable-blur from Stage 4, and `gpu_acceleration` appears nowhere in
  the code — only in this document.
  - **DECIDED 2026-07-26, from the CPU-vs-GPU measurements below: `gaussianBlur` runs on an
    offscreen `QOpenGLContext` + FBO shader path whenever the capability probe passes, and falls
    back to a CPU *box* blur otherwise. `mosaic` stays on the CPU unconditionally.**
  - **Scope narrowed by that decision:** the bullet used to plan GPU acceleration for
    "mosaic/blur/shadow" (the wording comes from `Features.md:321`). **Both of the other two are
    now out, for different reasons:**
    - **Mosaic — out, measured.** It is *slower* on the GPU at every size (9.4 ms vs 7.7 ms at
      4K): the transfer round trip costs more than the block-averaging it replaces, so a GPU
      mosaic would be a regression.
    - **Shadow — out, and it was never a `core/render` operation at all.** "Shadow" here is the
      pin window's unfocused **drop shadow** (`paste/display/windowShadow` + `shadowColor`),
      which already shipped in Stage 5/6. `PinWindow::drawDropShadow()` (`ui/PinWindow.cpp:496`)
      does not call `render::` — it fakes a soft blur with **5 concentric `drawRoundedRect`
      calls** (`kShadowBlur = 5`) in the thin transparent margin around the pin. That is
      microseconds of painting on a pin-sized rect, so there is nothing to accelerate and no
      entry point to accelerate it behind.

    That leaves **blur, and only blur**, behind the raster interface.
  - **The CPU box blur is now a prerequisite, not an alternative.** It is the fallback path, so
    it must land **before or with** the GPU work — otherwise a machine that fails the probe is
    left on the 1 117 ms/4K separable blur. Both redaction *brush* tools inherit this split for
    free, since `MosaicBrush`/`BlurBrush` call the same two `render::` functions.
  - ✅ **`general/gpu_acceleration` — semantics pinned 2026-07-26 (user).** **Two** values, not
    0/1/2 (that inherited wording is now dropped):
    - **`auto`** — use the GPU when the capability probe passes; fall back to the CPU box blur
      on probe failure, software-GL detection, or any runtime GL error.
    - **`cpu`** — never touch the GPU; always the CPU box blur.

    Stored as **strings**, matching `general/capture_backend`'s style rather than
    `gpu_acceleration`'s old integers. **Default = `auto`** — confirmed by the user 2026-07-26.
    The probe plus the silent fallback are what make `auto` safe to default; a `cpu` default
    would mean nobody gets the 120× blur win without first finding the setting.
    There is deliberately **no "force GPU" value** — with a probe and a fallback, forcing has no
    coherent meaning beyond "ignore the evidence and render wrong or slow".
  - **Preferences control (planned, S7):** **General** page → a **GPU acceleration** row with a
    two-item combo, **Auto** / **CPU**, persisting `general/gpu_acceleration`. Its only consumer
    is `core/render`'s blur dispatch — so the row's description text should say *blur*, not
    "acceleration" in general, or it will over-promise: **mosaic ignores this setting entirely**
    (it is CPU-only by measurement) and shadow has no entry point at all. **Do not build this
    control before the GPU blur path exists**, or it ships with nothing to consume it.
  - **Not ANGLE (decided 2026-07-26).** Earlier drafts of this bullet said "OpenGL/ANGLE"; that
    was Qt 5-era wording — **Qt 6.0 removed ANGLE**, so there is nothing to target. The path is
    plain desktop OpenGL via Qt.
  - **The probe must reject software GL.** `windeployqt` also deploys **`opengl32sw.dll`**
    (Mesa **llvmpipe**) as Qt's software fallback, so context creation can *succeed* on a machine
    with no usable GPU — and llvmpipe running a blur shader is **slower than the CPU code we
    already have** (`Raster.cpp` is an integer separable blur). So the capability probe has to
    check `GL_RENDERER` and fall back to CPU on `llvmpipe` / `Microsoft Basic Render Driver` /
    `GDI Generic`, not merely on context-creation failure. Without that check the toggle is a
    pessimisation on exactly the weak machines it is meant to help.
  - **Do not force the discrete GPU.** Adapter choice belongs to the driver + the Windows
    *Graphics performance preference*; exporting `NvOptimusEnablement` /
    `AmdPowerXpressRequestHighPerformance` would wake a dGPU for no gain. The measurements below
    show the GPU path is **transfer-bound, not shader-bound** (0.1–2.8 ms of shader work vs
    ~9 ms of PCIe round trip at 4K), so an **iGPU is the better target** — shared memory should
    shrink the dominant cost. Leave selection to the OS.
  - **Sequencing — CPU *and* GPU both benchmarked 2026-07-26 (`tst_render_bench`); full table in
    Testing below.** The GPU half required writing a reference OpenGL implementation, since none
    exists in `core/render`. Verdict, split by operation:
    - **Mosaic — drop it from this bullet. The CPU is *faster*.** 7.7 ms vs **9.4 ms** GPU at
      full 4K, 0.5 ms vs 1.4 ms on a region. Mosaic is cheap enough that the upload+readback
      round trip costs more than the work it replaces. A GPU mosaic would be a *regression*.
    - **Blur — build it. The GPU wins by two orders of magnitude.** 4K blur: **1117 ms CPU →
      9.3 ms GPU** (120×) at default strength, **3689 ms → 10.1 ms** (364×) at max. GPU blur is
      also **radius-independent** (9.3 ms at r=12, 10.1 ms at r=80) and is the **only** measured
      option that meets the 16 ms budget at 4K.
    - **The CPU box-blur fix is still worth doing — as the fallback, not the answer.** At
      region size it reaches 14 ms (inside budget), so machines that fail the GL probe stay
      usable; but at 4K it is 233 ms, still 15× over. GPU beats it 15–25× everywhere.
  - **Why the result should generalise off this machine (RTX 5090):** GPU cost is dominated by
    transfer, not compute — at 4K, upload ~4.7 ms + readback ~4.3 ms of a ~9.3 ms total, with
    the shader itself 0.19–1.14 ms. A GPU 10× slower still lands near the same ~9 ms floor, so
    the blur win is not a high-end-hardware artefact. The corollary is that **~9 ms of the 16 ms
    budget is spent moving a 4K frame across PCIe**, which is the real ceiling to design against.
  - **Cost of the box-blur change (know before starting):** `test/golden/` holds committed PNGs
    for the **`blur` and `blur_brush`** shapes (`tst_annotation_golden`), so both must be
    regenerated and re-eyeballed. `tst_render`'s blur cases are property-based (uniform
    preserved, edge monotonic, deterministic) and should survive unchanged.
  - **Test consequence:** GPU float math will not match the CPU integer path bit-for-bit across
    vendors, so `tst_render`'s determinism/golden cases stay **CPU-only**; the GPU path gets a
    max-per-channel-delta assertion against the CPU result instead.
- Appearance — mixed, and the two sub-items are at two different places:
  - ✅ **QSS themes — deferred 2026-07-26, SHIPPED 2026-08-07.** A **Theme** combo (Auto / Light /
    Dark, default Auto) now sits on Preferences → Interface → General, directly above Accent color,
    persisting **`interface/theme`**. Two consumers: `PrefsStyle::load()` selects the palette its
    `@token`s resolve against, and `Application::applyColorScheme()` maps the value onto
    `QStyleHints::setColorScheme()` (`auto` → `Qt::ColorScheme::Unknown`, letting Qt track the OS),
    which carries the theme to the windows the QSS never reaches — `ColorDialog`, the group manager,
    the recording preview, message boxes, menus — and to the native title bars. Auto follows the OS
    live: `PreferencesDialog` re-runs `loadStyleSheet()` on `QStyleHints::colorSchemeChanged`, since
    on Auto the scheme changes with **no setting changing**.
    **The key is `interface/theme`, not the `general/theme` this plan reserved.** The control lives
    on the Interface page, `config.ini` groups mirror the Preferences tabs, and
    `restoreInterfaceDefaults()` resets only `interface/*` — a `general/*` key would have been
    silently skipped by that page's own Restore Defaults. **`general/theme` was never implemented
    and its name is now free for reuse.**
    Implementation shape: `res/preferences.qss` holds **no colour literals** at all — its 67 values
    became 20 named tokens, and `kLight`/`kDark` in `ui/PrefsStyle.cpp` are the two value tables
    over that one set of rules, so the schemes cannot drift. Two adjacent literals were folded in
    on the way: the quick-snip "+" and drag hint carried inline `color:#101828` stylesheets (near
    invisible on dark) and now use a `#SettingDescStrong` QSS rule instead.
    ⚠️ **Two bugs found in manual testing, both specific to Auto + an OS-driven switch** — neither
    reproduces when the theme is set by hand, which is what made them easy to miss:
    1. **Restyling must be deferred one event-loop turn.** `colorSchemeChanged` arrives while Qt is
       still working through the OS theme change; re-applying the stylesheet from inside it left
       `#PageTitle` and `#SectionTitle` holding the *previous* scheme's colour while cards, sidebar
       and setting labels had already flipped. `QTimer::singleShot(0, …)` around `loadStyleSheet()`
       fixes it. Setting the theme by hand always worked because that path re-applies in a later
       cycle anyway.
    2. **Natively-drawn controls need QSS rules, or they do not follow the OS at all.** The checked
       `QRadioButton` indicator kept its light-mode rendering on a dark card, while the checkboxes
       beside it were fine — because `QCheckBox::indicator` was styled in the QSS and
       `QRadioButton::indicator` was not. Re-polishing the widget tree (`style()->unpolish/polish`,
       kept, since it covers spin boxes and scroll bars on the same path) did **not** fix the radio;
       styling it from tokens did. Its checked look is a **donut** — a radial gradient, not a fat
       border, because QSS grows an indicator outward from its `width`/`height` and a thick border
       would make the checked state larger than the unchecked one and shift the row.
    **Out of scope, deliberately:** the hand-painted overlay/pin chrome (`PaintBar`, `OcrBar`, pin
    borders) keeps its own greys, and `PinWindow::themeColor()` (`ui/PinWindow.cpp:151`) keeps its
    hard-coded neon-cyan literal and its `TODO(theme)` — that is an **accent** gap, not light/dark.
    *(Cheap partial, still available: point `themeColor()` at `interface/accentColor`, which already
    exists and is live-updating — a one-line change that would close the TODO on its own.)*
  - ✅ **Startup-on-login** — **already shipped, out of stage order**, with Stage 6's General tab:
    `platform/win/WinAutostart` behind `platform::IAutostart` (HKCU `Run` value, or a
    highest-privilege logon Scheduled Task for the elevated form), wired at
    `ui/PreferencesDialog.cpp:539` and reconciled on launch in `app/Application.cpp:125`. **The
    keys are `general/runOnStartup` + `general/startupElevated`**, not the `general/startupOnLogin`
    this stage originally planned — the plan's key name is obsolete, the feature is not.
- ❌ **Packaging — portable ZIP only, not started.** `windeployqt` is only a *comment block* in
  `CMakeLists.txt` (~L290–302) documenting the manual step; there are no `install()` rules, no
  CPack configuration, and no packaging CI.
  - **Decision (2026-07-26): SnapCap ships as a portable ZIP. There will be no installer** —
    no Inno Setup, no MSIX. This supersedes the earlier "Inno Setup (or MSIX) installer" plan.
  - **Why — LGPLv3 §4 relinking.** The obligation is that the end user can swap in their own
    Qt DLLs and run the result. A portable unpack-and-run tree satisfies that in the most direct
    way possible: every Qt DLL sits beside the exe, replaceable with a file copy.
    **MSIX would actively break it** — it enforces package integrity, so the user *cannot*
    replace the bundled Qt, which is precisely the freedom §4 exists to guarantee; it also
    virtualises the install directory, which would defeat the portable
    `config.ini`-next-to-the-exe model (`Settings::resolveConfigPath()`).
  - **Remaining work:** `install()` rules + a **CPack ZIP** generator, so `cpack -G ZIP`
    reproduces the portable tree (exe + Qt DLLs/plugins + `lang/` + `res/`) instead of it being
    assembled by hand. Keeping the file list in `install()` rather than in a Windows-only
    packaging script is also what keeps a future macOS/Linux bundle cheap.
  - **Side effect avoided:** because we never install under `Program Files`, the exe directory
    stays writable and settings always live next to the exe — so there is no
    installed-vs-portable split in where `config.ini` lands.
- ✅ **Compliance artifacts — DONE 2026-07-26 (Phase A; see the Phase A block above for detail).**
  The *audit* half already held: `find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)`
  (+`Test`) is LGPLv3-only with **no GPL-only Qt module**, and Qt stays dynamically linked. The
  paperwork now ships too: `LICENSE.LGPLv3.txt`, `LICENSE.GPLv3.txt` and `THIRD_PARTY_NOTICES.txt` at repo
  root **and beside the exe**, plus an expanded Preferences → **About** page (no `ui/AboutDialog`,
  by decision). The notice list was written from a **verified** audit of what the build actually
  links and deploys, which turned out wider than this bullet originally assumed — it adds the
  MinGW GCC runtime, libwinpthread, Mesa `opengl32sw.dll` and `D3Dcompiler_47.dll`, and it
  records that Qt SVG + Qt Image Formats ship beyond the four `find_package` modules.
  QuaZip/hoedown/md4c/OpenSSL are confirmed **not** dependencies of this build (TLS is Schannel).
  The **Update** page remains a `prefs.update_hint` placeholder, as deferred.
  - ✅ **About surface — decided 2026-07-26 (user): expand the existing Preferences → About page.
    No separate `ui/AboutDialog`.** The category already exists
    (`PreferencesDialog::buildAboutTab`); it grows the license text, the third-party notices and
    the Qt-source link in place. Earlier revisions of this bullet listed "no `ui/AboutDialog`" as
    a gap — that is now **not** a gap, it is the chosen design, so nothing new is added to `ui/`.
  - ✅ **Qt source obligation — decided 2026-07-26 (user): option (a), we host the Qt source
    ourselves.** LGPLv3 §4(d)(0): convey the Corresponding Source *alongside* the binary, on the
    same release page as the portable ZIP, linked from Preferences → About. The **written offer**
    alternative (§4(d)(1)) is rejected — it would bind us for **3 years** to supply source to
    *anyone* who asks, not just users; self-hosting is a one-time cost that ends when we stop
    distributing. Applies to **Qt only**: yaml-cpp, ZXing-C++, Tesseract and Leptonica are
    permissive and need attribution, not source.
    - **Host the archive ourselves — do not just link `qt.io`.** The obligation is *ours*; if
      upstream retires 6.11.1 a bare upstream link leaves us non-compliant. Mirror the file.
    - **What to host:** source covering **every Qt module whose DLL or plugin actually ships in
      the ZIP** — not just the four in `find_package`. `windeployqt` also copies plugins
      (`platforms/`, `imageformats/`, `styles/`, `tls/`), so anything pulled in that way counts
      too — e.g. `qtiff.dll` from the optional **Qt Image Formats** module. The safe, simple
      choice is the full **`qt-everywhere-src-6.11.1`** archive, which covers whatever the
      deployment happens to contain.
    - **Re-host on every Qt bump.** The source must match the binary it was built from, so a Qt
      upgrade means mirroring the new archive. This is the same "`windeployqt` is a snapshot"
      trap recorded in `docs/Development.md`, one layer out: bumping Qt silently invalidates the hosted
      source unless someone re-mirrors it.
    - **How many archives must we keep? One per *Qt version* that a *currently downloadable*
      release was built against — not one per SnapCap release, and not forever.** Consecutive
      SnapCap releases on the same Qt share one archive, and an archive can be retired once the
      release built from it is no longer offered. If only the latest build is published, exactly
      one archive is live at a time.
      ⚠️ **The accumulating case is release hosting that never forgets:** GitHub Releases keeps
      every old tag downloadable by default, and a downloadable release is still being conveyed —
      so "we never delete old releases" silently means "we host every Qt version we ever
      shipped". Either prune old releases or budget for one archive per Qt bump.
      *(If size ever bites — `qt-everywhere-src` is large — the tighter alternative is to mirror
      only the modules whose DLLs/plugins actually ship, at the cost of re-checking that set
      every time the `windeployqt` output changes.)*
- ⏸️ **Update page — DEFERRED past Stage 7 (user, 2026-07-26).** Preferences ships an **Update**
  category that renders only the `prefs.update_hint` placeholder, and no bullet in this stage ever
  claimed it — it was an unowned page. It stays a placeholder: **an update checker is future
  work**, and Stage 7 can be signed off with the page as-is. When it is picked up, the shape of it
  is itself undecided (in-app checker hitting an endpoint — which brings a network call, an
  endpoint to host and a privacy question — versus a static link to a releases page).
- ❌ **DXGI Desktop Duplication (DDA) capture backend** — the Stage 2 deferral that lands here (see
  "Open decisions" below) is still just the comment at `platform/win/WinScreenCapture.h:7`; the
  only backend is GDI BitBlt. **Confirmed in scope for this stage** (user, 2026-07-25) — it is
  the one Stage 2 deferral kept rather than dropped.
  - **Shape of the work (decided 2026-07-26): `BitBlt` stays the default backend; DDA is
    opt-in behind the existing `IScreenCapture` interface, with automatic fallback to `BitBlt`
    on *any* DDA error** — `DXGI_ERROR_UNSUPPORTED`, `DXGI_ERROR_ACCESS_LOST`, or an
    `AcquireNextFrame` timeout. No `core/` or `ui/` change: the fallback happens inside the
    Win32 backend, so callers keep seeing one `grabVirtualDesktop()`.
  - **Why DDA at all — it is a correctness fix, not a speed-up.** `BitBlt` returns black or
    stale pixels for content on a **hardware overlay plane** (MPO: accelerated video playback,
    some games, some HDR paths) — the "I snipped a video and got a black rectangle" bug. DDA
    composites those planes and returns the true image. If that bug is never reported against
    SnapCap, this bullet has little value; **do not** sell it as faster capture.
  - **Why it must not be the default.** DDA is built for *continuous* capture. `AcquireNextFrame`
    only yields a frame when the desktop **changes**, so on a static desktop the first call times
    out and needs a wait-and-retry; add D3D11 device creation + `DuplicateOutput` on top and a
    **cold one-shot grab can be slower than the single `BitBlt` it replaces**. SnapCap freezes
    the screen exactly once per snip — the worst possible fit for DDA's design point.
  - **Failure modes the fallback has to absorb:** hybrid-GPU (the D3D11 device must be created
    on the adapter that *owns* the output, or duplicating an iGPU-attached display from a dGPU
    device fails outright); `ACCESS_LOST` is routine, not exceptional (mode/resolution change,
    fullscreen-exclusive transitions, UAC secure desktop, session lock / fast-user-switch, driver
    reset); and DDA is simply unavailable over RDP and on the Microsoft Basic Display Adapter,
    where `BitBlt` keeps working. **DDA can therefore never be the only backend.**
  - **Neutral / already-handled:** DDA is **per-output**, so `grabVirtualDesktop()` becomes N
    duplications + stitching — `ScreenLayout` already thinks per-monitor and DDA returns raw
    physical px, which matches the capture-px model, but this must not regress the mixed-DPI fix.
    The cursor arrives as separate metadata rather than composited, which suits us since
    `grabCursor()` is already independent. Readback cost is **not** saved (GPU texture → staging
    copy → map). DRM/protected content stays black either way. Needs `d3d11.lib` + `dxgi.lib`,
    both system libraries ⇒ **no LGPLv3 impact**.
  - **Opt-in surface:** **`general/capture_backend`** (decided 2026-07-26), values
    `bitblt` (**default**) | `dxgi`. Selecting `dxgi` is a *request*, not a guarantee — the
    auto-fallback above means a machine that cannot duplicate simply keeps using `BitBlt`,
    so the key never needs a "did it work?" state of its own.

**Data/settings (`[General]`,`[Log]`):** ❌ **none of these exist yet** —
`general/gpu_acceleration=auto` (values `auto`|`cpu` — see the GPU bullet; consumed by
`core/render`'s **blur** dispatch only, and surfaced as the General-page **GPU acceleration**
combo), `general/capture_backend=bitblt`,
`general/startupOnLogin=false`, `general/theme`. (Autostart
shipped under the different key pair noted above, so `general/startupOnLogin` should be dropped
rather than implemented; the accent colour lives at `interface/accentColor`, which is *not*
`general/theme` — and **`general/theme` was never implemented and is now free for reuse**: the
theme system shipped 2026-08-07 keyed **`interface/theme`** instead, because the control sits on
the Interface page and `restoreInterfaceDefaults()` only resets `interface/*`. See the Appearance
bullet.
❌ **`general/process_priority` and `misc/double_tap_interval` — WON'T-DO (user, 2026-07-29).**
Both names are **dropped**, not reserved: nothing reads them, nothing will, and they are free
for reuse. The user closed them rather than implement them, and the two blockers are why —
`process_priority` had an obvious mechanism (a Win32 `SetPriorityClass` at startup) but **no
agreed value vocabulary**, and `misc/double_tap_interval` **had no feature at all**: no
double-tap gesture exists in `Features.md` or the code, so wiring the key would have meant
inventing the gesture it times. Every mention of them is now gone from `Features.md` and
`docs/Usage.md` — including **§4a "Advanced configuration"** (added 2026-07-26), which existed
only to document these two keys and was **deleted** with them.
*(History, superseded: on 2026-07-26 the user decided both keys would be `config.ini`-only and
deliberately **not** surfaced in the Preferences GUI, as power-user escape hatches read straight
from `Settings`; `double_tap_interval` was then **deferred past Stage 7** with its name kept
reserved, explicitly *not* dropped like `startupOnLogin`. 2026-07-29 reverses that last part —
it is dropped exactly like `startupOnLogin` now.)*
`general/capture_backend` — values `bitblt`|`dxgi` — is the DXGI opt-in added
2026-07-26; its consumer will be `platform/win/WinScreenCapture`, **not** a `core/` reader.
**Both `capture_backend` and `gpu_acceleration` store strings, deliberately and consistently:**
a readable value (`bitblt`/`dxgi`, `auto`/`cpu`) beats a magic number in a hand-edited
`config.ini`, and each has exactly two values. *(An earlier revision of this line said
`gpu_acceleration` would keep `0/1/2` integers — that was superseded on 2026-07-26
when its semantics were pinned to `auto`|`cpu`.)*)

**i18n keys:** 🟡 the `about.*` set **shipped 2026-07-26** on the expanded Preferences → About
page (`about.version`, `about.uses_qt`, `about.qt_relink`, `about.qt_source{,_desc,_button}`,
`about.licenses{,_desc,_button}`) — note **`about.title` was deliberately not added**, since
`prefs.tab.about` already titles the page. Still missing: `prefs.gpu` (+ its row description and
the two combo item labels, `prefs.gpu.auto` / `prefs.gpu.cpu`), which lands with the GPU blur
path. **Two of the keys this list used to name are no longer wanted:**
- ~~`prefs.theme`~~ — the selector shipped 2026-08-07 under different keys: **`prefs.iface.theme_mode`**
  for the row label and **`prefs.iface.theme_opt.{auto,light,dark}`** for the three items (plus
  `prefs.desc.theme_mode`). `theme_mode`, not `theme`, for the same YAML reason as `prefs.gpu_accel`:
  `theme_opt` is the parent map of the item labels, and one key cannot be both a scalar and a map.
- ~~`prefs.startup`~~ — **obsolete**, exactly like `general/startupOnLogin`: the feature shipped
  early under Stage 6 and its key is **`prefs.run_on_startup`**. Don't add `prefs.startup`.

(Adjacent keys that already ship and should not be confused with the above:
`prefs.iface.theme_color` = the Interface accent, `prefs.run_on_startup` = the General-tab
autostart toggle, `prefs.about_text`/`prefs.update_hint` = the two placeholders —
`prefs.update_hint` **stays** a placeholder, since the Update page is deferred.)

**Testing:** 🟡 partially done — the **redaction perf benchmark now exists and has been run**
(2026-07-26); the rest of the stage's testing is still untouched.
- ✅ **Perf: mosaic/blur re-render cost — MEASURED, CPU *and* GPU.** The GPU half is measured
  against a **reference OpenGL implementation living in the benchmark itself**, because
  `core/render` still has no GPU path — writing one was the only way to make "CPU vs GPU"
  answerable. It measures the **whole round trip** (QImage → texture → passes → readback →
  QImage), since `render::mosaic/gaussianBlur` return a QImage the caller draws with QPainter;
  kernel-only timings would flatter the GPU and answer a question nobody is asking. `glFinish()`
  bounds each phase, and textures/FBOs are cached across calls of the same size — i.e. the
  numbers are **steady-state cost while dragging a shape**, not cold one-shot cost.
  `test/tst_render_bench.cpp` (built as
  `tst_render_bench`, deliberately **not** `add_test`'d: it asserts nothing about wall-clock,
  which would be flaky across machines, and its 4K cases take seconds — run it by hand).
  Parameters mirror `Annotation.cpp:317,338` (`block = max(6, strength*3*scale)`,
  `radius = max(2, strength*2*scale)`), strength 6 = default, 20 = slider max.
  **Release/MinGW, AMD Ryzen 9 9950X3D, median of 3–5 runs, deterministic xorshift noise:**

  GPU rows are the reference OpenGL path on an **NVIDIA RTX 5090**, whole round trip.

  | case | 960×540 CPU | 960×540 GPU | 4K CPU | 4K GPU |
  |---|---|---|---|---|
  | `mosaic` block 18 (default) | **0.5 ms** ✅ | 1.4 ms ✅ | **7.7 ms** ✅ | 9.4 ms ✅ |
  | `mosaic` block 120 (max) | **0.5 ms** ✅ | 2.3 ms ✅ | **8.4 ms** ✅ | 12.0 ms ✅ |
  | `gaussianBlur` radius 12 (default) | 61 ms ❌ | **0.9 ms** ✅ | 1 117 ms ❌ | **9.3 ms** ✅ |
  | `gaussianBlur` radius 80 (max) | 228 ms ❌ | **0.9 ms** ✅ | 3 689 ms ❌ | **10.1 ms** ✅ |
  | *(ref)* 3× box blur, any radius | 14 ms ✅ | — | 233 ms ❌ | — |

  GPU phase breakdown at 4K: **upload ~4.7 ms + readback ~4.3 ms**, shader **0.19–2.83 ms**.

  **Findings.** (1) **Mosaic should stay on the CPU** — it already meets the budget everywhere,
  and the GPU is *slower* (9.4 vs 7.7 ms at 4K) because the transfer costs more than the work.
  (2) **Blur is the whole case for a GPU path**: 120×–364× faster, radius-independent, and the
  only measured option inside the budget at 4K. (3) The CPU **3× running-sum box blur** is
  radius-independent too and fits the budget at region size (14 ms), so it is the right
  **fallback** for machines that fail the GL probe — but at 233 ms/4K it is not the answer.
  (4) The GPU path is **transfer-bound, not shader-bound**, so the win should survive on far
  weaker GPUs (see the GPU bullet).
  *Fidelity, asserted not just reported: GPU vs CPU max per-channel delta **≤ 1** (mosaic,
  blur, and a split-image case); box-blur vs true Gaussian **4–6**. The GPU tolerance check
  earned its keep — it caught a silent `setUniformValue(name, QPoint)` type mismatch that sends
  a float `vec2` to an `ivec2` uniform, leaving it `(0,0)`; every sample clamped to texel 0, and
  a uniform-colour test image hid it completely.*
  *Caveat: the 233 ms 4K box-blur figure is an **upper bound**, not a tuned floor — the
  prototype's vertical passes use strided access; a transposing implementation would beat it.*
- ✅ **Memory: bounded with many pins (50 pins) — MEASURED 2026-07-29.** Deferred past the stage
  on 2026-07-26 and picked up afterwards, so it confirms rather than gates: the "perf targets met"
  exit criterion was already satisfied by the **render** benchmark alone. Driven against
  `build\SnapCap.exe` by a scripted harness — a **fresh clipboard bitmap per paste** (so no two
  pins can share a `QImage` buffer through implicit sharing, which would have flattered every
  number here), the `F3` global hotkey, `EnumWindows` asserting the live pin count at every
  sample, and `PrivateMemorySize64` as the metric. Machine: two 4K monitors at 150%, so
  clipboard pins take the **display dpr of 1.5**.

  | scenario | pins | growth over baseline | per pin | modeled | ratio |
  |---|---|---|---|---|---|
  | stills 1920×1080 | 50 | 1227.5 MiB | 24.38 MiB | 23.73 MiB (3 copies) | **1.03** ✅ |
  | stills 400×300 | 50 | 78.5 MiB | 1.55 MiB | 1.37 MiB (3 copies) | **1.13** ✅ |
  | animated GIF 640×480×30f | 10 | 381.4 MiB | 38.14 MiB | 37.50 MiB (32 copies) | **1.02** ✅ |

  **Growth is linear**, and a two-point fit over the two still sizes gives
  **`per pin = 3.06 × image bytes + 0.15 MiB`** — every 5-pin step of the 50-pin run was
  121.6–122.4 MiB, so there is no super-linear term. The 3 copies are exactly the ones the code
  holds: `PinModel::m_source`, `PinWindow::m_composed`, and the Qt backing store. `m_composed` is
  a *detached* copy only because `PinWindow::refreshComposed()` calls `setDevicePixelRatio(1.5)`
  on it — `compose()` returns `m_source` implicitly shared for an untransformed pin, so at dpr 1.0
  that copy would cost nothing. An animated pin adds `m_frames` (N), i.e. **N+2**, which is what
  the GIF row measures.
  **Flat under load:** 50 still pins idle 60 s → **−0.5 MiB**; 10 GIF pins *playing* 60 s
  (≈15 000 frame recomposes, each detaching a fresh `m_composed`) → **−0.1 MiB**. No drift.
  **Released on destroy:** 1235.8 → **18.0 MiB** against an 8.8 MiB baseline — a +9.2 MiB
  allocator residue that is stable at +20 s, not retained images.
  **Closed** pins are bounded by the recover ring instead, by design: 12 pins (310.1 MiB) →
  `closeAll` → 97.1 MiB, retaining 10 × one image copy = the `paste/behavior/maxRecoverable` cap.
  **Handles are not the limit either:** +2.2 GDI / +2.8 USER per pin (122 / 152 at 50 pins,
  against the 10 000-per-process default).
  **Verdict: bounded.** Harness + raw per-sample JSON live in the session scratchpad, uncommitted
  — this is a one-off measurement, not a regression test (it needs an interactive desktop and
  steals focus, so it can never be a `ctest` case).
  🐛 **The GIF scenario also found a crash** — destroy-all on ≥6 *playing* animated pins was a
  use-after-free (`0xC0000005`) on the freed `PinModel`. Fixed the same day (`PinWindow::detach()`;
  full write-up in **Stage 5**), after which the scenario ran to completion: 389.4 → **10.0 MiB**
  against an 8.2 MiB baseline, i.e. animated pins release everything they hold too. (The
  destroy-all figure in the paragraph above is the *stills* run, which never crashed — its clock
  is idle — so it was measured before the fix and is unaffected by it.)
- Manual checklist: the **portable ZIP** unpacks and runs as a standalone app on a **clean
  Windows VM** (no dev tools, no install step); the **startup** toggle works; the **GPU
  acceleration** combo switches blur between GPU and CPU; **Preferences → About** shows the
  license text plus the link to our hosted Qt source. **Not runnable yet — no packaged ZIP, no
  About content, no GPU combo** (only the startup toggle exists).
  *(No "theme toggle" check — the theme system was deferred past this stage, so there was nothing
  to toggle when this checklist was written; an earlier revision of this line listed one. It
  **shipped 2026-08-07** and carries its own manual checklist at the Appearance bullet.)*
- **Compliance checklist** (from `Tech-Stack-Plan.md`): ✅ dynamic Qt linking verified (no
  `-static`); ✅ no GPL-only module in `find_package(Qt6 …)`; ✅ `windeployqt` bundles replaceable
  DLLs/plugins — **now part of the build** as of Phase C (an `install(CODE)` step, so the ZIP
  re-derives the DLL set every run), and the packaged tree was verified unsigned, non-read-only,
  dynamically importing every `Qt6*.dll`, and still running after a `Qt6Core.dll` substitution;
  ✅ **license texts
  shipped** (repo root + beside the exe, 2026-07-26); 🟡 **Qt source linked from Preferences →
  About** — the link and the About page ship, but it currently points at the **upstream** Qt
  archive as a placeholder. Self-hosting the matching `qt-everywhere-src-6.11.1` archive on our
  own release page is a **release-time step**, and the item is not ✅ until then. Nothing is being
  distributed yet, so no obligation is live today.
  *(The portable-ZIP decision above makes the "replaceable DLLs" item structurally true —
  nothing signs or seals the shipped tree.)*

**Exit:** the portable ZIP unpacks and runs on a clean VM; perf targets met **for rendering**
(the 50-pin memory check was deferred out of this stage, then measured 2026-07-29 — see Testing);
**LGPLv3 compliance
checklist fully ticked**. Status: **all three are now met, bar one release-time step.**
The **portable ZIP unpacks and runs on a clean VM** — user-verified 2026-07-26 (Phase C). The
**rendering perf target is met** as of Phase B — 4K blur is 10.1 ms on the GPU and the CPU
fallback is radius-independent (the 50-pin memory check, deferred out of this stage, was run
2026-07-29 and passed — see Testing). **Compliance is all but met** — every artifact ships and the deployment is verified
relinkable; the only outstanding item is swapping the placeholder `SNAPCAP_QT_SOURCE_URL` for our
own hosted `qt-everywhere-src` archive, which cannot be done until there is a release page to
host it on. **Phase D (DXGI) is the last piece of stage scope**, and note it is a *correctness*
feature, not an exit criterion — none of the three exit criteria depend on it.

**Build order (planned 2026-07-26):** **A** compliance ✅ → **B** CPU box blur + GPU blur ✅ →
**C** packaging (portable ZIP) ✅ → **D** DXGI. A was sequenced first because it needed no new
machinery; B was ordered before C so the ZIP ships the final DLL set — which proved its worth
immediately, since the GPU path added `Qt6OpenGL.dll` and `windeployqt` had to be re-run to pick
it up; D is last because it is the highest-risk and most isolated.

**Deferred extras — line corrected 2026-07-26 after verifying against the code.** It previously
read *"deferrals: copy-image-as-file, native share"*, which contradicted the project-wide
decision (2026-07-12) that **every feature is in scope**. Verified state:
- ✅ **Copy image as file — ALREADY IMPLEMENTED** (the user was right). `output/clipboard/copyAsFile`
  with three modes (`always` / `hotkey` / `disable`), a Preferences → **Output → Clipboard** radio
  group (`ui/PreferencesDialog.cpp:1786`), and **`Ctrl+Alt+C`** wired in *both* hosts —
  `ui/SnipOverlay.cpp:1737` (`copy(true, asFile=true)`) and `ui/PinWindow.cpp:881`
  (`copyImageToClipboard(asFile=true)`), landing via `IClipboard`'s `CF_HDROP` temp-file format.
  It was never a deferral; the line was stale.
- ❌ **Native share — NOT implemented, and not tracked anywhere.** No share code exists in
  `ui/`, `core/`, or `platform/` (no `IDataTransferManager`, no share-sheet call), **and it is
  missing from `Unimplemented-Features.md`** — so it was invisible in both ledgers. Now added
  there. Remains unscheduled: no stage owns it.

---

## OCR Feature — Text Recognition  *(post-roadmap add-on)*

In-place OCR: the **OCR** button turns a snip region or a pin into a selectable-text surface
(grey word plates → drag-select → copy). Backend = **Tesseract + Leptonica**, static-built
from source with the project's Qt MinGW kit (both LGPLv3-clean; Qt stays dynamic). Ships
English only; the language set is a folder scan of `<exe>/tessdata/`.

- **OCR-Stage 1 — dependency + seams** *(DONE)*: `tools/build_tesseract.ps1` superbuild;
  `IOcr` + `OcrWord/OcrLine/OcrResult` in `platform.h`; `platform/ocr/TesseractOcr`;
  `core/ocr/OcrSelection` (+ `tst_ocr_selection`); CMake gated on `SNAPCAP_HAVE_TESSERACT`.
- **OCR-Stage 2 — shared layer + snip host** *(DONE)*: `ui/OcrTextLayer` + `ui/OcrBar`; the
  `PaintBar` OCR toggle; snip in-place OCR (capture-px mapping), off-thread recognition,
  select/copy/line-breaks/language, `Ctrl+A`/`Ctrl+C`(copy-and-leave)/`Esc`.
- **OCR-Stage 3 — pin host** *(DONE)*: same button + shared layer/bar on `PinEditController`;
  source-px mapping tracks the pin's 90°-step rotation/flip/scale; context menu suppressed.
- **OCR-Stage 4 — Preferences OCR tab + downloader** *(DONE — verified 2026-07-25)*: the **OCR**
  category (`PreferencesDialog::buildOcrTab`, `ui/PreferencesDialog.cpp:1847`) ships all four
  pieces — a **default-language** picker persisting `ocr/language` (empty = "Automatic (follow app
  language)"; read by **both** hosts, `SnipOverlay.cpp:2376` + `PinEditController.cpp:207`), a
  **Languages** card listing `kOcrManifest`'s 12 common languages ∪ any `*.traineddata` drop-in
  found on disk (`installedOcrLanguages()`), per-row **Download** (`QNetworkAccessManager` GET of
  the `tessdata_fast` raw URL straight into `<exe>/tessdata/`, button cycling Download →
  Downloading… → Installed/Remove via `refreshOcrControls()`) / **Remove** (confirm + delete), and
  the **Open language folder** button. `ocrTessdataDir()` deliberately mirrors the backend's own
  datapath so the tab and `TesseractOcr` can't disagree about where packs live.
- **OCR-Stage 5 — docs + i18n** *(DONE — verified 2026-07-25)*: the `ui/PreferencesDialog` + `ui/OcrBar` + `platform/ocr/TesseractOcr`
  rows in `Architecture-FileMap.md`, `Features.md`, and `docs/Usage.md` §2b ("OCR — copying text
  out of a capture", incl. the Preferences → OCR download path) are all in lockstep. Every string
  is a key under `ocr.*` (`ocr.title/bar.*/prefs.*/lang.*`) in `lang/en.yaml`; `tools/i18n_check.py`
  is clean for **en** — its current failures are the 11 non-English locales' missing keys, none of
  which are `ocr.*`, i.e. the user's standing translation to-do, not an OCR gap.

**Exit:** ✅ **met.** OCR works from both hosts on installed languages; new languages are
installable in-app (Preferences → OCR) or by manual drop-in into `<exe>/tessdata/`; docs are in
lockstep. Remaining OCR work is backlog, not unfinished stage scope: accuracy is Tesseract's
(small or low-contrast text and glyph-like bullets misfire — a `•` may read as `5`; zooming a
pin up before OCR helps), CJK word boxes don't tile a line so plates and the highlight are drawn
per line, and in-app translate / TTS are out of scope.

## Milestone map

| Milestone | Stages | Outcome |
|---|---|---|
| **M0 Foundation** | 0–1 | Tray app, remappable global hotkeys, Action layer, i18n, settings |
| **M1 Capture** | 2–3 | Snip + smart detect + color picker → clipboard/file |
| **M2 Editor** | 4 | Full annotation engine (golden-tested) |
| **M3 Pin** | 5 | Floating pin windows (signature feature) |
| **M4 Persistence** | 6 | History, backup/restore, full Preferences, live language switch |
| **M5 Release** | 7 | Packaged, performant, LGPLv3-compliant build |

## Traceability to `Features.md`

- §1 Snip → Stages 2, 3 (+ history `,`/`.` in 6).
- §2 Annotation → Stage 4.
- §2b OCR → the OCR Feature section above (post-roadmap add-on).
- §3 Paste/Pin → Stage 5.
- §3b Screen Recording → no stage; its own phases 0–5 (see the note in Stage 7). §3c Scrolling
  Capture (2026-08-09) → no stage either; what it established lives in
  `Features.md` §3c and the `SCR` rows of `Architecture-FileMap.md`. Open gaps for both:
  `Unimplemented-Features.md` §3b / §3c.
- §4 Key Bindings → the Action/Command layer + per-stage "Key bindings handled" (contexts:
  Global=Stage 1, Snip=2/3, Edit=4, Pin=5).
- §5 System & Infrastructure → tray/hotkeys/settings (1), formats (2), history/prefs/backup
  (6), GPU (7); themes were deferred out of 7 and shipped 2026-08-07.
- §6 Portability → `platform/` interfaces (`platform.h`), Windows backends across 1/2/3/5/7.

## Open decisions (carry-over)

- ~~Qt **version** pin: 6.2.x LTS vs latest 6.x.~~ **Resolved: Qt 6.11.1**
  (MinGW kit), the version the mirrored LGPLv3 source release (`qt-6.11.1`) matches. A bump is
  manual — see `docs/Development.md` → Pitfalls.
- ~~Capture backend: ship BitBlt first, add DXGI Desktop Duplication in Stage 7.~~ **Resolved
  2026-07-26: `BitBlt` stays the *default* permanently; DDA is an opt-in backend with automatic
  fallback to `BitBlt` on any DDA error.** It is a correctness fix for hardware-overlay
  (black-video) capture, not a speed-up. ⏸️ **Deferred out of Stage 7 at close-out
  (2026-07-26) and not yet built** — the shape is decided, only the implementation is
  outstanding; see Stage 7 Phase D and `Unimplemented-Features.md` §5.
- ~~Packaging: `windeployqt` bundle now, Inno Setup / MSIX installer later.~~ **Resolved
  2026-07-26: portable ZIP only, no installer** — see the Stage 7 Packaging bullet for the
  LGPLv3-relinking rationale and why MSIX in particular is ruled out.
- Every feature is **in scope** (decided 2026-07-12): all are planned for implementation, with no
  paid tier (the architecture already leaves room). The `[PRO]` tag that marked some of them was
  retired 2026-10-20.
