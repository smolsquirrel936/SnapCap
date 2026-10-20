# SnapCap — Unimplemented Features

A verified ledger of `Features.md` items that are **not yet functional in code**. Compiled by
auditing the codebase directly (not just the plan docs) — every row was grep/read-checked against
`app/ ui/ core/ platform/`.

Last verified: **2026-07-26** (full audit); spot-checked **2026-10-20** against the features
shipped since — only the arrow-styles item had landed. (**GPU acceleration was removed this day** — it shipped in Stage 7
Phase B: blur runs on an offscreen GL context when the capability probe passes
(`general/gpu_acceleration=auto`, Preferences → General), falling back to a CPU box blur
otherwise. Mosaic deliberately stays CPU-only — it measured *slower* on the GPU — and "shadow"
was never a `core/render` op. **DXGI Desktop Duplication was added the same day**, when Stage 7
closed with Phase D deferred and no stage left owning it.) ("Rounded corners" was removed the same day when it shipped — a
Preferences → Snip → Display opt-in reveals a corner-shape chip on the readout row; scroll it to
bend, click it to toggle right-angle/rounded.) (That pass removed "Region-size input during snip" — the
click-to-edit `( x , y )` / `w x h` readout has shipped since `1249ee2`; the earlier audit
missed it while looking for a separate size-entry *control*. "Refresh screenshot (`F5`)" was
removed the same day when it shipped — `F5`/⟳ button, tap to refresh once, hold for continuous.
"`Tab` to switch line ↔ arrow" was dropped the same day as **won't do** (user's call): SnapCap has
no separate Arrow tool — the arrowhead is a 4-value `ArrowStyle` sub-dropdown on the Line tool, so a
binary toggle has nothing to toggle, and `Tab` is already the detect-mode / readout-field key.
*(Since 2026-08-14 the arrowhead is two per-end `CapStyle` dropdowns rather than `ArrowStyle`; the
reasoning is unchanged.)*
"Text horizontalize (`Shift` on rotated text)" was also dropped as **won't do** (user's call,
tested live): the existing rotate handle already makes it easy enough to drag a text box back to
0°, so a `Shift`-on-corner-drag shortcut isn't worth the extra gesture.) (A **Win-only hold +
drag** snip was dropped **2026-07-24**: Preferences → Control → **Quick snip** covers it — its
modifier-only hold-and-drag gesture (`platform/win/WinQuickDragGesture`) captures + Copies/Pins on
a held Ctrl/Alt/Win + drag.)

> **Legend**
> - **Verify:** `✓code` = confirmed absent/partial by reading the code this pass · `📋plan` = taken
>   from a stage deferral list (cross-checked, no contradicting code found).
> - **This file lists only what is missing.** Once a feature is implemented, its row is deleted
>   here and the implementation detail is recorded in `Implementation-Plan.md` (per-stage) and
>   `Architecture-FileMap.md` (per-file) instead — this ledger never carries "resolved" entries.

---

## §2 Image Annotation

| Feature | Verify | Notes |
|---|---|---|
| Annotation extras: magnify tool · rounded-rect · ~~dashed patterns~~ · ellipse/circle brush · ~~bidi & more arrow styles~~ · filled/outlined text · free-select · 1–5 pen sizes · reorderable colour buttons · scroll-on-colour opacity · per-tool colours · sharable palette | ✓code | Spot-checked magnify/rounded/dashed/free-select — none present. (**"Counting numbers" was removed from this row 2026-07-25**: it shipped as the **Number label** tool on 2026-07-20 — auto-numbering badge, three looks — and `Features.md` already strikes it out. The earlier "spot-checked counting … none present" note was wrong. On-canvas select/move/resize/**rotate**/restyle of non-text shapes also shipped. **"Dashed patterns" was struck out 2026-08-08**: rect/ellipse/pencil now have a 5-way line-style dropdown (`Style::lineStyle`, `edit/line_style_<tool>`) — the rest of this row is still open. **"Bidi & more arrow styles" was struck out 2026-10-20**: it shipped 2026-08-14 — the Line tool's two ends are decorated independently (`Style::startCap`/`endCap`, 10 `CapStyle`s: four arrowheads, filled/hollow dot and diamond, bar), so a double-headed arrow is just an arrowhead at both ends.) |

## §3 Paste / Pin

| Feature | Verify | Notes |
|---|---|---|
| Pin extras: crop · free-size thumbnail · background modes (transparent/chessboard) · topmost toggle · solo mode · web-image paste/drag · editable colour cards | ✓code | None present. |

## §3b Screen Recording

Shipped **2026-07-30 → 08-04** (`Features.md` §3b). Named gaps, none of which blocks the feature.

| Feature | Verify | Notes |
|---|---|---|
| Audio device pickers | ✓code | Both sources open the Windows **default** endpoint (`GetDefaultAudioEndpoint`). Needs endpoint enumeration plus a device-id parameter through `WinAudioCapture`; the default devices cover the common case. |
| DDA on non-primary monitors | ✓code | `ddagrab` is used only for a region wholly on the primary monitor (output 0); anything else takes `gdigrab`, which can record accelerated video as black. Needs a real DXGI output→monitor mapping. |
| Exact-cut trim | ✓code | Trim is a stream copy, so a cut snaps back to a keyframe — up to 2 s early (`gop_size = fps * 2`). The escape hatch is a re-encode path behind an "Exact cut" option; deliberately not built until the snapping proves annoying. |
| Mosaic / blur while recording | ✓code | Disabled: they sample the pixels beneath, and the annotation layer handed to the recorder is transparent there. Upgrade path: pass the rects to the recorder and redact each frame. |
| Per-app ("just this app") audio | — | Rejected for v1: needs a process picker and silently fails before Windows 10 20H1. |

## §3c Scrolling Capture

Shipped **2026-08-09**, deliberately as a first cut — the user asked
for "the basic one only, more convenience operations later". These are the named gaps, none of
which blocks the feature.

| Feature | Verify | Notes |
|---|---|---|
| ~~Horizontal (left/right) scrolling capture~~ | — | **Shipped 2026-08-09** — the direction button's backend landed the same day the button did. Built as the **transpose** this row predicted: each frame is rotated 90° clockwise into the unchanged vertical `ScrollStitcher` and the canvas is rotated back at Done, so all 405 lines of matching and their 19 tests were reused untouched. The real work was where this row said it would be — `WM_MOUSEHWHEEL` in the hook, `MOUSEEVENTF_HWHEEL` injection for Auto, and an axis-aware preview strip — plus Move mode's clamp. |
| Non-wheel scroll sources | ✓code | Only `WM_MOUSEWHEEL` triggers a grab. A scrollbar drag, `PageDown`, a touchpad flick or auto-scroll all move the content without capturing it. The planned follow-up is a periodic change-poll alongside the hook — it also catches these, at the cost of CPU and of mistaking unrelated animation for scrolling. |
| Sticky sidebars / headers **reconstructed**, not just matched | ✓code | Matching survives them (per-row column bands + per-strip hashes). The *output* cannot: only newly exposed rows are appended, so pinned furniture carries whatever it showed at that moment and the result has correct content in the middle with repeated/broken edges. A fix means detecting the static columns/rows and sampling them once. Inherent to a region wider than the thing that scrolls; a narrower region is the workaround. |
| ~~Auto-scroll~~ | — | **Shipped 2026-08-09**, the same day this row was written: the **Auto** button on the 2nd-level bar drives the page itself with an adaptive step, stopping at the end of the content, when the pointer leaves the region, or when the user takes the wheel. Only *one-key capture* (a single press that captures a whole page unattended, end to end) is still open, and it is a thin layer on top of Auto rather than new machinery. |
| Annotating the stitched result | ✓code | Tools are off for the whole session and the result is only ever shown scaled-to-fit. Editing it in place needs a pan/zoom canvas the overlay does not have. Workaround: pin the result and annotate the pin. |
| Subpixel re-rasterisation fallback | ✓code | Row/strip hashes are exact-equality, which is right because Windows scrolls by whole pixels. Content re-rendered at a different subpixel phase would defeat the shortlist; the fallback would be a coarse diff on a downscaled greyscale copy. Not observed in practice. |
| Wide blank margins / periodic content | ✓code | Both **stall** growth, never corrupt it. A row counts as matching when a contiguous band ≥ 35% of the width agrees (the rule that survives sticky sidebars), and that cannot tell a static sidebar from a blank margin — so a centred document with very wide plain margins (a PDF or word-processor view) can match at `dy = 0`. Runs of identical rows (uniform table rows, flat backgrounds) make the offset genuinely ambiguous; ties go to the smaller offset, so the strip stops growing rather than gaining rows that were never on screen. Workaround for both: narrow the region onto the content. A real fix would judge whether the agreeing band carries information — per offset, since per row is too strict (a blank row inside a scrolling column legitimately agrees). |

## §5 System & Infrastructure

| Feature | Verify | Notes |
|---|---|---|
| DXGI Desktop Duplication capture backend | ✓code | **Not a `Features.md` row** — a capture *backend*, listed here because it is a real user-visible correctness gap that **no stage owns** after Stage 7 closed with it deferred (same reason Native share is here). Only backend is GDI BitBlt (`platform/win/WinScreenCapture`); `general/capture_backend` has no consumer. BitBlt returns **black or stale pixels for hardware-overlay content** (accelerated video, some games/HDR paths) — the "I snipped a video and got a black rectangle" bug. Fully designed in `Implementation-Plan.md` Stage 7 Phase D (opt-in, BitBlt stays default, auto-fallback on any DDA error) — implementation only. **It is a correctness fix, not a speed-up**; do not sell it as faster capture. **Updated 2026-08-04:** its value is no longer unproven — the *recorder* uses DDA today (`ddagrab`, via `platform/win/WinRecorder`) and captures hardware-overlay content correctly, so the mechanism is demonstrated to work on this hardware. What remains for snips is the DXGI **output-to-monitor mapping**: the recorder trusts output 0 = primary and falls back to `gdigrab` for anything else, which is the same gap Phase D would have to close properly. |
| Native share | 📋plan | No share code anywhere in `ui/`/`core/`/`platform/` (no share-sheet / `IDataTransferManager` call). **No stage owns it** — added here 2026-07-26 after it was found stale in Stage 7's deferral line, which wrongly paired it with copy-image-as-file (that one *is* implemented: `output/clipboard/copyAsFile` + `Ctrl+Alt+C`). |
