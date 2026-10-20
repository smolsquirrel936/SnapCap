# SnapCap — User Guide

A practical, feature-by-feature reference: **in what situation, which action (click / drag /
hotkey) produces which effect**, plus the tips and warnings that aren't obvious from the key
tables.

> This guide describes **how SnapCap actually behaves today**, verified against the source —
> the snip/pin input handlers (`SnipOverlay`, `SnipScreenView`, `PinWindow`), the action
> registry, `TrayIcon`, and the settings code.
>
> **Platform:** Windows 11 / 10. **Modifier note:** all shortcuts below use `Ctrl` (Windows).

---

## Contents

- [**0.** Launching & global hotkeys](#0-launching--global-hotkeys)
- [**1.** Snip — screen capture](#1-snip--screen-capture)
- [**2.** Annotation — drawing on a capture](#2-annotation--drawing-on-a-capture)
- [**2b.** OCR — copying text out of a capture](#2b-ocr--copying-text-out-of-a-capture)
- [**2c.** Recording — capturing a region to video](#2c-recording--capturing-a-region-to-video)
- [**2d.** Scrolling capture — content bigger than the screen](#2d-scrolling-capture--content-bigger-than-the-screen)
- [**3.** Paste / Pin — floating image windows](#3-paste--pin--floating-image-windows)
- [**4.** Preferences & portability](#4-preferences--portability)
- [**4a.** Advanced configuration](#4a-advanced-configuration)
- [**5.** Tips & warnings at a glance](#5-tips--warnings-at-a-glance)

---

## 0. Launching & global hotkeys

SnapCap is a **tray application** — launching `SnapCap.exe` shows **no window**, just a tray
icon. On Windows 11 the icon usually hides inside the `^` overflow flyout on the taskbar.

**Situation → action → effect**

| You want to… | Do this | Result |
|---|---|---|
| Start a snip | Press `F1`, or **left-click** the tray icon | The freeze-frame capture overlay appears on every monitor |
| Snip and immediately copy | Press `Ctrl + F1` | Snips and copies the whole result flow in one shot |
| Start a custom/delayed snip | Press `Shift + F1` | Captures a preset X/Y/W/H region after the configured delay (set via **Preferences → Control → Custom snip → Configure**) |
| Snip the active window | Press your **Snip active window** hotkey (unbound by default — assign one under **Preferences → Control**) | Opens a snip with the frontmost window's frame already selected and ready to edit; you can still resize it or pick a different region |
| Quick snip (hold + drag) | Set a **Quick snip** modifier combo under **Preferences → Control** (unbound by default): click the box and **hold** the modifiers (Ctrl/Alt/Win) — release to capture e.g. `Ctrl+Alt`. Then, anywhere on the desktop, **hold that combo and drag** a region | The dragged region is captured and your chosen action runs at once — **Copy** and/or **Pin**, no toolbar. Pick the action(s) next to the box. A plain modifier+click (no drag) is left alone, so normal Ctrl/Alt-clicks still work |
| Record a region to video | Start a snip (`F1`), then click the **movie-camera** button on the snip toolbar | Opens the recording row — Record, Pause and a countdown/elapsed timer. There is **no global hotkey** for recording; it always begins as a snip (see §2c) |
| Paste the clipboard to screen | Press `F3`, or **middle-click** the tray icon | Turns current clipboard content into a floating pin window |
| Hide / show all pins | Press `Shift + F3` | Toggles visibility of every floating window at once |
| Close all pins | Tray menu → **Close All Pins**, or assign a **Close all** hotkey under **Preferences → Control** (unbound by default) | Closes every floating window at once — **recoverable** with `F3` (see §3) |
| Destroy all pins | Tray menu → **Destroy All Pins**, or assign a **Destroy all** hotkey under **Preferences → Control** (unbound by default) | Destroys every floating window at once — **permanent, not recoverable** |
| Open the menu / Preferences | **Right-click** the tray icon | Tray menu with Preferences, Groups, Quit, etc. |

> **Global hotkeys are remappable** in **Preferences → Control (Key Bindings)**, where they're
> grouped into **Core / View / Tools / Misc** sections with a **search box** at the top to jump
> to one by name, description, or shortcut. The defaults live in `res/keymap.default.yaml`; your
> overrides are stored in `config.ini`. A "Toggle mouse click-through" global hotkey exists but
> is **unbound by default** — assign it yourself if you want click-through pins (see §3).

> ⚠️ **Only global hotkeys are user-remappable.** The in-overlay snip keys and the in-window
> pin keys below are currently **hardcoded** — remapping them in `config.ini` changes the tip
> labels but not the actual behavior.

> **Exempt specific apps from the hotkeys.** Under **Preferences → Control → App exclusions**,
> click **Add…** to pick a currently-running app or browse for its `.exe`. While one of the
> listed apps is the active (foreground) window, **all** of SnapCap's global hotkeys are released
> so the keys reach that app instead — handy when a game or remote-desktop session needs a key
> SnapCap has claimed (e.g. `F1`). The moment you switch to another window, the hotkeys work
> again. Apps are matched by executable name (e.g. `chrome.exe`); the list is stored in
> `config.ini` under `[control] ignoreApps`. (Quick snip is a separate gesture and isn't affected.)

---

## 1. Snip — screen capture

Pressing `F1` **freezes** every screen and shows a dimmed overlay. Everything you do next
happens on that frozen image until you finish or abort.

### 1.1 Choosing the region

| You want to… | Do this | Result |
|---|---|---|
| Auto-select a window/element under the cursor | Move the mouse (detection is on) | A highlighted suggestion snaps to the window or UI element |
| Switch detection mode | `Tab` | Cycles **off → window → element → off** |
| Widen/narrow the detected element | **Scroll wheel** (element mode, before selecting) | Scroll **up** to select the enclosing parent element, **down** to go back into the child. Steps outward until the whole window is selected |
| Draw a region by hand | **Left-press and drag** | A selection rectangle follows the drag |
| Draw a **square** region | Hold `Ctrl` while dragging it out | Width and height stay equal for the whole drag |
| Move a committed region | Drag inside it, or `↑ ← ↓ →` (see §1.2) | Repositions the selection (grab an edge/corner handle to resize it) |
| Resize keeping the **same ratio** | Hold `Shift` and drag a **corner** handle | The region grows/shrinks at its current width:height ratio; the opposite corner stays put |
| Resize into a **square** | Hold `Ctrl` and drag a **corner** handle | The region grows/shrinks with width = height |
| Select the entire (virtual) desktop | `Ctrl + A` | Sets the selection to fullscreen across all monitors and enters edit mode |
| Confirm the highlighted suggestion | **Left-click** it | Commits that region (enters read-only / edit-ready state) |
| Abort while choosing | **Right-click** (before a shape/text is in progress) | Closes the overlay and discards the snip (see §1.7) |

> 💡 Snip works across a **multi-monitor, mixed-DPI** setup — a single drag can span two
> screens at different scale factors and the result is stitched correctly. This is the app's
> trickiest internal model; if you ever see a 1-px seam at a monitor boundary, that's the area
> to report.

> 💡 **A slip of the hand while clicking won't cost you the window.** Nudging the mouse as you
> click used to be read as a drag, leaving you with a useless few-pixel region instead of the
> window you were aiming at. Now a quick press only becomes a drag once it travels far enough —
> about 30 px within the first 300 ms, then 10 px, and after holding for a full second any real
> movement counts. So a flick-and-slip still selects the highlighted window, while a deliberate
> small region stays possible: press, pause a beat, then drag. This only applies when there's a
> highlighted suggestion to fall back on — with detection off (`Tab`), every drag is taken
> literally. The distances are adjustable, see [§4a](#4a-advanced-configuration).

> 💡 **Element hierarchy:** once you scroll out to a parent element, that level sticks while you
> stay on the same element — small hand movements won't snap you back. Moving onto a *different*
> element starts again from the innermost one. The first scroll on an element can take a moment
> while the app asks the target app for its element hierarchy (browsers are the slow case);
> after that, scrolling is instant. Only elements the target app actually exposes can be
> selected, so decorative page boxes, canvas drawings and video contents may have no step of
> their own.

### 1.2 Pixel-precision adjustment

| You want to… | Do this | Result |
|---|---|---|
| Nudge the **cursor** 1 px | `W` `A` `S` `D` | Moves the OS pointer up / left / down / right by one pixel |
| Move the **region** 1 px | `↑` `←` `↓` `→` | Shifts the whole selection by one pixel |
| Grow the region 1 px | `Ctrl + ↑ ← ↓ →` | Pushes the corresponding edge outward |
| Shrink the region 1 px | `Shift + ↑ ← ↓ →` | Pulls the corresponding edge inward |
| Type an exact X, Y, W or H | **Click that number** in the `( x , y )` / `w x h` readout above the region | Opens a small white input on the number; type a new value |

> ℹ️ The four numbers in the readout are **click-to-edit** once the region is committed (no
> annotation tool active — the same state that shows the resize grips). `Enter` or a click
> elsewhere confirms, `Tab` moves to the next number, `Esc` cancels just that edit and leaves
> the snip alone. Editing **W or H keeps the top-left corner fixed**; editing X or Y moves the
> whole region without changing its size.

> 💡 **Recommended gesture:** hold **Mouse-left** and tap `W A S D` to drag-size the selection
> while fine-positioning the cursor a pixel at a time. Arrow keys are most useful **after** a
> rough drag, to land the selection edges on exact pixels using the magnifier.

> ℹ️ `W A S D` moves the cursor by exactly one **physical (capture) pixel** per press at any
> display scale, and the **magnifier loupe, color readout, and coordinate readout advance one
> physical pixel with it** — so on a 150 %/4K monitor you get true single-pixel targeting, not
> the rounded ~1.5 px logical step.

### 1.2a Rounded corners

The captured region is a plain rectangle by default. If you want the corners rounded off,
turn the control on first: **Preferences → Snip → Display → "Show rounded corner option while
snipping"**.

With it on, a chip appears on the readout row above the region, just right of the `w x h` box —
a corner icon plus a number:

| You want to… | Do this | Result |
|---|---|---|
| Round the corners more / less | **Scroll** the wheel over the chip | Steps the bending level by 1 (0–100); scrolling also switches the shape to rounded |
| Switch back to right angles | **Click** the icon | Corners go square; the number disappears |
| Go back to rounded | **Click** the icon again | Returns to the same bending level you had |

- The icon always shows the shape currently in force: a sharp corner or a curved one.
- The number is shown **only in rounded mode** — with right angles there is no bend to report,
  so the chip shrinks to just the icon. Your level is still remembered: clicking back to rounded
  brings it, and its number, straight back.
- The level is in **logical pixels**, so a corner looks the same size on a 100 % and a 150 %
  monitor, and it is **clamped to the region** — on a region too small for the number you set,
  the corners round as far as they can while the chip keeps showing your number.
- Both the shape and the level are **remembered between snips**.
- The chip appears under the same rule as the ⟳ button and the click-to-edit numbers: only once
  the region is committed and no annotation tool is active.
- Copies and saves get **transparent** corners. Formats that can't store transparency
  (JPG, BMP) get **white** corners instead of the black they'd otherwise produce.

### 1.2b Refreshing the frozen screenshot

Entering snip mode **freezes** the desktop, so anything that changes underneath — a loading
page, a clock, a video — is stuck at the moment you pressed `F1`. Refresh re-grabs it without
leaving the overlay:

| You want to… | Do this | Result |
|---|---|---|
| Refresh once | `F5`, or **click** the ⟳ button on the readout row above the region | Re-grabs the live desktop and repaints the frozen background |
| Keep refreshing | **Hold** `F5`, or **press and hold** the ⟳ button | Keeps re-grabbing up to ~30×/second while held (slower if a full-desktop grab takes longer); the view locks on the last frame when you let go |

- Your **selection, annotations and undo history survive** a refresh — only the background
  pixels change. The barcode chip re-scans against the new pixels.
- `F5` works **before** you drag a region as well as after; the ⟳ button only appears once the
  region is committed and no annotation tool is active (same rule as the click-to-edit numbers).
- Nothing blinks. The overlay asks Windows to leave it out of any screen capture, so the mask,
  selection and toolbar stay perfectly steady while the desktop behind them updates live.
- The captured **cursor** is *not* re-grabbed: while snipping, the on-screen pointer is
  SnapCap's own crosshair, so the export keeps the real cursor from the moment you pressed `F1`.
- Refreshing while browsing snip history (`,` / `.`) returns you to the **live** desktop.

### 1.2c Barcodes & QR codes in the capture

Whenever you settle a region, SnapCap scans it for a barcode. Both **2D** codes (QR, Micro QR,
rMQR, Aztec, DataMatrix, PDF417, MaxiCode) and ordinary **1D** barcodes (EAN-8/13, UPC-A/E,
Code 128/39/93, ITF, Codabar, GS1 DataBar and DataBar Expanded) are recognised — 18 symbologies
in all. If it finds one, a third chip appears on the readout row above the region — a small code
icon followed by the decoded payload:

| You want to… | Do this | Result |
|---|---|---|
| See the **full** link | **Hover** the chip | A tooltip shows the complete payload, however long |
| **Copy** the link | **Click** the chip | Copies the full payload; the chip reads "Copied" for 2 seconds and the snip stays open |
| **Open** the link | **Ctrl + click** the chip | Opens it in your default browser and closes the snip |

> ℹ️ The chip is sized to the **region's width**, so a long link is shortened to its domain and
> its ending — `github.com/…issues/12345`. Copying and opening always use the **full** link, not
> the shortened text. Hover to read the whole thing.

> ℹ️ Only **http/https** links can be opened — `Ctrl + click` does nothing for other payloads
> (WiFi credentials, contact cards, plain text, and the plain numbers a 1D barcode carries).
> Those still show and still **copy** normally.

**Scan a code in one gesture.** Assign a hotkey to **Scan and copy barcode/QR code** under
**Preferences → Control → Tools** (unbound by default). Pressing it opens a normal (dimmed)
snip — drag a region around a code and it's decoded, copied to the clipboard, and the snip
closes automatically. If the region holds no readable code, a **"No barcode found"** notification
appears and the snip closes; `Esc` or a right-click cancels before you release.

> ℹ️ Like the numbers beside it, the chip is only clickable once the region is committed and no
> annotation tool is active. If a region contains several codes, the first one found is shown.
> Re-scanning happens every time the region changes, so moving or resizing onto a different code
> updates the chip.

### 1.3 Magnifier (loupe), guide lines & color picker

The magnifier auto-appears while you're choosing a region — there's no key to force or hide it.
It shows zoomed pixels, the live selection size, and the color under the cursor — one swatch
plus a row per color format you enabled under Preferences → Interface → Magnifier → Color values.

| You want to… | Do this | Result |
|---|---|---|
| Toggle full-screen crosshair guide lines | `G` | Shows/hides the auxiliary lines running from the cursor to the screen edges (the setting persists to Snip → aux lines) |
| Choose which color format to copy | `Shift` | The readout lists every format you enabled in Preferences; `Shift` moves the highlight down the list |
| Copy the pixel color | `C` | Copies the color under the cursor to the clipboard, in the highlighted format |

> ⚠️ `G` toggles the **crosshair guide lines** — and nothing forces the magnifier
> (`Alt` is the quick-mark modifier, §2.2).

> 💡 After copying a color with `C`, you can `F3` to paste it as a **color card** pin, or
> `Ctrl + V` it into another program.

**Appearance.** Under **Preferences → Interface → Magnifier** you can also turn off the
**grid lines** drawn between the magnified pixels, hide the **coordinates** line at the top of
the readout, and set the **magnifier size** from 50 % to 200 % (the loupe and its readout panel
scale together). The loupe and the readout are always the same width: enabling a long format such
as CMYK widens both, the loupe simply showing a few more pixel columns with the sampled pixel
still at its centre. Changes apply the next time you start a snip, not to one already open.

**Pick color without a full snip.** Assign a hotkey to **Pick color** under **Preferences →
Control → Tools** (unbound by default). Pressing it freezes the screen at full brightness and
shows just the loupe — move the cursor, then **left-click** (or press `C`) to copy the pixel
color and close; `Shift` cycles the format as usual, and `Esc` or a right-click cancels.

### 1.4 Capturing the mouse cursor

| You want to… | Do this | Result |
|---|---|---|
| Toggle showing the captured cursor | `` ` `` (backtick) or `!` | Shows/hides the OS pointer in the capture, live and in the export |

- The cursor is snapshotted **once, at freeze time** — it's the real OS arrow from the moment
  you pressed `F1`, not SnapCap's crosshair.
- If the pointer was **outside** your selected region, it simply won't appear (expected).
- The toggle **persists** to Preferences (Snip → Capture the cursor).

> ⚠️ If **Preferences → Snip → "Disable cursor capture"** (the master switch) is on, the
> `` ` ``/`!` key is a **no-op** — the toggle is locked off.

### 1.5 Snip history — two independent systems

SnapCap has **two** separate "history" mechanisms. Don't confuse them:

**(a) History strip — past *successful* captures (persisted to disk)**

| You want to… | Do this | Result |
|---|---|---|
| Open the history strip / step to an older snip | `,` | Opens a thumbnail strip of recent successful snips; steps older |
| Step to a newer snip | `.` | Moves the highlight toward more recent snips |
| Pick the highlighted snip | `Enter`, or **click** a thumbnail | Restores that capture into the overlay (edit-ready) |
| Close the strip | `Esc` (or **right-click**) | Closes the strip only, without aborting the snip |

- Only **successful** snips (ones you copied/saved/pinned) are recorded.
- Count is set by **Preferences → Snip → history records** (default 20).

**(b) Area reuse — recall a past selection *rectangle* (this session only)**

| You want to… | Do this | Result |
|---|---|---|
| Reuse an older selection area | `R` | Restores an earlier region rectangle and stays in the overlay |
| Reuse a newer selection area | `Shift + R` | Steps toward more recent regions |

> ⚠️ **Area history is session-only** — it resets every time you restart SnapCap (this is a
> deliberate design choice, not a bug). The history *strip* (`,`/`.`) survives restarts; the
> area ring (`R`/`Shift+R`) does not.

### 1.6 Finishing a snip ("successful snipping")

| You want to… | Do this | Result |
|---|---|---|
| Copy to clipboard | `Ctrl + C`, or `Enter`, or **double-click** the region | Copies the result and closes the overlay |
| Copy as a **file** | `Ctrl + Alt + C` | Copies as a file object (must be enabled in Output → Clipboard) |
| Save to a file | `Ctrl + S` | Opens the Save dialog |
| Quick-save (no dialog) | `Ctrl + Shift + S` | Silently saves to your Quick-Save folder |
| Pin to screen | `Ctrl + T`, or **middle-click** the region | Turns the capture into a floating pin window (see §3) |

> 💡 **Double-click** and **middle-click** run *configurable* actions (Preferences → Snip).
> Defaults: double-click = copy & close, middle-click = pin to screen.

#### File formats

The Save dialog offers **PNG, JPEG, BMP, ICO, TGA**, **GIF** and **PDF**. The format follows the
extension you pick, and the one you chose last is offered first next time (Preferences →
Output → Manual Save).

Only formats SnapCap can actually write are listed, so the list can differ between machines:
**TIFF** appears once Qt's optional *Qt Image Formats* module is installed alongside the app.

> 💡 **GIF** stores at most 256 colours. A capture of flat UI, text or a diagram usually fits
> inside that and is saved **exactly**; photos and gradients are dithered instead, which is
> normally invisible. For screenshots GIF is often *smaller* than PNG.

> 💡 **PDF** gives you a one-page document holding just your image — no margins, no letterbox,
> and the pixels are stored **losslessly**, so text stays crisp however far you zoom in.
> Preferences → Output → **Save to PDF** picks the page size: **96 DPI** (the default — the page
> matches the image's size on screen), **72 DPI** (one pixel per point, the largest page), or
> **Fit to A4** (a standard A4 page, turned to match the image and scaled to fit). Transparency
> is filled with white, as on any printed page.

> ⚠️ **ICO** cannot exceed **256 × 256 pixels**. Saving a larger capture as `.ico` shrinks it,
> so SnapCap asks first and tells you the exact size it would become — pick another format if
> you want the full resolution.

If a save fails (a read-only folder, a full disk), SnapCap now says so instead of doing
nothing quietly, and the snip is **not** added to your history.

### 1.7 Aborting a snip

| You want to… | Do this | Result |
|---|---|---|
| Cancel the snip | `Esc`, or **right-click** (when not editing), or the overlay **X** button | Discards everything and closes the overlay |
| — | Switching focus to another app's window | Auto-aborts (toggleable in Preferences). **Not while recording** — a recording hands the desktop back to the app you are filming on purpose, so clicking it never ends the session |

> ⚠️ While a **text box** is open or a **line** is being placed, **right-click** does *not*
> abort — it **finishes** the current shape/vertex instead. Press `Esc` to actually cancel.

**Ask before discarding.** Preferences → Snip → Keys → *"Ask for confirmation when Esc is
pressed"* puts a "Discard this capture?" question in the way of `Esc`. It only appears once you
have **finished choosing the region** — while you are still dragging one out there is nothing to
lose, so `Esc` just leaves. The question carries a **Don't ask again** box: tick it and press
**Yes** and the setting turns itself off. Ticking it and answering **No** changes nothing.

---

## 2. Annotation — drawing on a capture

Once a region is committed, a **paint bar** lets you draw on it. This applies to both a fresh
snip and a pin (see §3.6).

| You want to… | Do this | Result |
|---|---|---|
| Show / hide the paint bar | `Space` | Toggles the annotation toolbar |
| Pick a tool | Click it on the paint bar | Shapes (rectangle / ellipse), lines & arrows, pencil, marker, redaction (mosaic / blur), text, number label, eraser — picking a group expands its variants on a second row |
| Confirm a line / arrow | **Right-click** | First right-click ends placement and turns the vertices into draggable control points; a second right-click commits the shape |

> 💡 **Arrow vs. line — one tool, not two:** the paint bar has a single **lines** button. An
> arrow is just a line whose end carries an arrowhead: the sub-bar sets each end's decoration
> separately (§2.1); out of the box a line has a sharp arrowhead at its end. A multi-segment
> line is that same tool, just click-placed instead of dragged (§2.1). There is **no `Tab` toggle**
> between them while editing — `Tab` only cycles region detection (§1.1).

> 💡 **Outline or filled:** the **rectangle / ellipse** sub-bar has one **fill toggle** beside the
> width dot. Its icon shows the current mode, an outline square (circle for the ellipse) or a
> solid one, and a click flips to the other. While a shape is filled the width dot is greyed out,
> since a filled shape has no outline to size.

> 💡 **Dashed strokes:** the **rectangle / ellipse** and **pencil** sub-bars carry a **line-style
> dropdown** just left of the colour swatches — solid, dashed, dotted, dash-dot or dash-dot-dot.
> Each of the three tools remembers its own choice across snips and restarts. The dropdown
> disappears when a rectangle/ellipse is set to **solid fill**, since a filled shape has no
> outline to dash. Dash lengths scale with the pen width, so a fatter stroke gets longer dashes.
> The **line** tool has the same five patterns in its own dropdown (see §2.1), plus two extra
> tapered styles that only make sense on a line.

### 2.1 Drawing specifics

- **Single line / arrow:** press at the start, drag to the end, release. The line then sits in
  its **control phase** — drag either end to adjust, then **right-click** to commit it. Unlike
  the drag tools below, releasing the mouse does *not* finish it.
- **Multi-segment line:** click successive points; each click (or a double-click) adds a vertex.
  **Right-click** ends placement (with only one vertex placed, it discards the line instead),
  leaving draggable control points; **right-click again** to commit.
- **Line ends and line type:** the line sub-bar carries three dropdowns, left to right —
  **start of the line**, **line style**, **end of the line**. The two end dropdowns offer the same
  ten decorations (plain, four arrows - sharp, round, chevron, hollow - filled and
  hollow dot, filled and hollow diamond, and a bar), so the two ends are set independently: an
  arrow at both ends, a dot at one and an arrow at the other, or nothing at all. The line style
  offers the same five dash patterns as the pencil (solid, dashed, dotted, dash-dot, dash-dot-dot)
  plus two the line tool alone has: **tapered** and **tapered hollow**, which widen the line from
  its start toward its end. A tapered line can't also be dashed — it's drawn as a solid shape
  rather than a stroke — so those are alternatives in the one list. Arrowheads always stay solid,
  even on a dashed line. All three choices are remembered across snips and restarts, and changing
  one while a finished line is selected restyles that line.
- **Curved line:** the **mode button** at the left end of the line sub-bar, just before the width
  dot, switches the line tool between **straight segments** and a **smooth curve**. It is not a
  press-in toggle — its icon *shows you the current mode*: a **zigzag** means straight segments, a
  **wave** means curve. Click it to switch; your choice is remembered across snips and restarts.
  Nothing about the drawing gesture changes: click the same points, right-click to end placement,
  right-click again to commit. The curve passes **through every point you clicked**, so the
  control points still land where you put them and dragging one reshapes the curve around it.
  It combines with the end dropdowns — a curved shaft can carry any of the end decorations, which
  follow the curve's direction where it ends.
- **Drag tools** (rectangle, ellipse, pencil, marker, mosaic, blur): the shape commits when you
  **release** the mouse — there's no separate "finish" step. A right-click while nothing is in
  progress **aborts the whole snip** (see §1.7), so don't use it to "confirm" a drag shape.
- **Constrained shapes** — hold a modifier while drawing:
  - `Ctrl` → rectangle becomes a **square**, ellipse a **circle** (also mosaic, blur, eraser-rect
    and marker-rect, which are all rectangle drags)
  - `Shift` → line / arrow snaps to **horizontal / vertical / 45°**. This works while dragging
    the line out, while click-placing each vertex of a multi-segment line, and — on a
    **single-segment** line — while dragging either end's control point afterwards. (A middle
    vertex of a multi-segment line has two neighbours, so there's no single angle to snap to;
    those stay free.)
  - `Shift` → **pencil / marker** draw a straight stroke from where the drag began, at any
    angle; `Ctrl` → the same stroke locked to **horizontal / vertical / 45°** (a near-horizontal
    highlight lies flat). Both work mid-stroke: press or release the modifier while dragging to
    switch between straight and freehand, and `Ctrl` wins if you hold both

### 2.1a Re-selecting a committed shape

A rectangle, ellipse, mosaic, blur or marker-rect you've already drawn can be picked back up and
edited — no need to undo and redraw. This works **whatever tool is active**: the shape's own body
is what you click, so an empty-canvas drag still draws a new shape.

| You want to… | Do this | Result |
|---|---|---|
| See a shape is editable | **Hover** over it | A dashed outline traces the shape (an ellipse gets an *elliptical* outline, not a box). A **hollow** (unfilled) rectangle/ellipse lights up only when you hover its **outline** — its empty interior isn't part of the shape; a **filled** one lights from anywhere inside |
| Select it | **Click** the outlined shape | The dashed outline turns into 8 resize grips (corners + edge midpoints), a spin handle above the top edge and a red **×** delete button above the top-right corner |
| Move it | **Drag** its body | Repositions the whole shape |
| Resize it | **Drag** a grip | Corner grips resize both axes, edge grips one axis; the **opposite** grip/edge stays pinned. Hold `Ctrl` for a square / `Shift` to keep the aspect ratio |
| Rotate it | **Drag** the **spin handle** above the shape | Rotates about the shape's centre; hold `Shift` to snap to 15° |
| Restyle it | Change **colour / width / fill / line style / mosaic strength** on the paint bar while it's selected | Re-styles the selected shape in place |
| Change its width / strength | **Mouse scroll** anywhere while it's selected | Rectangle/ellipse pen width, or mosaic/blur strength, changes live and directly on the shape (one `Ctrl + Z` per scroll) |
| Delete it | Click the **×** button, or press `Delete` / `Backspace` | Removes the selected shape |
| Deselect | `Esc`, click empty canvas, or switch tools | Drops the selection |

- **A shape you've just drawn is auto-selected** the moment you release the drag — its grips,
  spin handle and × button appear right away, so you can tweak it without clicking it again.
  (Drawing the next shape on empty canvas drops the selection as usual.)
- Freehand strokes (pencil, freehand marker, brush mosaic/blur, eraser), lines/arrows, text and
  number labels are **not** re-selectable this way — text and number labels have their own
  re-edit gestures (§2.4, §2.5).
- Every move / resize / rotate / restyle / delete is a single `Ctrl + Z` step.

### 2.2 Quick-mark a UI element

Instead of dragging a shape around a button, field or toolbar item, let SnapCap size it for you.

| You want to… | Do this | Result |
|---|---|---|
| Preview the element under the cursor | Hold `Alt` and move the mouse | A dashed box outlines the UI element that would be marked |
| Mark it | `Alt` + **right-click** | Creates the shape filling exactly that element's bounds |

- Works with **rectangle, ellipse, marker, mosaic, blur and eraser**. Ideal for one-click
  redaction — hold `Alt` over a username field with mosaic selected and right-click.
- A quick-mark is always a **rectangle**, so the marker/mosaic/blur/eraser tools mark as
  their *rect* mode even when the toolbar has them in freehand mode. Your toolbar setting
  is untouched: the mark is a rect, your next hand-drawn stroke is still freehand.
- **Pencil, lines / arrows, text and number label** ignore the gesture — there's no
  sensible rectangular version of them. With those active, right-click behaves as usual.
- Detection runs whenever `Alt` is held, even if you cycled region detection **off** with `Tab`
  while choosing the area (§1.1).
- `Alt` does nothing else during a snip, so holding it is unambiguous. (The crosshair guide
  lines moved to `G` — §1.3.)
- The dashed preview is **not** trimmed to your snip region. If the element sticks out past the
  region, the box shows that — the mark keeps the element's real bounds, and the part outside
  the region simply doesn't appear in the saved image.
- A **plain** right-click is unchanged: it still aborts the snip (§1.7).

### 2.3 Pen width & editing history

| You want to… | Do this | Result |
|---|---|---|
| Change pen/tool width | **Mouse scroll** (with a tool active) | Scroll down / up decreases / increases stroke width or eraser size |
| Undo | `Ctrl + Z` | Reverts the last edit |
| Redo | `Ctrl + Y` | Re-applies a reverted edit |
| Clear **all** edits | `Ctrl + Shift + Z` | Removes every annotation |
| Re-edit a finished shape | `Ctrl + Z` until it disappears, then `Ctrl + Y` | Brings the shape back into an editable state |

> ⚠️ **`Ctrl + Shift + Z` (clear all) cannot be undone.** Everything you've drawn is gone.

### 2.4 Text

| You want to… | Do this | Result |
|---|---|---|
| Scale a text box | Drag its **corner** grips | Resizes the text |
| Rotate text | Drag the **rotate handle above** the box | Rotates the text box |
| Bold, italic or outline text | The **B**, *I* and outlined-**A** toggles on the text sub-bar | Outline gives the coloured letters a white border, so they stay readable on any background |
| See what a font looks like | Open the **font family** drop-down and **hover** a name | A specimen panel appears beside the list, showing sample text drawn in that font |

**Font preview.** You don't have to select a font to see it. Hovering any name in the
family drop-down pops up a panel alongside the list with a pangram and the digits 0–9
rendered in that typeface; fonts that cover Chinese get a third line of Han characters.
It follows the cursor from row to row and disappears when the drop-down closes. The
specimen is always plain and one fixed size — the **B** / *I* toggles and the size box
don't change it, so you're comparing typefaces, not styling.

### 2.5 Number labels

Stamps auto-numbering badges — ①②③ — for step-by-step callouts. Pick the tool (right of
**text**), and the cursor becomes the exact badge your next click will drop.

| You want to… | Do this | Result |
|---|---|---|
| Stamp the next number | **Click** inside the capture | Drops the badge centred on the click; the counter advances by 1 |
| Choose the look | The **first three** buttons on the 2nd-level bar | Solid · solid with a white ring · outline-only (transparent inside) |
| Choose the numbering | The **dropdown** right of those three | `1.2.3` · `A.B.C` · `a.b.c` · `I.II.III` · `i.ii.iii` — see below |
| Resize the next badge | **Mouse scroll** (not over an existing badge) | Grows/shrinks the badge and its digit; the size box follows |
| Resize **one** badge | **Mouse scroll** *over* that badge | Resizes only the badge under the cursor |
| Move a badge | **Drag** it | Repositions it (undoable) |
| Renumber a badge | **Click** it, type digits, `Enter` | The first digit replaces the whole number; `Esc` cancels |
| Reorder a badge | Click it, then the **+ / −** buttons (or `↑` / `↓`) | Moves it one place in the numbering — see below |
| Delete a badge | Click it, then the red **×** above it (or `Del`) | Removes it; the badges above close the gap |
| Restart the count at 1 | Re-pick the number-label tool | The counter resets whenever the tool is (re-)selected |

**Reordering with + / −.** Clicking a badge puts small **+** and **−** buttons on the right
of its dashed outline. They don't just change one number — they **swap** with whichever badge
currently holds the number you're moving to, so everything else keeps its order:

| Badges | Action | Result |
|---|---|---|
| 1 2 3 4 | click the **2**, press **+** | 1 **3** 2 4 |
| 1 3 2 4 | press **+** again | 1 **4** 2 3 |

The other badges stay in the same relative order throughout (here: 1, 2, 3 reading left to
right). **+** is greyed out on the highest badge and **−** on the lowest, so numbering never
develops a gap. Tap them as fast as you like — with this tool active, a double-click is just
two clicks and won't trigger the snip's double-click action (or close a pin).

> 💡 **Typing is different from + / −.** Typed numbers are taken literally with no swap, so
> you *can* end up with two badges showing the same number. Use it when you want an arbitrary
> number; use + / − when you want to reorder.

> 💡 **Undo rewinds the counter too.** Stamp 1, 2, 3 and press `Ctrl + Z` — the 3 goes away
> *and* the next click stamps 3 again, not 4. `Ctrl + Y` puts both back.

> 💡 One `Ctrl + Z` undoes a whole renumber — including the badge it swapped with — not one
> button press at a time. The same goes for a whole drag and a whole scroll-resize: each
> gesture is a single undo step, however many mouse events it took.

**Numbering style.** The dropdown next to the three look buttons counts in numbers
(`1.2.3`), capital or lower-case letters (`A.B.C` / `a.b.c`), or Roman numerals
(`I.II.III` / `i.ii.iii`). Your choice is remembered between screenshots and restarts.

> 💡 Unlike the other controls on the bar, this one is **retroactive**: the badges are one
> sequence, so switching converts the badges you've *already* stamped as well as the next
> one. One `Ctrl + Z` converts them all back.

> 💡 You always **type digits** when renumbering a badge, whatever it's showing — type `4`
> on a Roman badge and it displays `IV`. Letters run A…Z then AA, AB…; Roman numerals go up
> to 3999, above which the badge falls back to plain digits.

> 💡 The badge keeps a **fixed size** no matter how many digits it holds — 2- and 3-digit
> numbers shrink their digits to fit rather than growing the circle, so a row of callouts
> stays visually even. A wide numeral like `VIII` shrinks the same way, so it reads smaller
> than a `8` would.

**Deleting a badge.** Click it, then the red **×** centred above its outline — or press
`Del`. The badges numbered above it shift down, so 1, 2, 3, 4 minus the **2** leaves 1, 2, 3
rather than a hole at 2. One `Ctrl + Z` brings back both the badge and the old numbering.

> ⚠️ Deleting every *digit* and pressing `Enter` **restores the original number** rather than
> removing the badge. Use **×** / `Del` (or the eraser) to remove one.

### 2.6 Styling

- **Color board** with a larger "custom color" button that also sets pen **alpha /
  translucency**.

> 💡 **On mixed-DPI monitors, annotation sizes are pre-scaled** so a stroke keeps its on-screen
> thickness no matter which screen's pixels the region came from. You don't need to do anything
> — just be aware a 4-px pen looks the same size on a 150 % and a 100 % monitor.

---

## 2b. OCR — copying text out of a capture

SnapCap can read the **text** inside a capture and let you select and copy it — no retyping.

**Where to start it**

- **While snipping:** drag out a region, then click the **OCR** button (the viewfinder-"A"
  icon on the toolbar, just after undo/redo).
- **On a pin:** click the pin, press `Space` to show its toolbar, then click the same **OCR**
  button. OCR reads the whole pin.

**Selecting & copying**

1. After a moment, each recognized word gets a light **grey plate** — the capture is now a
   text surface (the cursor becomes an I-beam).
2. **Drag across the words** you want (they highlight in the accent color, in reading order), or
   press `Ctrl + A` / click **Select all** to take everything.
3. **Copy** with `Ctrl + C` or the **Copy** button. `Ctrl + C` also **leaves OCR** and returns
   you to the normal toolbar. The text lands on the clipboard as plain text — paste it anywhere.

**The OCR bar** (second row, while OCR is active)

- **Select all** · **Copy** · **Line breaks** (toggle: keep the original line breaks, or join
  everything into one flowing line) · **Language ▾** (which recognition language to use).

**Leaving OCR:** press `Esc`, click the OCR button again, or pick any drawing tool. Your
capture / pin is untouched.

**Languages**

- SnapCap ships with **English**, and picks a default language from your app's UI language when
  a matching pack is installed.
- To add more languages, open **Preferences → OCR** and download the ones you want (they're
  saved next to the app, in a `tessdata` folder). You can also drop a `*.traineddata` file into
  that folder yourself — SnapCap lists whatever is there.

> 💡 OCR works best on clear, reasonably large text. Tiny or low-contrast text may come back
> with mistakes — zoom a pin up (scroll) *before* running OCR if the text is small.

---

## 2c. Recording — capturing a region to video

Recording starts from the **snip toolbar**, so a recording is just a snip you decided to film.
There is no global hotkey for it.

1. Press `F1` and choose a region exactly as you would for a screenshot.
2. On the toolbar that appears, click the **movie-camera** button. A recording row opens with
   the **record** button (a ring with a red dot), the **pause** button (two bars) and a timer. The
   buttons are icons only; hover one for its name.
3. *(Optional)* Set a countdown: the timer starts at `-0:00` — scroll it, or click and type, to
   make it `-0:05` for a 5-second delay. Typing is right-aligned, so `5` = 5s and `130` = 1:30.
4. Click the **record** button.

The moment you press record, **the frozen screenshot is dropped and the region goes live** — the
dimming disappears, the desktop underneath keeps running, and **clicks pass straight through** to
whatever you are recording, so you can drive the app normally. Only a thin border marks the
recorded area. If you set a countdown, it runs on the live screen so you can get set up.

The timer then counts up as elapsed time, and the record button becomes a **stop** button (a ring
with a red square). **Pause** stops the clock and leaves that time out of the file entirely; while
paused it shows a **resume** (play) icon. **Stop** finalizes the video, and a tray notification
shows where it was saved.

### Annotating while you record

Pick any annotation tool from the toolbar mid-recording and draw on the region — arrows, boxes,
text, highlighter, number labels. What you draw is **burned into the video** from that moment on
and stays for the rest of the clip.

While a tool is armed the region stops passing clicks through (it needs them to draw with);
deselect the tool and click-through returns. The recording is unaffected either way. Only the
**recorded region** stops passing clicks — the rest of the desktop stays clickable even with a
tool armed. Note that drawing gives SnapCap the keyboard focus, so the app you are filming will
look inactive (greyed title bar) while you draw; it goes back to normal as soon as you click it.

Choosing a tool moves the recording row down to a **third row**, directly under the tool's own
options row, so all three stay visible.

| You want to… | Do this |
|---|---|
| Start recording | `F1` → pick a region → movie-camera button → **record** (red dot) |
| Delay the start | Scroll or type in the timer to set e.g. `-0:05`, then **record** |
| Use the app you're recording | Just click it — clicks pass through unless a tool is armed |
| Draw on the video | Pick any annotation tool and draw |
| Pause / resume | **Pause** (two bars) on the recording row, then **resume** (play); paused time is cut from the file |
| Stop and save | **Stop** (red square; a preview window opens for review and trimming) |
| Record microphone too | Tick **Record microphone** in Preferences → Recording |

### Settings — Preferences → Recording

Everything about recording lives in **Preferences → Recording** (the tab directly below OCR).

| Section | What it controls |
|---|---|
| **Video** | Format (MP4 / MKV / WebM / **GIF**), codec, frame rate, quality vs. target bitrate, and whether to prefer a hardware encoder |
| **Sound** | System sound and microphone (each on its own), plus audio quality |
| **Behavior** | Countdown before recording, whether the cursor is drawn in, and an automatic stop after N minutes |
| **GIF** | Frame rate, maximum width and looping — used only when the format is GIF |
| **Output** | Where files go, with a live preview of the resulting name |

**Which encoder will actually run.** Under the codec dropdown SnapCap tells you what your machine
resolves to — e.g. *"Will use h264_nvenc (hardware)"*. All four codecs work everywhere because
each has a software fallback, so the choice is about size and compatibility, not availability:

- **H.264** — plays everywhere. The safe default.
- **H.265 (HEVC)** — noticeably smaller than H.264; less universally supported.
- **AV1** / **VP9** — smallest files, and royalty-free.
- **GIF** — silent, no codec, its own palette. Handy for short clips, but **far** larger than
  video: a few seconds can run to several MB.

WebM accepts only VP9 and AV1; pick it with another codec and SnapCap moves you to one that fits
rather than failing when the file is written.

### Sound

Recordings capture **your PC's audio** by default; the **microphone** is off until you turn it on.
With both on they are **mixed into one track**; with both off the file has no audio stream at all.
If a source will not open (no mic plugged in), recording continues without it and says so in
`splog.txt`.

> 💡 **Your speaker volume does not affect the recording.** Windows hands loopback the mix *after*
> the volume slider, so a quiet system would otherwise produce a quiet file — at a 6% slider that
> is a 42 dB loss. SnapCap reads the slider's actual gain and divides it back out, so the
> recording comes out at source level whether you monitor at 6% or 100%. Changing the volume
> mid-recording is tracked too (re-checked 4×/second).
>
> ⚠️ **A per-app level in the Volume Mixer is NOT recoverable**, because Windows applies it before
> the mix SnapCap sees. If a recording is still quiet, check that app's own slider (and its
> in-page volume, e.g. YouTube's) rather than the speaker icon. Muting the system entirely gives
> digital silence, which nothing can recover.

**Where recordings go.** By default an `.mp4` in your **Videos** folder named
`SnapCap_<date>_<time>.mp4`, using the same `$…$` template syntax as the image save paths (§4).

> **Which encoder is used.** SnapCap prefers your GPU's hardware encoder (NVIDIA/Intel/AMD, or
> Windows Media Foundation) and falls back to a software encoder when none can be opened, so
> recording works on any machine. Whichever it chose is logged to `splog.txt`.

> ⚠️ **Mosaic and blur are unavailable while recording.** They work by processing the picture
> *underneath* them, which here is live video the toolbar cannot see — so they would record as
> garbage. The button is greyed out with that explanation while a recording runs.

> ⚠️ **Recording a video or a game?** SnapCap records via Desktop Duplication when the region
> sits entirely on your **primary** monitor, which captures hardware-accelerated content
> correctly. A region on a secondary monitor (or spanning two) currently falls back to the GDI
> grabber, which can record accelerated video as **black**. Keep the region on the primary
> monitor if you are recording video playback.

### Reviewing and trimming what you recorded

When a recording stops, a **preview window** opens (turn it off with *Show a preview when
recording stops* in Preferences → Recording). It **stays on top of other windows**, so it cannot
open behind the app you were filming. **The file is already saved by then** — this is a review
window, not a save prompt, so closing it can never lose a take.

| You want to… | Do this |
|---|---|
| Play it back | **Play** (sound included) |
| Jump to a moment | Drag anywhere on the bar |
| Choose a range to keep | Drag the two **markers** at the ends of the bar — the kept part stays bright |
| Save just that range | **Save trimmed copy** |
| Find the file | **Show in folder** (the newest trimmed copy once you have saved one, else the original) |

**Trimming never touches the original.** It writes a **new** file next to it, named
`<original>_trimmed_<start>_to_<end>.<ext>` with both times as `HH-MM-SS` — so trimming
`SnapCap_2026-08-04_17-43-49.mp4` from 2:20 to 5:15 gives
`SnapCap_2026-08-04_17-43-49_trimmed_00-02-20_to_00-05-15.mp4`. Several trims of one recording
therefore tell each other apart by what they contain.

**The preview stays open after a save**, so one recording can yield several clips: move the
markers, save again, repeat. Each save writes its own file and the original is never touched.
Saving the same range twice adds a `_2`, `_3` counter. Saving does not open a folder window —
use **Show in folder** when you want one. That button selects the original recording until you
save a trim, and the **newest trimmed copy** after that.

> ⚠️ **Trims cut on keyframes.** The copy is made without re-encoding, which is instant and
> lossless — but a cut can only land on a keyframe, so the clip may start **up to 2 seconds
> earlier** than the marker you set. (Asking for 2.5 s–7.5 s of a test clip produced 5.5 s rather
> than 5.0 s, starting from the keyframe at 2.0 s.) The end is not affected the same way.

> ⚠️ Mosaic and blur remain unavailable while recording (see above).

---

## 2d. Scrolling capture — content bigger than the screen

A long web page, a chat log, a spreadsheet — anything that does not fit on screen. You scroll it
yourself and SnapCap joins the pieces into one tall image, or a wide one if you switch the
direction (below). Like recording, it starts from the **snip toolbar**; there is no global hotkey.

1. Press `F1` and drag a region over the content you want. **Keep it on one monitor.**
2. Click the **scrolling-capture** button on the toolbar — a page with a down arrow, immediately
   right of the OCR button. A second row opens showing the size of the image so far
   (`width × height px`) and seven icon buttons — **Direction** (a double-headed arrow),
   **Move** (an open palm), **Auto** (an **A** inside a circular arrow), then, fenced off between
   two dividers, the two **Trim** buttons (a crossed-out torn-off block each), and finally **Done** (a tick)
   and **Cancel** (a cross). Hover any of them for its name.
3. The frozen screenshot drops, the region goes **live**, and clicks and the wheel pass straight
   through to the window underneath. A narrow **preview strip** appears beside the region (above
   it, for a sideways capture). **The whole top toolbar greys out** for as long as the session
   runs — drawing tools, undo/redo, the mode buttons, and Copy / Save / Pin / ✕ alike. The second
   row is in charge until you press Done or Cancel.
4. **Scroll with the mouse wheel, with the pointer inside the region.** Each time you pause, that
   view is captured and joined on. The strip itself lengthens with the image — it starts as short
   as the first capture and extends as you go, never showing empty space ahead of what you have
   captured — and the height in the size readout climbs with it; the width stays put, since every
   capture is the same region.
5. Click **Done**. The desktop re-freezes and the region shows the whole result shrunk to fit, so
   you can see what you captured.
6. **Copy**, **Save** and **Pin** come back on the top row and act on the full tall image. The
   drawing tools stay off: the result is far taller than the region on screen, so there is nowhere
   to draw on it at the right size.

**Cancel** (or `Esc`, or a right-click outside the region) throws the stitched image away and puts
you back in **normal annotation mode on that same region** — the overlay stays open, every tool
comes back, and the region is captured **fresh** at that moment, so what you get is exactly what
you would have got by dragging it just now. Press `Esc` again to close the capture altogether.

**Scrolling back up works too** — go above where you started and the image grows upward. Scrolling
back through parts you already captured just moves the marker on the strip; nothing is duplicated.
(The marker — a coloured outline showing which part of the image the region is looking at — appears
as soon as you have captured more than the first view; on that first view it would just outline the
whole strip.)

**Once the image is too long for the strip**, the strip shows a slice of it rather than the whole
thing, and a **white arrow** appears at each end that has more image beyond it — pointing the way
the rest of the capture lies. Both ends show one when you are somewhere in the middle; each
disappears as you reach that end. On a sideways capture they point left and right instead.

### Direction — capturing sideways instead of downward

Some content is too **wide** rather than too tall: a Gantt chart, a wide spreadsheet, a timeline.
Press **Direction** — the double-headed arrow — so it points ↔, and everything above works the
same way turned 90°.

- Scroll the content **sideways** and the image grows to the **right**; scroll back past where you
  started and it grows to the **left**.
- Any wheel scrolling counts, whichever wheel you use: a tilt wheel, a two-finger sideways swipe,
  or the usual `Shift` + wheel. (During a *downward* capture only the ordinary vertical wheel
  counts — a sideways scroll there moves the content somewhere the pieces cannot be joined.)
- The **preview strip** lies above the capture area instead of beside it, growing rightward.
- **Move** drags the area **left and right**, and **Auto** scrolls **rightward**.
- The size readout still reads `width × height px` — this time the *width* is what climbs.

Your choice is remembered for next time, unlike Move and Auto: it describes the kind of content
you capture, not one session. The button **locks once a second view has been captured**, because
from then on the image has committed to an axis. Flipping it before that simply starts again from
the current view, and nothing is lost.

> ⚠️ **Some apps ignore sideways wheel scrolling.** If **Auto** stops immediately saying it
> reached the end of the content, that app is one of them — scroll it by hand with `Shift` + wheel
> instead, which is far more widely supported.

### Move — reaching content the region never covered

Suppose you drew the region across the middle of the screen and captured your way down a page.
Now you want the very top of the page as well — but the page is already scrolled to its top, so
scrolling up does nothing, and the header sits *above* your region where it can never be captured.

Press **Move** — the open palm — and drag the capture area upward over the header. Release, and
that strip of the page joins onto the top of your image. The palm closes into a fist while the
mode is on, so you can tell at a glance that the area is grabbable.

- The area moves **along the capture direction only** — up and down for a downward capture, left
  and right for a sideways one — and never changes size.
- You cannot drag it so far that it loses touch with what you have already captured — it stops
  short. A move therefore always joins on; it can't strand the capture.
- The marker on the preview strip **follows the area as you drag**, so you can see where you are
  about to capture before you let go.
- Leave the mode with **Esc** or by pressing **Move** again. (`Esc` peels off one layer at a time:
  the first leaves Move, the second leaves the scrolling capture, the third closes the snip.)

> ⚠️ **The wheel does nothing while Move is on.** Dragging needs the capture area to accept the
> mouse, and an area that accepts a drag also swallows the wheel. Leave Move to scroll again —
> moving and scrolling alternate rather than mix.

### Auto — letting SnapCap do the scrolling

Press **Auto** and the page scrolls itself, capturing as it goes, until it reaches the bottom.

The pointer jumps into the middle of the capture region when you press it. That is deliberate and
necessary: a scroll is delivered to whatever sits under the pointer, and pressing the button put
the pointer on the toolbar. It is yours again the moment auto-scroll stops.

**Three things stop it**, and all of them are ordinary gestures:

- It **reaches the end** of the content — a message says so and the session stays open, so you can
  scroll back, check the strip, and press Done when you are happy.
- You **move the pointer out of the region** — the quickest way to take back control.
- You **scroll manually**. Your wheel wins; auto-scroll steps aside rather than fighting you.

Press **Auto** again to resume from wherever you are.

Step size looks after itself. SnapCap measures how far the page actually moved and aims the next
step at about half the region, so there is always plenty of overlap to join on. That matters
because a single wheel notch is worth very different amounts in different apps and at different
zoom levels — nothing is assumed about it.

> 💡 Auto-scroll only ever walks toward the far end — **downward**, or **rightward** for a
> sideways capture. To extend the image the other way, scroll back by hand.

### Trim — throwing away part of what you captured

The two buttons between **Auto** and **Done** cut the capture down. They work on whatever the
capture area is showing **right now**:

| Button | Vertical capture | Sideways capture |
|---|---|---|
| **Trim start** (left of the pair) | Removes everything **above** the capture area | Removes everything **left** of it |
| **Trim end** (right of the pair) | Removes everything **below** the capture area | Removes everything **right** of it |

Each icon shows the piece it will tear off — a block with a torn edge along the cut and an ✕ over
it — and turns to match the capture direction, so the buttons always look like what they do.

Use them for a page header you started on, or for scrolling further than you meant to at the end.
**Scroll to where you want the image to begin or end, then press the button:** what the capture
area is showing is always kept, so the two mean "keep from here down" and "keep up to here".

A button is greyed out when its side is empty — with nothing captured beyond the area yet, there is
nothing there to remove.

> ⚠️ **There is no undo for a trim**, and no confirmation. What there is instead: scrolling back
> over ground you cut away simply captures it again, the same as any other scroll. Pressing a trim
> button also stops **Auto**, since auto-scrolling would put the far end straight back.

### If it says "scroll back"

Scroll too far in one jump and the new view shares nothing with what came before, so there is no
way to know how the two join. SnapCap refuses to guess: the frame is dropped, a red warning
appears and the strip's border turns red. **Scroll back up until content you already captured is
on screen** and it picks up again by itself. Nothing is lost, and a wrong image is never produced.

Smaller scroll steps — roughly half a screen at a time — are the way to avoid it entirely.

Scrolling **back** over ground you have already captured is different, and always safe: however
far you flick, SnapCap finds where you are in the image it has built and the marker on the strip
follows you there.

> 💡 **Drawing tools are switched off** for a scrolling capture, and stay off afterwards: the
> result is many times taller than the region, so there is nowhere on screen to draw on it at full
> size. Anything you drew before pressing the button is discarded. Save the image and annotate it
> in a pin (`Ctrl + T`) if you need arrows on it.

> ⚠️ **Only the mouse wheel triggers a capture.** Dragging the scrollbar, pressing `PageDown`, or
> a touchpad flick will scroll the window but will not add anything to the image.

> ⚠️ **Choose the region around the part that actually scrolls.** If you capture a whole browser
> window, the toolbar and any pinned sidebars are inside the region but do not move with the
> content — the article between them stitches correctly, while the edges end up repeated or
> broken. A region drawn around the content column gives a clean result.

> ⚠️ **Pages that fake scrolling** — infinite feeds that reload content, or anything animating
> while you scroll — can stop matching. The symptom is the strip refusing to grow.

> The capture stops growing at 30000 px tall, and is **vertical only** for now.

---

## 3. Paste / Pin — floating image windows

`F3` (or middle-click the tray icon) turns the **current clipboard content** into a top-most
floating window. From a snip, `Ctrl + T` / middle-click pins the capture directly.

### 3.1 What can be pasted

- **Images** — pasted as the image.
- **Colors** — RGB (three 0–255 ints or three 0–1 floats) or HEX (`#…`) → a color card.
- **Text** — plain or HTML, rendered as an image.
- **File path(s)** — image files paste as the image; non-image paths render the path text as
  an image (can be disabled in options). An **animated** image file plays — see §3.8.

### 3.2 Transforming a pin

| You want to… | Do this | Result |
|---|---|---|
| Rotate 90° | `1` (left / CCW) / `2` (right / CW) | Rotates the image |
| Flip | `3` (horizontal) / `4` (vertical) | Mirrors the image |
| Grayscale / invert | `5` / `6` | Toggles grayscale / color inversion |
| Scale | **Mouse scroll**, `+` / `-`, or drag the window **border** | Zooms the pin |
| Change opacity | `Ctrl + scroll`, or `Ctrl + +` / `Ctrl + -` | Makes the window more/less transparent |
| Reset size & opacity | **Middle-click** the pin | Returns to 100 % scale and full opacity |
| Toggle thumbnail mode | `Shift + double-click` | Shrinks to a fixed-size fast thumbnail (rainbow border); repeat to restore |

> ⚠️ The number keys `1`–`6` **always** perform image transforms — even with an annotation
> tool active. The pin toolbar only intercepts `Esc`, `Ctrl+Z`, and `Ctrl+Y`; everything else
> falls through to the pin. What *does* change with a tool active is the **scroll wheel** — it
> sizes the pen instead of zooming (§3.6). So while annotating, avoid tapping `1`/`2` unless you
> mean to rotate the whole pin.

> 💡 **Thumbnail behavior is configurable** (Paste → Behavior → "Reset on thumbnail"). When on,
> entering thumbnail mode drops rotation/flips/opacity and restores them on exit; when off,
> only the scale changes.

### 3.3 Window management

| You want to… | Do this | Result |
|---|---|---|
| Close the paint bar, or close the window | `Esc` | Closes the paint bar if open; otherwise closes the window (**recoverable**) |
| Close the window | `Ctrl + W`, or **double-click** it | Closes it (recoverable) |
| **Destroy** the window | `Shift + Esc` | Closes it **permanently — not recoverable** |
| Recover a closed window | Press the paste hotkey (`F3`) again | Recovered **oldest-first**, up to the recoverable count (default 10) |
| Hide / show **all** windows | `Shift + F3` | Toggles every pin at once |
| Replace the image | `Ctrl + V` | Swaps in the current clipboard content |
| Copy the source text | `Ctrl + Shift + C` | Copies the plain text (only if this pin was made from text) |
| Copy the pin as a file | `Ctrl + Alt + C` | Copies the image as a file object |
| Move the window 1 px | `↑ ← ↓ →` | Nudges the pin |
| Magnetic-snap while dragging | Hold `Shift` and drag | Aligns edges to other windows |
| Open Preferences | `Ctrl + Shift + P` | Opens Preferences from any pin |
| **Multi-select** a pin | `Ctrl` + **click** it | Toggles that pin in/out of the selection — selected pins get an amber outline |
| Select **all** visible pins | `Ctrl + A` (on any pin) | Adds every visible pin to the selection |
| Clear the selection | **Click** any pin normally (no `Ctrl`, no drag) | Deselects every pin, leaving just the clicked one focused |
| Move the whole selection | **Drag** any selected pin | All selected pins move together (magnetic snap follows the dragged pin) |

> 💡 **Multi-select fans out operations.** Once several pins are selected, an action on **any**
> one of them applies to **all** of them: rotate `1`/`2`, flip `3`/`4`, grayscale/invert `5`/`6`,
> scale (`+`/`-`/scroll), opacity (`Ctrl` + `±`/scroll), reset (middle-click), thumbnail
> (`Shift` + double-click), 1-px nudges, close (`Esc` / `Ctrl + W` / double-click) and destroy
> (`Shift + Esc`). Clipboard actions (`Ctrl + V` replace, `Ctrl + Shift + C` copy text,
> `Ctrl + Alt + C` copy-as-file) stay on the single pin you invoke them on.

> ⚠️ **`Shift + Esc` destroys permanently.** A normal close (`Esc` / `Ctrl + W` / double-click)
> goes into the recover ring and can be brought back with `F3`; a destroyed window cannot.

> 💡 **Recover-ring vs. fresh clipboard:** a freshly-copied clipboard item pastes ahead of
> recovery once; otherwise `F3` drains the recover ring (oldest first) before re-pasting the
> clipboard.

> ⚠️ **`Ctrl + V` (replace image) clears any annotations** you've drawn on that pin.

### 3.4 Click-through (mouse-through)

1. Assign a **"Toggle mouse click-through"** global hotkey in Preferences (unbound by default).
2. Trigger it while the mouse is over a pin → that pin becomes click-through (mouse events pass
   to whatever is behind it).
3. Trigger it with the mouse **not** over any pin → all click-through pins become clickable
   again.

### 3.5 The pin right-click menu

**Right-click** a pin for the full menu. Most items show a single-key accelerator:
copy image (`C`), save as (`S`), show toolbar (`Space`), drop shadow (`W`), paste/replace
(`P` / `R`), **Move to group** (`M`), view in folder (`V`), close (`X`), destroy (`D`). The
**Zoom** and **Image processing** submenus (and their items) have **no** accelerators — pick
them by mouse, or use the pin-level transform keys in §3.2. In **Zoom**, the pin's current
zoom level is **ticked** (no tick means the current scale isn't one of the presets — e.g. after
a scroll-wheel zoom).

> 💡 Rotation keys match the menu: `1` = **rotate left** (counter-clockwise), `2` = **rotate
> right** (clockwise) — consistent whether you press the key or click the menu item.

> 💡 **Save image as…** (`S`) offers the same formats, and the same ICO and GIF caveats, as a
> snip save — see §1.6. It saves the pin **as displayed**, including any zoom, rotation, flip,
> grayscale/invert and annotations.

### 3.6 Annotating a pin

Press `Space` (or menu → **Show toolbar**) to draw on a pin. The paint bar reuses the snip
tools, but with pin-specific rules:

- Annotations are **baked into the image** — every export (save, copy, thumbnail) includes
  them automatically.
- **Pin text is uneditable once committed** (unlike snip text, which you can re-edit). This is
  intentional.
- With a tool active, the **scroll wheel sizes the pen**; zoom only works when no tool is
  selected.
- The pin toolbar keeps **cancel + undo/redo** but hides the save/copy/pin buttons.

> ⚠️ Replacing a pin's image (`Ctrl + V`, or menu → Replace by file) **clears its
> annotations** — see §3.3.

### 3.7 Pin groups

Pins can be organized into **named groups**. A new snip/paste starts **ungrouped** (group 0),
which is always visible.

| You want to… | Do this | Result |
|---|---|---|
| Show / hide a group | Tray menu → **Group** submenu → tick/untick a group | Loads that group's pins from disk (show) or serializes them away (hide) |
| Assign a pin to a group | **Right-click** pin → **Move to group** | Moves the pin; if the target group is hidden, the pin is put away to disk |
| Manage groups | Tray → **Group → Manage…** | Rename / create / delete groups |

- Group checkboxes are **independent** — you can show **any number of groups at once**.
  Live windows = ungrouped pins + every shown group's pins.
- **There are no group hotkeys** by design — grouping is menu-driven only.

> ⚠️ **Deleting a group discards its pins** (after a confirmation dialog). Moving a pin into a
> hidden group makes its window disappear immediately (it's saved to disk, not lost).

> ⚠️ **Session restore interacts with groups:** group *definitions* are always durable, but a
> shown group's pins are only re-saved when **Preferences → General → Restore session** is on.
> With restore off, hidden groups keep their last on-disk contents.

### 3.8 Animated images (GIF)

A pin **plays and loops** an animated image when it was pinned **from a file**:

| You want to… | Do this |
|---|---|
| Pin an animated GIF | Copy the **file** in Explorer → `F3` |
| Turn an existing pin into one | Right-click → **Replace by file…** → pick the GIF |
| Pause / resume | Right-click → **Pause** (only shown on an animated pin) |

- Everything else about the pin still applies **per frame**: zoom, rotate, flip,
  grayscale/invert, opacity and annotations all ride along with the animation.
- Opening the annotation toolbar (`Space`) **pauses playback** so you aren't drawing on a
  moving image; closing it resumes. Hidden pins (`Shift + F3`) stop playing too.
- **Save image as…** and **Copy image** produce the **frame showing at that moment**, not the
  animation — SnapCap writes single-frame files only.
- An animated pin **still animates after a restart**, as long as the original file is still
  where it was. If it moved, the pin comes back as a still.

> ⚠️ Copying an animated GIF **out of a browser or image viewer** (rather than copying the
> file itself) pins a **still**. Windows puts a flat bitmap on the clipboard in that case —
> the frames are gone before SnapCap sees it. Copy the *file* to get the animation.

> ⚠️ A very large animation is pinned as a **single frame** with a warning, because every
> frame has to be held in memory to play.

---

## 4. Preferences & portability

- Open Preferences from the **tray menu**, or `Ctrl + Shift + P` from any pin.
- Tabs, in order: **General, Interface, Snip, Paste, Output, Control (key bindings), OCR,
  Recording, Update, About.** (Annotation, cursor, history, and log-level settings live inside these tabs
  — there is no separate Annotate/Misc/Log tab.)
- **Number boxes and dropdowns ignore the scroll wheel until you click them.** Scrolling with the
  cursor over one just scrolls the page, so passing over a setting can never change it by
  accident. Left-click the field and the wheel adjusts it from then on; click anywhere else and
  it goes back to ignoring the wheel. Reaching it with `Tab` doesn't count — only a click does.
  The colour picker's H/S/V and R/G/B boxes and the font picker's size box work the same way.
- **Theme** (Interface) switches SnapCap between **Light** and **Dark**. **Auto** — the default —
  follows your Windows setting and changes with it straight away, without restarting SnapCap or
  reopening the window. The choice covers SnapCap's windows and dialogs, including their title
  bars. The **Accent color** setting just below is a different thing: it sets the colour used for
  highlights and selected rows, and applies in both light and dark.
- **GPU acceleration** (General) controls how the **Blur** annotation tool is rendered.
  **Auto** (the default) uses your graphics card when a suitable one is found and silently falls
  back to the CPU otherwise; **CPU only** never uses the GPU. Blur looks the same either way —
  the difference is speed, and it only shows on large regions (a full 4K blur is roughly 235 ms
  on the CPU versus 10 ms on a GPU). The **Mosaic** tool is unaffected: it is already fast
  enough on the CPU, and measured *slower* on a GPU.
- **About** shows SnapCap's version, the Qt version it is running against, and its licensing.
  **Open download page** goes to the Qt source code SnapCap was built from, **Get FFmpeg source**
  to the source (and build script) of the FFmpeg that screen recording uses, and the
  **LGPLv3**, **GPLv3** and **Third-party notices** buttons each open that document in your
  text viewer. All three also ship as `.txt` files in SnapCap's own folder.

> 📄 SnapCap uses Qt under the **LGPLv3**. Qt is dynamically linked and unmodified, so every
> `Qt6*.dll` beside `SnapCap.exe` can be swapped for your own build — that freedom is exactly
> why SnapCap ships as a portable folder instead of an installer.

- **Installing SnapCap:** there is no installer. Unzip `SnapCap-<version>-win64.zip` anywhere you
  can write — a normal folder, a USB stick — and run `SnapCap.exe`. Everything it needs sits in
  that folder, including Qt.
- **Uninstalling SnapCap:** delete the folder. Two optional settings write outside it, both **off
  by default**: **Run on startup** (General) adds a value under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, or — in its *elevated* form — a `SnapCap`
  scheduled task; and **Copy image as file** (Output → Clipboard) leaves the copied images in
  `%TEMP%\SnapCap\` without cleaning them up. Switch **Run on startup** off *before* deleting the
  folder — SnapCap will offer to restart as administrator if removing the scheduled task needs it.
- **Portable by default:** `config.ini` and `splog.txt` live **next to `SnapCap.exe`**. A
  "Config path" field (General) can relocate the config; a `config.path` pointer file next to
  the exe records where it went. If the exe folder isn't writable, settings fall back to
  `%LOCALAPPDATA%\SnapCap\SnapCap\`.

> 💡 Because it's portable, you can drop the SnapCap folder on a USB stick or sync it — your
> settings travel with the exe.

---

## 4a. Advanced configuration

A few settings deliberately have **no Preferences control**. They're rarely needed, so they live
only in `config.ini` and are edited by hand.

### Editing `config.ini` by hand

1. **Quit SnapCap first** (tray menu → **Quit**). Settings are held in memory and the whole file
   is written back whenever anything changes, so edits made while SnapCap is running can be
   silently overwritten.
2. Open `config.ini` in any text editor. It sits next to `SnapCap.exe` unless you moved it — see
   [§4](#4-preferences--portability) for where it lives and how to relocate it.
3. Add the key under its `[group]` heading, creating that heading if it isn't there already.
4. Save, then start SnapCap again.

Deleting a key restores its default. Keys SnapCap doesn't recognise are simply ignored — so a
typo does no harm, but it also produces **no warning**, and your setting just won't apply. Check
the spelling if something seems to have no effect.

> 💡 Everything *else* in `config.ini` is managed by the Preferences window. Prefer the GUI for
> those — hand-editing them works, but Preferences validates what you type.

### Available keys

| Key | Group | Default | Purpose |
|---|---|---|---|
| `behavior\clickSlopPx` | `[snip]` | `30` | How far, in pixels, a snip press may drift in the first `clickWindowMs` and still count as a **click** on the highlighted window rather than a tiny drag (see [§1.1](#11-choosing-the-region)). Measured per axis. Raise it if slips still get through; lower it if fast small drags are being swallowed. **`0` turns the protection off** — every drag is then taken literally, as in older builds. |
| `behavior\clickWindowMs` | `[snip]` | `300` | How long, in milliseconds, that generous slop lasts. After it the required distance drops to 10 px, and after a full second of holding to 2 px — so a deliberate small region is always reachable by pausing a moment before you drag. |

Both keys, written out in full:

```ini
[snip]
behavior\clickSlopPx=30
behavior\clickWindowMs=300
```

> ⚠️ These are **snip-only**, and only apply while a window/element suggestion is highlighted.
> With detection off (`Tab`) there's nothing for a click to fall back to, so every drag is taken
> at face value regardless of what you set here.

---

## 5. Tips & warnings at a glance

**Warnings**

- `Ctrl + Shift + Z` (clear all annotations) — **not undoable**.
- `Shift + Esc` on a pin — **destroys permanently**, no recovery.
- `Ctrl + V` on a pin — **replaces the image and clears annotations**.
- **Area reuse** (`R` / `Shift + R`) is **session-only** and resets on restart; the history
  strip (`,` / `.`) persists.
- Right-click **finishes** an open text box or line instead of aborting — use `Esc` to cancel.
- Deleting a pin group **discards its pins**.
- The `` ` ``/`!` cursor toggle is a **no-op** if "Disable cursor capture" is set.
- `G` during a snip toggles **crosshair guide lines**; nothing toggles the magnifier.
  `Alt` is the quick-mark modifier (§2.2), not a magnifier key.
- In-overlay and in-window keys are **hardcoded** (not remappable); only **global** hotkeys are.
- A **scrolling capture** turns the drawing tools off for good, and only the **mouse wheel** adds
  to it — a scrollbar drag or `PageDown` scrolls but captures nothing.

**Tips**

- After a rough drag, use the **arrow keys** (move) with `Ctrl`/`Shift` (grow/shrink) to land
  snip edges on exact pixels.
- Copy a color with `C` during a snip, then `F3` to drop it as a color card.
- While drawing: `Ctrl` gives perfect squares/circles, `Shift` gives 45°-snapped lines and
  arrows. `Shift` while dragging a pin is magnetic edge-snapping.
- Don't hand-drag a box around a button or input field — hold `Alt` to preview it, then
  `Alt` + right-click to mark it exactly (§2.2).
- Resizing a snip region by a **corner** handle: `Shift` keeps its ratio, `Ctrl` makes it
  square — the same `Ctrl` = "equal sides" rule as the shape tools.
- `Ctrl + F1` snips **and** copies in one shot; `Ctrl + Shift + S` quick-saves with no dialog.
- Middle-click a pin to reset it; `Shift + double-click` for a quick thumbnail.
- Show **multiple pin groups at once** — the group checkboxes are independent, not radio
  buttons.
