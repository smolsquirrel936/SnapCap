# SnapCap — Feature Specification

A multifunctional screen-capture app for Windows (portable design for future
cross-platform support).

This is the reference catalog. Struck-out items and inline notes record what SnapCap has shipped or
dropped; `Unimplemented-Features.md` is the ledger of what is still missing.

> Legend: ⭐ = signature/core feature. SnapCap has no paid tier: every feature here is free and in
> scope.
> Key bindings below are for Windows/Linux. On macOS, `Ctrl` → `Cmd`.

SnapCap has **three major functionalities**: **Snip**, **Image Annotation**, and **Paste**.

---

## 1. Snip (Screen Capture) ⭐

**Start snipping**
- Global hotkey (default `F1`).
- Left-click the tray icon.

**Detection / region selection**
- Smart **window detection** and **interface-element detection** under the cursor.
- `Tab` — cycle the detection mode: off → window → element → off.
- `Ctrl + A` — set snipping area to fullscreen.
- `R` — reuse the area of your last successful snip.
- Custom-size / position / delayed capture via a dialog.
- **Fixed-ratio capture** — hold `Shift` while dragging a **corner** grip to keep the region's
  current width:height ratio, or `Ctrl` to force a square. `Ctrl` also squares the initial
  drag-out of a region. (Edge grips are unconstrained.)
- **Region-size input** — click any number in the `( x , y )` / `w x h` readout above the
  region to type it directly. `Tab` / `Shift + Tab` steps the four fields, `Enter` commits,
  `Esc` closes just the editor (the snip stays). `x`/`y` move the region keeping its size;
  `w`/`h` resize it from the top-left corner.
- Hierarchy element detection (scroll to pick parent/child), active-window capture,
  rounded corners, refresh screenshot (`F5`).
- **Quick snip** (hold a modifier combo — Ctrl/Alt/Win — and drag, no hotkey needed) captures
  and immediately Copies and/or Pins, no toolbar. Set the combo under Preferences → Control.

**Pixel-precision adjustment**
- Move cursor by 1px — `W A S D`.
- Move the area by 1px — `↑ ← ↓ →`.
- Enlarge the area by 1px — `Ctrl + ↑ ← ↓ →`.
- Shrink the area by 1px — `Shift + ↑ ← ↓ →`.
- **Recommended:** hold `Mouse-left` + `W A S D` to move/enlarge/shrink in one gesture.
- While dragging to size the area, hold `Space` to move the whole area.
- `` ` `` or `!` — show/hide the captured mouse cursor in the screenshot.

**Magnifier (loupe)**
- Always shown while choosing a region — there is no key to force or hide it.
- Shows zoomed pixels, live selection size, and the color under the cursor.
- `G` toggles the full-screen crosshair **guide lines**; nothing toggles the magnifier
  (`Alt` is the quick-mark modifier, §2).

**Barcode detection (during snip)**
- Every committed region is scanned for a barcode — every symbology ZXing reads
  (`BarcodeFormat::Any`, 18 formats). 2D: QR, Micro QR, rMQR, Aztec, DataMatrix, PDF417,
  MaxiCode. 1D: EAN-8/13, UPC-A/E, Code 128/39/93, ITF, Codabar, DataBar (+ Expanded).
  A hit adds a code icon + the decoded payload as a third chip on the `( x , y )` /
  `w x h` readout row above the region.
- The link is elided to the region's width (domain + ending, e.g. `github.com/…issues/12345`);
  hovering the chip shows the full payload.
- Left-click copies the full payload; `Ctrl + left-click` opens http/https links in the
  browser and closes the snip. Non-web payloads (WiFi, vCard, plain text) are copy-only.
- First code only when several are present; re-scanned whenever the region changes.
- Numeric payloads (EAN/UPC and friends) are shown and copied like any other — they are
  never openable, since only `http`/`https` qualifies.
- **Scan and copy** (standalone) — a global hotkey (unbound by default, under Control → Tools)
  opens a normal dimmed snip; the first dragged region is decoded and its payload copied and
  the snip closes, or a "No barcode found" toast appears when the region holds no code.

**Color picker (during snip)**
- `C` — copy the pixel color (RGB / HEX). Then `F3` pastes it as a color card, or
  `Ctrl + V` pastes it into another program.
- `Shift` — cycle the highlighted color format in the readout.
- **Pick color** (standalone) — a global hotkey (unbound by default, under Control → Tools)
  opens the loupe over the undimmed frozen screen with no region drag; a left-click or `C`
  copies the pixel color and closes, `Shift` cycles the format, `Esc`/right-click cancels.

**Snipping history**
- `,` / `.` — previous / next snip record (only *successful* snips are recorded).
- Max history count is configurable.

**Finish a snip ("successful snipping")**
- Copy to clipboard — toolbar copy / `Ctrl + C` / `Enter` / double-click the area.
- Save to file — toolbar save / `Ctrl + S`.
- Quick-save to preset folder — `Shift +` save button / `Ctrl + Shift + S`.
- Paste/pin to screen — toolbar pin / `Ctrl + T` / middle-click the area.

**Abort a snip**
- `Esc` at any time, the `X` button, or right-click when not in edit mode.
- Another program's window becoming active (this auto-abort is toggleable in Preferences).

---

## 2. Image Annotation ⭐

Tools (free): rectangle, ellipse, line, line-strip, **arrow**, pencil, marker pen,
**mosaic**, **Gaussian blur**, **text**, **number label**, eraser.

**Drawing & editing**
- `Space` — show/hide the paint (annotation) bar.
- Right-click — finish the current shape.
- **Line strip:** click successive points; **single line:** press-drag start→end.
  As shipped, line, line-strip and arrow are **one** lines tool: the end dropdowns cover
  plain line vs. arrowheads, and click-placing vs. dragging covers strip vs. single segment.
- **Independent line ends**: the lines sub-bar carries
  `[start] [line style] [end]`. Both end dropdowns offer the same ten decorations — plain,
  four arrows (sharp / round / chevron / hollow), filled + hollow dot, filled +
  hollow diamond, bar — so the two ends are set separately. The line style adds the pencil's
  five dash patterns plus two line-only entries, **tapered** and **tapered hollow** (a shaft
  that widens from start to end; being a filled silhouette it cannot also be dashed, which is
  why the two live in the same list). Heads stay solid on a dashed shaft. All three persist
  (`edit/line_start_cap`, `edit/line_end_cap`, `edit/line_style_line`) and restyle a selected
  line.
- **Curved line**: a straight/curve **mode button** at the left end of the
  lines sub-bar turns the same click-placed vertices into a smooth curve **through** every
  one of them. Its icon shows the active mode (zigzag = straight, wave = curve); the gesture
  is unchanged (click points, right-click to end placement, right-click to commit) and it
  composes with all four arrow styles. Remembered across snips and restarts
  (`edit/line_curved`).
- Pen width — `Mouse scroll`.
- `Ctrl + Z` undo · `Ctrl + Y` redo · `Ctrl + Shift + Z` remove all edits (not undoable).
- Re-edit a finished shape (free): Undo until it disappears, then Redo.
  direct re-edit, plus `Ctrl + Shift + C` / `Ctrl + Shift + V` copy/clone a shape,
  `Ctrl`-drag to duplicate, `Del` to delete.

**Text**
- Scale — drag the text-box corners. Rotate — drag the dot above the box.
- **Font hover preview** — hovering a name in the family drop-down pops a specimen panel
  beside the list, drawn in that family: a Latin pangram + `0 1 2 3 4 5 6 7 8 9`, plus a
  Han line (`天地玄黃，宇宙洪荒。`) for families that report Chinese coverage
  (`QFontDatabase::writingSystems`, cached — note most Japanese families report Chinese
  too, so they get the line as well). Shown per row and dismissed when the list closes or
  the cursor leaves it. The specimen ignores the bold/italic toggles and the size box — it
  is a *typeface* comparison at a fixed size, not a preview of the shape you'd draw. Lives in
  `ui/PaintBar`, so it serves the pin editor's toolbar too. Built on
  **`QToolTip` with rich text**, not a custom window — a hand-rolled panel closed the
  drop-down out from under itself (its window shadow stole the mouse); see the `ui/PaintBar`
  row of `Architecture-FileMap.md` for the three traps in this area.
- Hold `Shift` before dragging corners to make rotated text horizontal again — **won't do**
  (2026-07-23): the rotate handle already gets a box back to 0° easily enough.

**Number label** (implemented, see the note below)
- Click stamps an auto-numbering badge centred on the click; the counter then advances.
  Re-selecting the tool restarts it at 1.
- Three looks (2nd-level bar, in order): solid · solid + white outer ring · outline-only
  (transparent interior). One `ToolId::NumberLabel`; the look is `Style::label`.
- **Numbering style** — a dropdown right of the three looks (separated by a rule), with five
  entries: `1.2.3` · `A.B.C` · `a.b.c` · `I.II.III` · `i.ii.iii` (`Style::numberStyle`,
  persisted as `edit/label_numbering`). It is **retroactive**, unlike every other control on
  the bar: badges are one sequence, so switching converts the badges already stamped as well
  as the next one, in a single undo step. A badge always *stores* a decimal ordinal — the
  style only picks the glyphs `annotation::formatNumber` draws it in, so the counter, the
  typed-digit renumber and the +/− swap are all style-independent. Letters run A…Z, AA…;
  roman covers 1–3999; anything outside that (including 0) falls back to decimal digits.
- Size — `Mouse scroll` (or the size box). Scrolling **over** a committed badge resizes
  just that badge. The badge size is `Style::fontPx` (the digit's size); the disc is a
  fixed multiple of it, and multi-digit numbers shrink their digits rather than growing
  the disc.
- Committed badges are re-editable: click to renumber in place (typed digits, `Enter`
  commits, `Esc` reverts), drag to move.
- Clicking a badge also shows **+ / −** buttons on its outline (`↑`/`↓` do the same). A step
  **swaps** numbers with whichever badge holds the target, so a ±1 step moves the badge one
  place in the order and every other badge keeps its relative position (1,2,3,4 → step the
  2nd up → 1,3,2,4 → again → 1,4,2,3). Clamped to the badges' existing range, so numbering
  never gains a gap. **Typed** numbers are raw (no swap, duplicates allowed) — the escape
  hatch for arbitrary numbering.
- A red **×** above the outline (or `Del`) deletes the badge and closes the gap: badges
  numbered above it drop by one, so 1,2,3,4 minus the 2 → 1,2,3.
- **Undo/redo carries the counter with it**: stamping 1,2,3 then `Ctrl+Z` stamps 3 next, not
  4. The undo stack is opaque, so the hosts measure the badge count either side of the
  undo/redo and step `m_nextLabel` by that delta (`syncNextLabel`) — a renumber/move/restyle
  moves no count and so leaves the counter alone.
- Every badge gesture is ONE undo step, however many input events it took: a renumber
  (`AnnotationDocument::setTexts`, even though it changes two badges), a drag
  (`moveApplied`), a scroll-resize (`resizeApplied`, flushed on a 500 ms wheel idle), and a
  delete (a `beginMacro`/`endMacro` group around the remove + the gap-closing renumber).

**Styling**
- Color board + a bigger "custom color" button (also sets pen **alpha/translucency**).
- **Line style:** rectangle, ellipse and pencil carry a dash-pattern dropdown on their
  2nd-level toolbar (left of the color palette) — solid, dashed, dotted, dash-dot,
  dash-dot-dot. Remembered **per tool** (`edit/line_style_<tool>`). Hidden while a
  rectangle/ellipse is in **solid-fill** mode, which has no outline to dash. Dash lengths
  are in units of pen width, so they scale with the stroke (and with mixed DPI).
- **Constrained shapes:** hold `Ctrl` for rectangle→square / ellipse→circle (and the other
  rectangle-drag tools); hold `Shift` for line/arrow→horizontal/orthogonal/45° and
  pencil→straight segments. *(`Ctrl` for equal sides matches SnapCap's own
  `Ctrl`-squares-the-region snip resize — user's call, 2026-07-21.)*
- **Quick-mark UI elements:** with rect, ellipse, marker, mosaic, blur or eraser selected,
  hold `Alt` to preview the element under the cursor as a dashed box, then `Alt`+right-click
  to mark it. A quick-mark is always a rectangle, so the freehand modes of the redaction
  tools mark as their rect twin (`Marker`→`MarkerRect`, `Eraser`→`EraserRect`,
  `MosaicBrush`→`Mosaic`, `BlurBrush`→`Blur`) without changing the toolbar mode; pencil,
  line, text and number label are excluded. *(`Alt` is required
  because a bare right-click already aborts the snip, and
  the preview makes the target region visible before committing — user's call, 2026-07-24.
  `Alt` rather than `Ctrl` because `Ctrl` already squares the rect tools mid-drag; the
  aux-line toggle moved off `Alt` to `G` so the modifier carries one meaning only.)*
  Detection runs on demand and ignores the snip-phase `Tab` detect mode; the mark keeps the
  element's full bounds even where they extend past the snip region.

**Annotation extras:** ~~counting numbers~~ (done — see **Number label** above),
magnify, rounded-corner rectangle,
~~dashed patterns~~ (done — see **Line style** under *Styling* above), ellipse/circle brush for
marker/mosaic/blur/eraser, ~~bidirectional &
more arrow styles~~ (done 2026-08-14 — the line's two ends take independent decorations),
filled/outlined text, all-shape rotation, free select, 1–5 pen sizes,
reorderable color buttons, scroll-on-color-button opacity, per-tool colors,
sharable palette.

---

## 2b. OCR — Text Recognition ⭐

Turn a capture into **selectable text**, in place. As built: the OCR rows of
`Architecture-FileMap.md` (`core/ocr`, `ui/OcrTextLayer`, `ui/OcrBar`, `platform/ocr`).

- **Where:** the **OCR** button (viewfinder-"A" icon) on the toolbar — during a **snip** (acts
  on the dragged region) and on a **pin** (`Space` toolbar, acts on the whole pin).
- **How it looks:** recognized words get a subtle grey plate; the region becomes a text
  surface (I-beam cursor). Drag to select (theme-blue highlight, reading order); a 2nd-level
  bar offers **Select all · Copy · Line breaks · Language ▾**.
- **Keys (in OCR mode):** `Ctrl + A` select all · `Ctrl + C` copy the selection **and leave
  OCR** (back to the annotate toolbar) · `Esc` leave OCR (keeps the capture/pin).
- **Languages:** ships **English**; the default language follows the UI locale. More languages
  install in **Preferences → OCR** (per-language download into the portable `tessdata/` folder)
  or by dropping a `*.traineddata` file there. Backend: Tesseract (LGPLv3-compatible).

---

## 3. Paste — Floating Image Windows ⭐

"Paste" converts the **current clipboard content** into a top-most floating window.

**Start pasting**
- Global hotkey (default `F3`); middle-click the tray icon; or "Paste to screen" while snipping.

**Supported clipboard content**
- Image.
- Color info — RGB (three 0–255 ints or three 0–1 floats) or HEX (`#…`).
- Text — plain or HTML.
- File path(s) — image files paste as the image; non-images render the path as an image
  (paste-file-paths can be disabled in options).

**Transform the image window**
- Rotate — `1` (left / CCW 90°) / `2` (right / CW 90°).
- Flip — `3` (horizontal) / `4` (vertical).
- Scale — `Mouse scroll`, `+` / `-`, or drag the window border.
- Transparency — `Ctrl + Mouse scroll`, or `Ctrl + +` / `Ctrl + -`.
- Reset to 100% size & opacity — middle-click.
- Thumbnail mode — `Shift + double-click`.
- Grayscale `5`, color-inversion `6`, crop, free-size thumbnail, background modes
  (transparent / pseudo-transparent / dark or light chessboard), topmost toggle, solo mode.

**Window management**
- `Esc` (or double-click) — hide paint bar, or close the window. `Ctrl + W` — close.
- Closed windows are **recoverable** by pressing the paste hotkey again — recovered
  **oldest-first**, up to the recoverable count (configurable, default 10). A freshly-copied
  clipboard item pastes ahead of recovery once; otherwise Paste drains the recover ring first,
  then re-pastes the clipboard.
- `Shift + Esc` — **destroy** the window (not recoverable).
- Hide/show **all** windows — `Shift + F3`.
- `Ctrl + A` — select all visible windows. `Ctrl`-click multi-select.
- `Ctrl + V` — replace the image with current clipboard content.
- `Ctrl + Shift + C` — copy the plain text (if this image was converted from text).
- `Ctrl + Shift + P` — open Preferences (works from any image window).
- **Magnetic snapping:** hold `Shift` while dragging to align to other windows/images.
- **Click-through (mouse-through):** assign a "Toggle mouse click-through" global hotkey
  in Preferences; triggering it toggles click-through for the image under the cursor
  (if no image window is under the cursor, all unclickable images become clickable again).
- Image **grouping**; drag & drop; GIF playback work here too. Paste/drag image from
  a webpage, editable color cards.
- The **magnifier and color-copy are snip-only** — start a snip to pick a color off a pin.

---

## 3b. Screen Recording ⭐

As built: the recording rows of `Architecture-FileMap.md`; the FFmpeg build and its licence
duties: `Licence-Compliance-Plan.md`; known gaps: `Unimplemented-Features.md` §3b.

- **Entry point is the snip toolbar**, not a hotkey: start a snip, pick a region, then press the
  movie-camera button. There is deliberately no global recording hotkey.
- 2nd-level bar: **Record/Stop · Pause/Resume · timer**. The timer doubles as an editable
  countdown (`-00:05`), counting down to `00:00` and then up as elapsed.
- On Record the region goes **live and click-through**, so the app being recorded stays usable.
- **Annotations can be drawn while recording** and are burned into the video from the moment
  drawn. Arming a tool takes input back; disarming returns click-through. Mosaic/blur are the one
  exception — they read the picture underneath, which here is live video, so they are disabled.
- **Audio**: desktop (WASAPI loopback, compensated for the speaker volume) and/or microphone,
  mixed into one track.
- **Formats**: MP4 / MKV / WebM / **GIF**; H.264, H.265, AV1, VP9, each with a software fallback
  so no codec is ever unavailable. Hardware encoders (NVENC/QSV/AMF/MF) are used when they open.
- **After stopping**, a preview window plays the result back with a seek bar and **trim** handles;
  saving a trimmed copy never touches the original.
- Settings live in **Preferences → Recording** (directly below OCR).

## 3c. Scrolling Capture ⭐

Capture content taller than the screen: scroll the window and the frames are stitched into one
tall image. As built: the `ScrollStitcher`, `WinWheelWatch`, `ScrollCaptureBar` and
`ScrollStripWindow` rows of `Architecture-FileMap.md`; known gaps: `Unimplemented-Features.md` §3c.

- **Entry point is the snip toolbar**, not a hotkey: start a snip, pick a region, then press the
  scrolling-capture button (row 1, immediately right of OCR). No global hotkey.
- The region goes **live and click-through**, so you scroll the real window underneath it. Each
  wheel event restarts a 150 ms idle timer; when it fires the region is grabbed and stitched.
- A **preview strip** appears beside the region — left, or right when there is no room, or
  overlapping the left edge for a full-width selection (it is excluded from capture, so it never
  lands in the result); above / below / inside the top band for a horizontal capture. Fixed scale,
  following the live frame, with an indicator showing where you are in the whole image and a
  chevron at each end that still has image beyond it.
- 2nd-level bar: the **size of the stitched result so far** (`width × height` in capture px — the
  measurement across the capture direction is the region's and never changes, the one along it is
  the progress) **· Direction · Move · Auto ‖ Trim-start · Trim-end ‖ Done · Cancel**, plus the
  messages below.
- **Trim** throws away everything before or after the area you are looking at — the top/bottom of a
  downward capture, the left/right of a sideways one — so a header you opened on, or an overshoot
  past the end, can go without restarting. The area itself is always kept and the session carries
  on; scrolling back over the cut ground re-captures it. Each button greys out when its side is
  empty, and its icon shows the piece being torn off, turned to match the capture direction.
- **Direction** flips the capture between **downward** and **rightward**. A horizontal session
  works the same way turned 90°: it takes either wheel axis (`Shift`+wheel is the common way to
  scroll sideways, and arrives as an ordinary vertical wheel), Move drags sideways, and Auto
  injects a horizontal wheel. The choice is **remembered across snips** — it describes the content
  you shoot, not one session — and **locks once a second frame lands**, which is what first commits
  the image to an axis.
- **Move** drags the capture area itself, along the capture direction. It exists for content the region never
  covered: start capturing halfway down a page and the part *above* the region is otherwise
  unreachable, since the page cannot scroll up past its own top. The move is clamped to keep
  overlap with what is already captured, so it can never fail to match, and the strip's marker
  follows the region as you drag. While Move is on the region takes the mouse, so the wheel does
  not reach the page until you leave the mode (`Esc`, or the button again).
- **Auto** scrolls the page for you. It parks the pointer in the region (an injected wheel goes
  wherever the pointer is), then steps → grabs → steps, sizing each step from how far the page
  *actually* moved last time to land near 50% of the region — so it adapts to any app's
  pixels-per-notch instead of assuming one. It stops, with a message, when the page stops
  producing new content, when you move the pointer out of the region, or when you take the wheel
  yourself; and it never fights you.
- **Scrolling up extends the image upward**; revisiting captured content just moves the indicator.
- A frame that cannot be placed is **dropped with a warning** ("scroll back until previously
  captured content is visible") and a red strip; scrolling back clears it. Nothing is ever
  stitched wrongly — the failure is always a stall.
- Matching survives the furniture real windows have: a **non-scrolling header band**, a **moving
  scrollbar thumb**, and **sticky sidebar columns** (a nav rail and a table of contents flanking
  the article that scrolls).
- **The whole first-level toolbar is off for the session** — tools, undo/redo, the mode toggles and
  Copy / Save / Pin / ✕ alike, leaving row 2 in sole charge. The tools stay off after **Done**: the
  result is far taller than the region, so it cannot be annotated in place; anything drawn before
  entering is discarded.
- **Done** re-freezes the desktop, shows the whole result scaled to fit inside the region, and
  hands it to row 1's **Copy / Save / Pin**. **Cancel / `Esc`** throws the stitched image away and
  returns to ordinary annotation on a **freshly grabbed** capture of the same region — the overlay
  stays up and every tool comes back, exactly as if you had just dragged that region.
- Limits: **one monitor**, result capped at **30000 px** along the capture direction, and the wheel
  is the only trigger (a scrollbar drag or `PageDown` does not grab). Apps that ignore
  `WM_MOUSEHWHEEL` cannot be driven by Auto sideways — scroll them by hand.

## 4. Key Bindings (authoritative reference)

### Snip only
| Key | Command |
|---|---|
| `Tab` | Cycle detection mode: off → window → element → off |
| `G` | Toggle the full-screen crosshair guide lines (aux lines). *(`Alt` is kept for quick-mark — user's call, 2026-07-24.)* |
| `Shift` | Cycle the color format highlighted in the magnifier readout |
| `C` | Copy the pixel color under the cursor |
| `` ` `` or `!` | Show/hide the captured mouse cursor |
| `Ctrl + A` | Set snipping area to fullscreen |
| `R` | Set snipping area same as last successful snip |
| `,` | Previous snip record |
| `.` | Next snip record |
| `F5` | Refresh screenshot |
| `Ctrl + ↑ ← ↓ →` | Enlarge area by 1px |
| `Shift + ↑ ← ↓ →` | Shrink area by 1px |
| `Ctrl + T` | Paste screenshot to screen |
| `Enter` | Copy screenshot and quit |
| `Esc` | Abort snipping |
| Right-click | Re-decide area / abort |

### Paste only
| Key | Command |
|---|---|
| `1` / `2` | Rotate left (CCW) / right (CW) 90° |
| `3` / `4` | Flip horizontal / vertical |
| `5` / `6` | Grayscale / invert colors |
| `Esc` | Hide paint bar or close window |
| `Shift + Esc` | Destroy the window |
| `Ctrl + W` | Close the window |
| `Ctrl + A` | Select all visible windows |
| `Ctrl + Shift + C` | Copy plain text (if converted from text) |
| `Ctrl + V` | Replace image with clipboard content |
| `Ctrl + Shift + P` | Open Preferences |

### Common (snip + paste)
| Key | Command |
|---|---|
| `W S A D` | Move cursor by 1px |
| `↑ ← ↓ →` | Move snipping area / image window by 1px (when not editing) |
| `Space` | Show/hide the paint bar |
| `Ctrl + Z` | Undo |
| `Ctrl + Y` | Redo |
| `Ctrl + Shift + Z` | Remove all edits (cannot undo) |
| `Ctrl + C` | Copy the image |
| `Ctrl + Alt + C` | Copy the image as a file (when enabled in Output → Clipboard → Copy image as file) |
| `Ctrl + S` | Save as file |
| `Ctrl + Shift + S` | Save to Quick-Save folder |
| Right-click | Finish current shape (in edit mode) |
| Scroll | Decrease/increase pen width (in edit mode) |

> Global defaults: `F1` snip · `Ctrl + F1` snip and copy · `Shift + F1` custom snip ·
> `F3` paste · `Shift + F3` hide/show all windows. Snip active window and Quick snip are
> unbound by default (assign them under Preferences → Control). Quick snip is not a keymap
> entry — its trigger lives in `snip/quickSnip/trigger` as a **modifier-only combo**
> (Ctrl/Alt/Win) that you hold and drag anywhere on the desktop to capture and immediately
> Copy and/or Pin, with no toolbar (a global `WH_MOUSE_LL` gesture,
> `platform/win/WinQuickDragGesture`; extra actions chosen beside the box). A full hotkey
> can't be set here — only modifiers, whose held state is inert and so safe to drag with.
> Note: `WASD`↔arrow roles can be swapped via config; both are remappable.

---

## 5. System & Infrastructure

- **Customizable global hotkeys** (snip, paste, hide-all, click-through, …) with reset.
  Ignore hotkeys for specified apps.
- **System tray** icon + menu (left/middle/right actions; tray can be hidden).
- **Preferences dialog** — groups: General, Snip, Paste, Control/Hotkeys, Annotate,
  Interface, Output, Misc, Log.
- **Auto-save / Quick-save**; configurable Quick-Save folder & history directory.
- **Snip history** of the last *N* successful captures (count configurable).
- **Save formats:** PNG, JPG, BMP, TGA, ICO, TIFF, GIF, PDF (auto alpha-channel removal option).
  PDF is a one-page document holding the image losslessly; its page size is chosen in
  Output → Save to PDF.
- **Clipboard integration** — read/write image, color, plain/HTML text, file paths.
- **Multi-monitor + HiDPI / DPI-scale aware.**
- **Themes / appearance** (~~full magnifier styling~~ — done 2026-07-26: Preferences →
  Interface → Magnifier has grid lines, coordinates and a 50–200 % size; the loupe's remaining
  colours/fonts/layout stay fixed by decision. ~~The **theme system** itself is **deferred past
  Stage 7**~~ — **shipped 2026-08-07**: Preferences → Interface → **Theme** (Auto / Light / Dark,
  Auto following Windows live), persisted as `interface/theme`; the reserved `general/theme` was
  never implemented).
- Optional **GPU acceleration** for ~~mosaic/blur/shadow~~ **blur only** rendering — narrowed
  2026-07-26 by measurement (`test/tst_render_bench`): **mosaic** is *faster* on the CPU (the
  upload/readback round trip costs more than block-averaging), and **"shadow"** was never a
  `core/render` operation at all — it is the pin drop shadow, drawn as 5 rounded rects in
  `PinWindow::drawDropShadow`. Blur uses the GPU when the capability probe passes
  (`general/gpu_acceleration` = `auto`|`cpu`, default `auto`), else a CPU box blur.
- Logging settings via `config.ini`. ⚠️ The **process-priority** and
  **double-tap** knobs are **won't-do** (decided 2026-07-29): `general/process_priority` and
  `misc/double_tap_interval` were never implemented — `process_priority` had no agreed value
  vocabulary and `double_tap_interval` had no gesture to time — so both names were dropped
  rather than left reserved. The `docs/Usage.md` §4a that existed only to document them was
  removed with them; today's §4a documents the hand-edited `snip/behavior/clickSlopPx` /
  `clickWindowMs` keys instead.

---

## 6. Portability Note (Windows-first, portable design)

OS-specific capabilities must sit behind a thin platform interface so Windows is the first
concrete backend and other platforms can follow. Platform-dependent surface:

- **Screen capture** (multi-monitor, HiDPI pixel buffers, optional cursor capture).
- **Window / UI-element detection** under the cursor.
- **Global hotkey** registration.
- **Always-on-top, click-through, per-pixel-opacity** floating windows.
- **Clipboard** read/write across multiple formats.
- **System tray** integration.

Everything above the platform layer (annotation engine, pin/window model, history,
settings, color picker, UI) should remain platform-agnostic.
