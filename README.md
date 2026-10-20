<div align="center">

# SnapCap

**A screen-capture app for Windows — snip, annotate, and pin images on top of everything.**

[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D6)](#download)
[![Download](https://img.shields.io/badge/download-portable%20ZIP-success)](../../releases/latest)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)](CMakeLists.txt)
[![Qt](https://img.shields.io/badge/Qt-6.11-41CD52)](https://www.qt.io/)
[![Qt license](https://img.shields.io/badge/Qt-LGPL--3.0%20(dynamic)-blue)](#license)

[Download](#download) · [Features](#features) · [Build](#building-from-source) · [Usage](docs/Usage.md) · [Architecture](#architecture) · [Localization](#localization) · [License](#license)

</div>

---

SnapCap freezes your screen, lets you grab any region — window, UI element, or freehand — draw
on it, read the text out of it, and then **pin the result to your desktop** as a floating,
always-on-top window you can move, scale, fade, and annotate.

It runs from the tray, ships as a **portable folder** (unzip and run — no installer, and
nothing in the registry unless you turn on run-on-login), and keeps **every UI string in
editable YAML files**, so adding a language never needs a recompile.

## Features

### 🖼 Snip

- **Smart detection** — windows and individual UI elements (buttons, text fields, browser
  elements) highlight under the cursor. `Tab` cycles off → window → element.
- **Pixel-precise regions** — `WASD` nudges the cursor, arrows move the region, `Ctrl`/`Shift`
  + arrows grow/shrink it. Corner drags snap to ratio (`Shift`) or square (`Ctrl`).
- **Type exact coordinates** — click any number in the `( x , y )` / `w x h` readout to edit it.
- **Magnifier** with live zoom, region size, and the color under the cursor; `C` copies the
  pixel color, `Shift` cycles RGB/HEX formats, `G` toggles crosshair guides.
- **Rounded corners**, **cursor capture**, `F5` refresh, custom-size and delayed capture.
- **Barcode & QR detection** — every region is scanned across 18 symbologies (ZXing); a hit
  shows the decoded payload as a clickable chip.
- **Quick snip** — hold a modifier combo (e.g. `Ctrl+Alt`) and drag anywhere to capture and
  copy/pin instantly, with no toolbar.
- **Two history systems** — a disk-persisted strip of past captures (`,` / `.`) and a
  session-only ring of past *areas* (`R` / `Shift+R`).
- **Multi-monitor and mixed-DPI aware** — one drag can span monitors running different scale
  factors; the whole pipeline works in native capture pixels.

### ✏️ Annotation

- Rectangle, ellipse, line, arrow, pencil, marker pen, **mosaic**, **Gaussian blur**, text,
  **auto-numbering labels**, and eraser.
- **On-canvas editing** — re-select a committed shape to move, resize, rotate, or restyle it.
- **Quick-mark UI elements** — hold `Alt` to preview the element under the cursor, then
  `Alt` + right-click to redact or highlight it exactly.
- Constrained drawing (`Ctrl` = square/circle, `Shift` = 45° lines), scroll-to-resize pen
  width, full undo/redo, and a custom color board with alpha.
- Blur is **GPU-accelerated** when a suitable graphics card is found (~10 ms vs ~235 ms for a
  4K region), with automatic CPU fallback and byte-identical output either way.

### 🔤 OCR

- Turn any capture into **selectable text in place** — words get a subtle plate, the region
  becomes a text surface, and you drag to select in reading order.
- Works on both snips and pins. `Ctrl+A` select all, `Ctrl+C` copy and exit.
- Ships with English; more languages install from **Preferences → OCR** or by dropping a
  `.traineddata` file into the portable `tessdata/` folder.

### 🎬 Recording

- Record any snip region to **MP4, MKV, WebM, or GIF** — H.264, H.265, AV1 or VP9, using a
  hardware encoder when one is available and a software fallback otherwise.
- The region goes **live** while recording: clicks pass through to the app underneath, and you
  can **draw on the video** with the annotation tools mid-recording.
- Countdown, pause/resume (paused time is cut from the file), system sound and microphone.
- A **preview window** to review and trim the clip after stopping.

### 📜 Scrolling capture

- Capture content **taller or wider than the screen** — scroll with the mouse wheel and the frames
  are stitched as you go, vertically or horizontally.
- **Auto** scrolls the page for you; **Trim** throws away part of the result before you keep it.

### 📌 Paste / Pin

- Turn the clipboard into a floating always-on-top window: **images, colors, plain/HTML text,
  and file paths** all paste.
- **Transform** — rotate (`1`/`2`), flip (`3`/`4`), scale by scroll, fade with `Ctrl`+scroll,
  thumbnail with `Shift`+double-click, reset with a middle-click.
- **Manage** — hide/show all (`Shift+F3`), magnetic edge snapping, click-through mode,
  named **pin groups** that can be put away to disk and restored.
- **Closed pins are recoverable** — press the paste hotkey again to bring them back,
  oldest-first.
- **GIF playback** on pins, with frame-by-frame stepping.

### ⚙️ System

- Fully **remappable global hotkeys** with per-app exclusions (release the hotkeys while a
  given app is focused, so `F1` reaches your game).
- **Tray icon** with left/middle/right actions, run-on-login (standard or elevated), and a
  sidebar **Preferences** window.
- **Save formats:** PNG, JPG, BMP, TGA, ICO, GIF, PDF — plus TIFF when Qt's optional Image Formats
  module is deployed. TGA, GIF and PDF are written by SnapCap's own encoders, so they need no
  plugin; the PDF is a single page holding the image losslessly, at your choice of page size.
- **Portable settings** — `config.ini` lives next to the exe and can be relocated.

**Defaults:** `F1` snip · `Ctrl+F1` snip and copy · `Shift+F1` custom snip · `F3` paste ·
`Shift+F3` hide/show all pins. The full key reference is in
[**docs/Usage.md**](docs/Usage.md).

## Download

Grab **`SnapCap-<version>-win64.zip`** from the
[**Releases**](../../releases/latest) page, unzip it anywhere you can write — a normal folder, a
USB stick — and run `SnapCap.exe`.

There is **no installer**: Qt and every other dependency sit inside the folder, and
`config.ini` lives next to the exe. To move your setup to another machine, copy the folder.

**To uninstall, delete the folder** — that is the whole app. Two optional settings write
outside it, and both are **off by default**:

- **Run on login** adds a `SnapCap` value under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. Its *elevated* form instead creates a
  `SnapCap` scheduled task. Turn it off before you delete the folder — SnapCap offers to
  restart as administrator if clearing the scheduled task requires it.
- **Copy image as file** (Preferences → Output → Clipboard) writes the copied images to
  `%TEMP%\SnapCap\` and does not clean them up.

Turn run-on-login off before deleting the folder, and empty `%TEMP%\SnapCap\` if you ever used
copy-as-file, and nothing is left behind. Saved screenshots and recordings are of course
wherever you told SnapCap to put them — by default the Desktop, Pictures, and Videos.

**Requires Windows 10 / 11 (x64)** — and nothing else; there is no runtime to install first.

SnapCap is a **tray app** — it opens no window on launch. On Windows 11 the icon hides in the
`^` overflow flyout. Press `F1` to snip.

## Building from source

This section is the quick start. The full build and release workflow, the architecture, the
repository's conventions, and the pitfalls worth knowing before you change anything are in
[`docs/Development.md`](docs/Development.md).

### Requirements

| | |
|---|---|
| **Qt** | 6.11.1, **MinGW** kit — install via the [Qt Online Installer](https://www.qt.io/download-qt-installer) |
| **Toolchain** | The MinGW 13.1, Ninja, and CMake ≥ 3.21 bundled with Qt (`C:\Qt\Tools\…`) |
| **Optional** | Qt *Image Formats* module for TIFF support |

Qt must be the **MinGW** kit, not MSVC — SnapCap statically links dependencies built with the
same compiler.

### Build

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Qt\6.11.1\mingw_64\bin;" + $env:PATH

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 -DCMAKE_CXX_COMPILER=g++ `
      -DSNAPCAP_FETCH_YAMLCPP=ON
cmake --build build
ctest --test-dir build --output-on-failure

build\SnapCap.exe
```

> **`-DSNAPCAP_FETCH_YAMLCPP=ON`** builds yaml-cpp from source. Use it whenever an
> incompatible (e.g. conda/MSVC) yaml-cpp is on the system, where `find_package` would
> otherwise pick a library this toolchain cannot link.
>
> **Kill any running `SnapCap.exe` first** — it locks the output binary and the link step
> fails with *"cannot open output file … Permission denied"*.

### Optional dependencies

All three are optional in the same way: without them the corresponding feature simply stays
inactive at runtime, and the build never hard-depends on the network.

| Dependency | Powers | How |
|---|---|---|
| **ZXing-C++** 2.2.1 | Barcode / QR detection | Fetched and built automatically (`-DSNAPCAP_FETCH_ZXING=ON`, the default) |
| **Tesseract** 5.5.0 + Leptonica | OCR | Build once with `tools/build_tesseract.ps1`, then re-run CMake |
| **FFmpeg** (stripped LGPLv3 build) | Screen recording + preview | Build once with `tools/build_ffmpeg.ps1` (needs [MSYS2](https://www.msys2.org) for `make`/`nasm`; compiles with the Qt MinGW kit), then re-run CMake |

The English OCR language pack (`eng.traineddata`) is downloaded at **configure** time.

### Packaging a release

This produces exactly the ZIP that is published on the [Releases](../../releases) page:

```powershell
cmake --build build --target package
# -> build/package/SnapCap-<version>-win64.zip
```

The staging tree is an `install()` **whitelist**, so `config.ini`, logs, history, and test
binaries can never leak into a release, and `windeployqt` is re-run against the staged binary
on every packaging run.

### Running the tests

```powershell
ctest --test-dir build --output-on-failure          # all 32 suites
ctest --test-dir build -R tst_selection             # one suite
```

Pure `core/` logic is unit-tested with QtTest; GUI and Win32 pieces are verified manually.
`tst_render_bench` is a benchmark and is deliberately **not** registered with CTest — it
asserts nothing about wall-clock time, so run `build\tst_render_bench.exe` by hand.

## Architecture

Dependencies point **inward only**: `app/ → ui/ → core/ → platform/platform.h`. `core/` is
platform- and widget-agnostic, which is what makes it headlessly testable; only `app/` knows a
concrete backend exists.

```
app/         main(), application shell, single-instance guard
ui/          Qt Widgets — snip overlay, paint bar, pin windows, preferences
core/        platform-agnostic logic (built as snapcap_core, linked by app + tests)
  action/      ActionId / ActionRegistry — the spine every input dispatches through
  annotation/  shape model, document, undo stack
  render/      CPU raster ops + offscreen GL blur
  codec/       TGA, GIF + PDF encoders, animated-image decoding
  i18n/        YAML string provider
  ocr/  barcode/  recording/  scroll/
platform/    platform.h — the OS interface set
  win/         the OS-specific backend (Win32)
  ocr/  barcode/  media/   OS-neutral backends (Tesseract, ZXing, FFmpeg)
lang/        editable translations: en.yaml, zh_cn.yaml, …
res/         keymap.default.yaml, preferences.qss
icons/       toolbar icons (SVG)
test/        one tst_<thing>.cpp per unit
tools/       i18n_check.py, build_tesseract.ps1, build_ffmpeg.ps1, dev demos
docs/        Usage.md (user guide), Development.md (developer guide)
claude-plan/ design documents: feature catalog, roadmap, file map
```

Three ideas carry most of the design:

- **Everything is an action.** Global hotkeys and the tray menu dispatch `ActionId`s through
  `ActionRegistry`; a `KeyContext` (Global/Snip/Pin/Edit) disambiguates keys that mean different
  things in different modes. Defaults live in `res/keymap.default.yaml`; `config.ini` stores only
  user overrides. (Keys inside the snip overlay and pins are handled directly and are not
  remappable.)
- **The platform is an interface.** `platform.h` declares screen capture, global hotkeys,
  element detection, autostart, clipboard, pin surfaces, tray, and the backends behind OCR,
  barcodes, recording and playback. A new OS means implementing those and a `createPlatform()` —
  no changes to `core/` or `ui/`.
- **The snip pipeline works in capture pixels**, not Qt logical coordinates. `core/ScreenLayout`
  converts at the view boundary, which is what makes a single drag across a 150 % and a 100 %
  monitor land on the right pixels.

## Localization

**No UI string is hard-coded.** Every string is a key looked up at runtime from
`lang/<locale>.yaml`, so translators never touch code and the app re-translates live when the
language changes.

Bundled locales: **English**, Simplified & Traditional **Chinese**, **German**, **Spanish
(MX)**, **French**, **Japanese**, **Korean**, **Dutch**, **Portuguese** (BR & PT), **Russian**.

Adding a language takes no recompile — copy `lang/en.yaml` to e.g. `lang/it_it.yaml`, set
`meta.name`, translate the values, and it appears in the language picker. Validate key
consistency across locales with:

```sh
python tools/i18n_check.py
```

## Documentation

| Document | What's in it |
|---|---|
| [`docs/Usage.md`](docs/Usage.md) | **The user guide** — every gesture and hotkey, verified against the source |
| [`docs/Development.md`](docs/Development.md) | Build and release workflow, architecture, conventions, and pitfalls |
| [`claude-plan/`](claude-plan) | Design documents — feature catalog, staged roadmap, per-file architecture map, licence compliance, and the ledger of unimplemented features |

## License

SnapCap is released under the **[MIT License](LICENSE.txt)**. The third-party components it
uses keep their own licenses, listed below.

SnapCap uses **Qt 6 under the LGPLv3**. Qt is **dynamically linked and unmodified** — every
`Qt6*.dll` sits beside `SnapCap.exe` and can be replaced with a file copy, which is precisely
why SnapCap ships as a portable folder rather than an installer. The full texts are bundled as
[`LICENSE.LGPLv3.txt`](LICENSE.LGPLv3.txt) and [`LICENSE.GPLv3.txt`](LICENSE.GPLv3.txt), and
**Preferences → About** opens them from within the app.
[`LICENSE.Apache-2.0.txt`](LICENSE.Apache-2.0.txt) ships alongside them for the Apache-2.0
components below.

Third-party components, their licenses, and how each is linked are documented in
[`THIRD_PARTY_NOTICES.txt`](THIRD_PARTY_NOTICES.txt):

| Component | License | Linkage |
|---|---|---|
| Qt 6.11.1 | LGPL-3.0-or-later | dynamic |
| yaml-cpp 0.8.0 | MIT | static |
| ZXing-C++ 2.2.1 | Apache-2.0 | static |
| Tesseract OCR 5.5.0 | Apache-2.0 | static |
| Leptonica 1.85.0 | BSD-2-Clause | static |
| tessdata_fast language data | Apache-2.0 | data file |
| FFmpeg (commit `8c9502e9b0`) | LGPL-3.0-or-later | dynamic |
| ↳ built into the FFmpeg DLLs: openh264, kvazaar, dav1d, SVT-AV1, libvpx, Opus, libvpl | BSD-2/BSD-3/BSD-3-Clear/MIT | static, inside FFmpeg |
| GCC runtime / winpthreads | GPL-3.0 *with Runtime Library Exception* / MIT + BSD-3-Clause | dynamic |
| Mesa llvmpipe (`opengl32sw.dll`, with LLVM) | MIT / NCSA | dynamic |
| Microsoft D3D shader compiler (`D3Dcompiler_47.dll`) | Windows SDK redistributable | dynamic |

Qt and FFmpeg are both **dynamically linked and replaceable**, and the Corresponding Source for
the exact versions shipped is linked from **Preferences → About**. Qt also contains third-party
code of its own (FreeType, HarfBuzz, libpng, libjpeg-turbo, …); those components and their full
license texts are in the appendices of `THIRD_PARTY_NOTICES.txt`.
