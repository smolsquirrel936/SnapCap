# SnapCap — Tech Stack, Architecture & i18n Foundation

## Context

SnapCap is a multifunctional screen-capture app (feature catalog in
`Features.md`). It targets **Windows first** but must be **portable** to other platforms
later. Two foundational decisions were needed before any code: the tech stack, and how UI
language strings are stored. Both are now decided.

This document defines the stack, the layered architecture (with a thin OS-abstraction so
Windows is the first of several backends), the project scaffold, and — most importantly —
an **i18n architecture that keeps every UI string out of code** in editable YAML files.

## Decisions (confirmed)

- **Stack:** Qt 6 + C++.
  - **License:** ship under **Qt's LGPLv3** with **dynamic linking** (no commercial license
    needed). Obligations confirmed from Qt's official pages — see "Licensing & LGPLv3
    compliance". It constrains how we link/distribute, not the architecture. *(SnapCap's own
    code was later released under the **MIT** license, 2026-10-20 — `LICENSE.txt`.)*
- **i18n:** **YAML**, one file per locale, **key → string**. No literal UI strings in code.

## Why not Qt's `tr()` for i18n

Qt's `tr()` approach keeps source strings *in code*. The
user explicitly wants strings *out of code* → we use a custom YAML-backed string provider
instead of (or layered over) Qt's `tr()`.

## Target architecture (layered, portable)

```
SnapCap/
  app/            # main(), startup, single-instance guard, tray bootstrap
  ui/             # Qt Widgets: snip overlay, paint bar, pin window, preferences
  core/           # platform-AGNOSTIC: annotation model, pin/window model,
                  # history, settings, color-picker logic, image I/O, i18n
  platform/       # OS-ABSTRACTION interfaces + per-OS implementations
    platform.h    #   IScreenCapture, IGlobalHotkey, IElementDetect,
                  #   IPinSurface (always-on-top/click-through/per-pixel-alpha),
                  #   IClipboard, ITray (+ later: autostart, OCR, barcode,
                  #   recording, playback and input-hook interfaces)
    win/          #   Windows impls (Win32) — the only OS backend built now
    ocr/ barcode/ media/   # OS-neutral library backends (Tesseract, ZXing, FFmpeg)
    (mac/, linux/ stubs reserved for later)
  res/            # keymap.default.yaml, preferences.qss
  icons/          # toolbar icons (SVG)
  lang/           # en.yaml, zh_cn.yaml, zh_tw.yaml, ... (editable translation files)
  third_party/    # Tesseract/Leptonica + FFmpeg, built by tools/build_*.ps1 (git-ignored)
  CMakeLists.txt
```

**What Qt already gives us cross-platform** (so the abstraction stays thin): screen pixels
(`QScreen::grabWindow`), clipboard (`QClipboard`, multi-format via `QMimeData`), tray
(`QSystemTrayIcon`), translucent/click-through windows (`Qt::FramelessWindowHint` +
`WA_TranslucentBackground` + `Qt::WindowTransparentForInput` / `setWindowOpacity`),
image I/O.

**What needs platform code (the real reason for `platform/`):**
- **Global hotkeys** — not in Qt. Windows: `RegisterHotKey` (or the `QHotkey` library).
- **Window / UI-element detection** under cursor — Windows: UI Automation + `EnumWindows`.
- **High-perf / advanced capture** — Windows: BitBlt is the **default backend, permanently**;
  Desktop Duplication (DXGI) is designed as an **opt-in** one that auto-falls back to BitBlt
  on any error — deferred out of Stage 7 and not yet built (`Unimplemented-Features.md` §5). It fixes black/stale hardware-overlay (video) captures rather than making capture
  faster — for one-shot snips a cold DDA grab can be *slower*.
- **Per-pixel-alpha + true click-through pins** — Windows: `WS_EX_LAYERED |
  WS_EX_TRANSPARENT`, `UpdateLayeredWindow` when Qt's window flags are insufficient.

## i18n architecture (the headline requirement)

Goal: a translator can add a language by dropping a new `lang/<locale>.yaml` and editing
text — **zero code changes, no recompilation**.

**File format** — `lang/en.yaml` is the canonical/default; others mirror its keys:
```yaml
# lang/en.yaml
meta:
  name: English
  locale: en
menu:
  snip: "Snip"
  paste: "Paste"
  quit: "Quit"
snip:
  toolbar:
    copy: "Copy"
    save: "Save"
  hint_size: "{w} × {h}"          # {placeholders} interpolated at runtime
preferences:
  title: "Preferences"
```

**Runtime component — `core/i18n` (singleton `I18n`):**
- Loads YAML via **`yaml-cpp`** into a flat `QHash<QString, QString>` (dotted keys).
- Lookup API: `I18n::t("menu.snip")` → `QString`; overload `t(key, {{"w",1280},{"h",720}})`
  for interpolation. Returns the key itself (or default-locale value) on miss, so missing
  translations are visible but never crash.
- **Fallback chain:** requested locale → `en` (default) → raw key.
- **Runtime language switch:** `I18n::setLocale("zh_cn")` reloads and emits
  `localeChanged()`; widgets re-apply text in a `retranslate()` slot (Qt's standard
  retranslate pattern, but sourced from YAML instead of `.qm`).
- Locale list is **discovered by scanning `lang/*.yaml`** — adding a file is enough to make
  it appear in the language dropdown (it reads `meta.name`).

**Convention so strings never live in code:**
- UI is built in code (not `.ui` literals) and every label is set from `t(key)`. *(The planned
  central helper that would walk the widget tree re-applying an `i18nKey` property was never
  built: each long-lived widget re-applies its own keys in its `retranslate()` slot, as above.)*
- A small **CI/build check script** (`tools/i18n_check`) parses sources for `t("...")` keys
  and verifies every key exists in `en.yaml`, and that other locales don't have orphan/
  missing keys. Keeps translations honest without runtime cost.

## Dependencies & build

- **Qt 6.11.1** (MinGW kit): `find_package` modules Core, Gui, Widgets, Network, OpenGL;
  `windeployqt` additionally ships Qt SVG (the SVG toolbar icons) and, when installed, Qt Image
  Formats (TIFF).
- **yaml-cpp** — YAML parsing for i18n and the default keymap. Settings use Qt `QSettings`
  (INI), not YAML.
- **ZXing-C++** (Apache-2.0) — barcode/QR decoding for the snip code hint. Fetched from source by
  CMake (`SNAPCAP_FETCH_ZXING`, default ON) and **optional**: without it `Platform::barcode`
  stays null and the hint never appears, so the build never hard-depends on the network.
- **Tesseract 5.5 + Leptonica** (Apache-2.0 / BSD-2) — OCR, static-built once with
  `tools/build_tesseract.ps1`; optional (no build → `Platform::ocr` is null).
- **FFmpeg** (LGPLv3, dynamic) — screen recording and its preview, a stripped build made once with
  `tools/build_ffmpeg.ps1`; optional (no build → `Platform::recorder` is null). Its own LGPL duties
  are in `Licence-Compliance-Plan.md`.
- Later, as features land: **hoedown or md4c** (markdown→image), OpenSSL (only if we add
  activation/network features). ~~QuaZip~~ — **won't-do (2026-07-26)**: its only planned use was
  the pin-session backup archive, and `core/SessionStore` stays a plain on-disk folder.
- **Build system:** CMake + Ninja with the **MinGW** toolchain bundled with Qt (g++ 13.1), Qt from
  the official installer. ZXing (by default) and yaml-cpp (with `-DSNAPCAP_FETCH_YAMLCPP=ON`) are
built from source through CMake `FetchContent`. *(The original
  plan — MSVC 2022 + vcpkg — was dropped in Stage 0; see `Implementation-Plan.md` Stage 0.)*
- **Packaging:** `windeployqt` to bundle Qt DLLs + plugins, shipped as a **portable ZIP**. **No installer** — decided 2026-07-26;
  Inno Setup / MSIX are both ruled out, MSIX specifically because its package integrity would
  stop the user replacing the bundled Qt (the LGPLv3 §4 relinking freedom). Built in Stage 7
  Phase C as `install()` rules + CPack ZIP (`cmake --build build --target package`); rationale in
  `Implementation-Plan.md` Stage 7 → Packaging.

## Recommended first milestone (foundation vertical slice)

Prove the stack + i18n end-to-end before broad feature work (full feature build-out then
follows `Features.md`). M0 delivers:

1. CMake project scaffolding + the directory layout above; `windeployqt` packaging works.
2. Tray app with single-instance guard; **F1 global hotkey** registered (validates
   `platform/win` hotkey path).
3. **Region snip** overlay → copy to clipboard + save to file (validates capture + overlay).
4. **One pin window**: always-on-top, scale (scroll), opacity (`Ctrl+scroll`) — validates the
   layered/click-through surface, the signature hard feature.
5. **i18n live**: every visible string from `lang/en.yaml`; ship `lang/zh_cn.yaml` +
   `lang/zh_tw.yaml`; a Preferences language dropdown that hot-switches with no restart.

This exercises every architecturally risky piece (global hotkey, capture overlay, layered
pin window, YAML i18n) in one thin slice.

## Licensing & LGPLv3 compliance (confirmed)

SnapCap uses Qt 6 under **LGPLv3** (no commercial license). Verified against Qt's
official pages — https://www.qt.io/development/open-source-lgpl-obligations and
https://doc.qt.io/qt-6/lgpl.html. SnapCap's own code is free to carry its own license (it is
MIT) as long as it is merely a "work that uses the library" — which requires **dynamic linking**
and the items below. Each obligation maps to a concrete action:

1. **Dynamic linking (mandatory).** Link Qt as shared **DLLs** (via
   `windeployqt`). **Do NOT statically link Qt** — static linking would make SnapCap a
   derivative subject to LGPL/GPL (or require a commercial license).
2. **User right to relink/replace Qt.** The end user must be able to swap in a modified or
   newer Qt and run the result. Dynamic DLLs satisfy this; additionally we must **provide
   sufficient install info** and **not lock the device** (no anti-tampering that blocks
   relinking — no "tivoization").
3. **Provide license texts.** Bundle the **LGPLv3** text (and the **GPLv3** it references)
   with the app, plus a **prominent notice** that SnapCap uses Qt under LGPLv3 (in the About
   dialog and as text files inside the portable ZIP — there is no installer to carry it).
4. **Provide Qt's source.** Provide the **complete corresponding source of the exact Qt
   version used** (incl. any modifications we make to Qt) **or a written offer** to obtain
   it. Applies even though we don't modify Qt. **Decided 2026-07-26: we take the first form
   and host the source ourselves** — the written offer is rejected because it binds us for 3
   years to supply source to anyone who asks. Action: **mirror** the matching
   `qt-everywhere-src-<version>` archive on the same release page as the portable ZIP and link
   it from **Preferences → About** (a bare `qt.io` link is not enough — the obligation is ours
   if upstream retires the version). Re-mirror on every Qt bump and **keep older archives up**
   while the releases built from them are still downloadable.
5. **Avoid GPL-only modules.** Some Qt modules are **GPL/commercial-only, not LGPL** (e.g.
   Qt Charts, Qt Data Visualization, Qt Virtual Keyboard). Our module set —
   **Core, Gui, Widgets, Network, OpenGL**, plus Svg and Image Formats deployed as plugins — is
   **all LGPLv3-available**, so we are clear. **Before adding any new Qt module, verify its
   license** (Qt ships per-module license info; use the "Qt Attributions" / Licenses report).
6. **No restriction of recipients' rights.** The LGPL freedoms cannot be negotiated away or
   restricted by our EULA.

**Third-party dependency licenses (track in a NOTICE/attributions file):** yaml-cpp (MIT),
hoedown/md4c (permissive), OpenSSL (Apache-2.0), Tesseract (Apache-2.0) + Leptonica (BSD-2)
for OCR, **ZXing-C++ (Apache-2.0)** for the snip QR hint, and **FFmpeg (LGPLv3)** for
recording. (QuaZip was dropped 2026-07-26 — no longer a dependency, so it needs no notice. The
LGPLv3 dynamic-link discipline below applies to **Qt** and, since recording, to **FFmpeg** —
`Licence-Compliance-Plan.md`.) Maintain `THIRD_PARTY_NOTICES.txt` and surface it in the About dialog.

**Compliance checklist (build/release gate)** — all met (Stage 7 Phase A/C, source mirror
2026-08-04; the release-day order is `Licence-Compliance-Plan.md` §4):
- [x] Qt linked dynamically; verify no `-static` Qt in the build.
- [x] `windeployqt` bundles Qt DLLs/plugins so they are replaceable *(portable ZIP — the shipped
      tree is neither signed nor integrity-sealed, so a plain file copy relinks it)*.
- [x] App ships `LICENSE.LGPLv3.txt`, `LICENSE.GPLv3.txt`, and `THIRD_PARTY_NOTICES.txt`.
- [x] Preferences → About: "Uses Qt under LGPLv3" + a link to **our own mirror** of the Qt
      source for the exact version shipped (no separate About dialog — the page is expanded
      in place).
- [x] No GPL-only Qt module pulled in (audit `CMakeLists` `find_package(Qt6 ...)` list).

## Verification

- **Build:** the MinGW configure/build in `docs/Development.md` → Build & test; run
  `build\SnapCap.exe` (or the packaged ZIP on a clean machine) and confirm it launches
  standalone.
- **Hotkey/capture/pin:** press F1 → select region → confirm clipboard + saved file; pin an
  image and verify always-on-top, scroll-to-scale, `Ctrl+scroll` opacity.
- **i18n:** launch in English; switch to 中文 in Preferences and confirm all strings update
  live; add a throwaway `lang/xx.yaml` and confirm it auto-appears in the dropdown (proves
  no-code language addition); delete a key from a locale and confirm graceful fallback +
  the `i18n_check` script flags it.
