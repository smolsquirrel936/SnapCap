# Developing SnapCap

The README covers the quick build. This document is the rest of what you need to work on the code:
the full build and release workflow, the architecture that spans files, the repository's
conventions, and the pitfalls that are easy to reintroduce.

SnapCap is a screen-capture app — **Snip**, **Image Annotation**, and
**Paste/Pin** floating windows — Windows-first and designed to be portable. Qt 6 + C++ (Widgets),
linked **dynamically** under **LGPLv3**: do not statically link Qt, and do not pull in GPL-only Qt
modules.

## Design documents

`claude-plan/` is the **design source of truth** — read the relevant file before non-trivial work:

- `Features.md` — the full feature catalog and the authoritative key-binding tables
  (Snip/Paste/Common).
- `Tech-Stack-Plan.md` — the stack decision, layered architecture, i18n design, LGPLv3 obligations.
- `Architecture-FileMap.md` — what every file does, the dependency rule, and call flows.
- `Implementation-Plan.md` — the staged roadmap (Stage 0–7) with per-stage status, objectives and
  tests.
- `Unimplemented-Features.md` — the ledger of `Features.md` items not yet functional in code.
- `Licence-Compliance-Plan.md` — what must be true before a public ZIP ships: the Qt and FFmpeg
  LGPLv3 corresponding-source duties, the release-day order, the H.26x patent decision, and (§1.3)
  how the stripped FFmpeg build came about.

## Project status

All roadmap stages (0–7) are signed off; there is no stage in progress. What remains is deferred
work plus the backlogs in `Unimplemented-Features.md`:

- **DXGI Desktop Duplication capture (Stage 7 Phase D)** — deferred, not dropped.
  `general/capture_backend` has no consumer and GDI BitBlt is the only snip backend, so
  hardware-overlay content (accelerated video) can still capture black. The design is preserved at
  Stage 7 Phase D, as an opt-in backend with BitBlt staying the default and auto-fallback on any DDA
  error. Tracked in `Unimplemented-Features.md` §5.
- **Update page** — deferred past Stage 7. (The **theme system**, deferred alongside it, shipped
  2026-08-07 as `interface/theme`: Auto / Light / Dark.)

Per-stage detail, including every item dropped as won't-do, is in `Implementation-Plan.md`.

## Build & test (Windows / MinGW)

The Qt kit is the **MinGW** one (Qt 6.11.1 at `C:\Qt`), **not MSVC** — use the bundled
g++/CMake/Ninja. Put the toolchain on `PATH` first.

**cmd.exe:**

```sh
set PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;%PATH%

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 -DCMAKE_CXX_COMPILER=g++ ^
      -DSNAPCAP_FETCH_YAMLCPP=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

**PowerShell** (use `$env:PATH` and backtick continuation, not cmd's `set`/`^`; add the Qt `bin`
dir too so the built exes find the Qt/MinGW DLLs):

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Qt\6.11.1\mingw_64\bin;" + $env:PATH

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 -DCMAKE_CXX_COMPILER=g++ `
      -DSNAPCAP_FETCH_YAMLCPP=ON
Get-Process SnapCap -ErrorAction SilentlyContinue | Stop-Process -Force   # unlock the exe
cmake --build build
ctest --test-dir build --output-on-failure
```

The `$env:PATH` assignment lasts only for the current PowerShell session — re-run it in each new
window.

- **`-DSNAPCAP_FETCH_YAMLCPP=ON`** forces building yaml-cpp from source (FetchContent) with the
  MinGW toolchain. Needed whenever an incompatible yaml-cpp (e.g. conda's MSVC build) is on `PATH`,
  where it poisons `find_package`. Needs network on first configure.
- **Kill any running `SnapCap.exe` before building** — it locks the output exe and the link step
  fails with "cannot open output file … Permission denied".
- **Run a single test:** `ctest --test-dir build -R tst_selection --output-on-failure`, or run the
  exe directly — `build\tst_<name>.exe` — with `C:\Qt\6.11.1\mingw_64\bin` and
  `C:\Qt\Tools\mingw1310_64\bin` on `PATH` for the Qt/MinGW DLLs.
- **Run the app:** `build\SnapCap.exe` (Qt DLLs, `lang/` and `res/` are copied beside it by a
  post-build step / an earlier `windeployqt`). It is a **tray app** — no window on launch; on
  Windows 11 the tray icon hides in the `^` overflow flyout. Press `F1` to snip.
- **Deploying Qt:** `windeployqt --release build\SnapCap.exe` keeps Qt dynamic and replaceable for
  LGPLv3. SnapCap ships as a **portable ZIP, no installer**: the deployed folder is the product,
  which is also what makes the Qt DLLs replaceable by a file copy.
- **Re-run `windeployqt` whenever the Qt *installation* changes** — a module added through the Qt
  Maintenance Tool, or a Qt version bump. `windeployqt` copies whatever exists **at the moment it
  runs**: a snapshot, not a live link, and `cmake --build` never refreshes it. Installing a module
  changes `C:\Qt\…`, **not** `build\`, so the app keeps loading the plugin set beside the exe and
  behaves as if the module were still missing. The symptom is silent and easy to misread as an app
  bug — e.g. installing **Qt Image Formats** and still seeing no TIFF in the Save dialog, because
  `build\imageformats\` lacks `qtiff.dll`. Nothing here is image-specific: `platforms\`, `styles\`,
  `tls\` and the Qt DLLs all go stale the same way.

### Building a release

`cmake --build build --target package` produces `build/package/SnapCap-<version>-win64.zip` via
`install()` rules + CPack. For the artifact you actually **publish**, configure with
**`-DSNAPCAP_RELEASE=ON`** first: it turns the LGPLv3 source-URL checks into hard errors instead of
warnings (test ZIPs for clean-VM runs build fine without it). The flag persists in the CMake cache,
so set it back to `OFF` afterwards.

- The staging tree is a **whitelist**, which is what keeps `config.ini`, `splog.txt`, `history/`,
  `session/` and the `tst_*.exe` out of a release.
- An `install(CODE)` re-runs `windeployqt` against the staged exe every time, so packaging does not
  suffer the snapshot hazard above — though the dev `build/` tree still does.
- `eng.traineddata` is downloaded at **configure** time so OCR ships with English. Because that one
  file is redistributed, it has its own tessdata_fast (Apache-2.0) entry in
  `THIRD_PARTY_NOTICES.txt`.
- The publishing order and the source-hosting duties are in `Licence-Compliance-Plan.md` §4.

## i18n

- **Every UI string is a key, never a literal in code.** Strings live in editable
  `lang/<locale>.yaml`; code calls `I18n::instance().t("key")` or the `TR()` macro. The `I18n`
  singleton loads `lang/*.yaml` at runtime, auto-discovers locales (a new file is a new language,
  no recompile), falls back locale → `en` → raw key, and emits `localeChanged()` so widgets
  re-translate live.
- **`lang/en.yaml` is the authoritative locale.** A change that adds or changes a UI string edits
  `en.yaml` only; the maintainer updates the other locales. Missing keys fall back to English at
  runtime, so the app keeps working in between.
- **Consistency gate** — run on every change touching UI strings:

  ```sh
  python tools/i18n_check.py
  ```

  It fails if any `TR("…")`/`.t("…")` key used in `app/ ui/ core/` is missing from `lang/en.yaml`,
  or if locales have mismatched key sets. It skips `test/` (fixture keys) and `build/`. Reports of
  missing keys in the *non-English* locales are expected after an `en.yaml` change — that list is
  the translation to-do.

## Architecture (the parts that span files)

**Layered, dependencies point inward only:** `app/ → ui/ → core/ → platform/platform.h`
(interfaces). `core/` is platform- and widget-agnostic (so it is headless-unit-testable); it never
includes `ui/` or `platform/win/`. Only `app/` knows the concrete backend exists (it calls
`platform::createPlatform()`). See `Architecture-FileMap.md` for the per-file map.

- **`snapcap_core` (CMake static lib)** holds all of `core/` and is linked by **both** the
  `SnapCap` app target and every `tst_*` test. This split is what lets tests exercise logic without
  the GUI/Win32 layers. New core logic goes here.
- **The Action/Command layer (`core/action/`) is the spine for global input.** Every user action is
  an `ActionId`; global hotkeys and the tray menu dispatch through `ActionRegistry`. `KeyContext`
  (Global/Snip/Pin/Edit) disambiguates context-local keys (e.g. `Ctrl+A` = fullscreen while
  snipping vs. select-all on a pin). **In-overlay and in-pin keys are not dispatched through it**:
  they are hardcoded in `SnipOverlay::onKey` / `PinWindow::keyPressEvent`, and the Snip/Edit keymap
  entries only supply the labels of the snip key-tips panel — which is why only global hotkeys are
  remappable. Default bindings live in `res/keymap.default.yaml` (mirrors `Features.md` §4);
  `config.ini` stores only user **overrides** (`hotkey/<action>`). `KeySequenceCodec` parses portable strings ("Ctrl+Shift+S")
  into a neutral `KeySequence{Qt::KeyboardModifiers, Qt::Key}`; the Qt::Key → Win32 VK mapping
  lives in `platform/win/WinGlobalHotkey`.
- **The platform abstraction (`platform/platform.h`)** declares `IScreenCapture`, `IGlobalHotkey`,
  `IElementDetect`, `IAutostart` (launch at sign-in: an HKCU `Run` value, or a highest-privilege
  Scheduled Task for the elevated form), `IClipboard` (always places the base image;
  `ImageClipboardOptions` adds the legacy flat-DIB / `CF_HDROP` temp-file formats behind the
  Output → Clipboard prefs), `IKeyboardFilter`, `IQuickDragGesture`, `IWheelWatch`, `IOcr`,
  `IBarcodeDecode`, `IRecorder`, `IAudioSink`, `IMediaPlayer`, `IPinSurface` and `ITray`.
  `createPlatform()` returns a `Platform` bundle of
  `capture`/`hotkeys`/`detect`/`autostart`/`clipboard`/`keyFilter`/`quickDrag`/`wheel`/`ocr`/`barcode`/`recorder`
  — `autostart` and every member after `clipboard` may be null (no backend on this OS, or the optional dependency
  was not built), and callers must treat null as "feature off". `IPinSurface` is created per window
  via `createPinSurface(WId)`, `IAudioSink`/`IMediaPlayer` via their own factories, and `ITray` by
  the app shell. The OS-specific backend is `platform/win/*` (Win32); the library-backed ones are
  **OS-neutral** and live beside it — `platform/ocr` (Tesseract), `platform/barcode` (ZXing),
  `platform/media` (FFmpeg playback/trim). A new OS means implementing the `win/` interfaces plus a
  `createPlatform()` — no `core/`/`ui/` changes. New Win32 backend files are gated behind
  `if(WIN32)` in CMake and link the system libs they need (`user32`, `gdi32`, …).
- **Snip capture (`ui/SnipOverlay` + `ui/SnipScreenView`)** is multi-monitor: `SnipOverlay` is a
  `QObject` controller that freezes every screen and shows one `SnipScreenView` per monitor,
  coordinating a single global `core/SelectionModel` (so a drag spans monitors). The result is
  stitched by the pure `core/ScreenComposite::compositeSelection`. All capture/selection math is in
  `core/` (testable); the views are thin render + input forwarders.
- **The snip pipeline works in capture (native physical) px, *not* Qt logical coords** — this is
  the mixed-DPI fix, and the easiest thing to get wrong when touching snip code. `core/ScreenLayout`
  (built from `platform::monitorLayout()` / `WinMonitorTable`) is the model: `captureBounds()` is
  the `SelectionModel`'s bounds, `coveredCaptureRegion()` keeps real gaps gap-aware, and
  `localLogicalToCapture()` / `captureRectToLocalLogical()` convert at the view boundary (mapping
  both exclusive edges so fractional-DPI seams cannot go off by one). Annotation *geometry* is
  capture px too, while pen/font/redact **sizes** are pre-scaled from logical into capture px at
  creation by `SnipOverlay::captureStyle(anchor)` — so a stroke keeps its on-screen size and
  `AnnotationDocument`/shape `paint()` code needs no DPI awareness. Tests: `tst_screen_layout`,
  `tst_screen_composite`.

## Conventions

- **Tests:** QtTest, one `test/tst_<thing>.cpp` per unit, registered in the `foreach` test loop in
  `CMakeLists.txt`. Pure logic in `core/` gets real unit tests; GUI/platform pieces are verified
  manually (each stage in `Implementation-Plan.md` has a manual checklist). `core` exceptions to
  "no GUI" are deliberate and cross-platform (e.g. `QImage` in `ImageExport`).
  - **`tst_render_bench` is built but deliberately NOT `add_test`'d** — don't "fix" it by adding it
    to the loop. It is a perf benchmark: it asserts nothing about wall-clock (that would be flaky
    across machines) and its 4K cases take ~8 s. Run it by hand: `build\tst_render_bench.exe`. It
    is also the **only** place the shipped GPU blur is exercised — `tst_render` is
    `QTEST_GUILESS_MAIN`, so a GL context cannot exist there — which is why its
    `shippedGpuBlurMatchesCpu` case asserts GPU output matches the CPU box blur.
- **Settings are portable:** `config.ini` (and `splog.txt`) live **next to `SnapCap.exe`** by
  default; a `config.path` pointer file next to the exe records a user-relocated config path
  (`Settings::resolveConfigPath()`), and the Preferences "Config path" field moves it via
  `Settings::relocate()`. It falls back to `%LOCALAPPDATA%\SnapCap\SnapCap\` (Qt
  `AppConfigLocation`) only when the exe dir is not writable. Group names mirror the Preferences
  tabs.
- **Design docs stay in lockstep with code.** After implementing a stage or feature, update the
  matching `claude-plan/*.md` status and the file's row in `Architecture-FileMap.md`.
- **The user guide stays in lockstep with behaviour.** A change that adds or removes any usage — a
  hotkey, click/drag gesture, menu action, or behaviour — updates `docs/Usage.md` to match.
- **Preferences settings are recorded with their consumer.** When you add, rename or implement a
  Preferences setting, record it and **its consumer** in the `ui/PreferencesDialog` row of
  `Architecture-FileMap.md` and the matching stage in `Implementation-Plan.md`; that row and
  `ui/PreferencesDialog.cpp` must stay in lockstep. Every page is currently fully wired, so a
  control shipped with **no consumer** must say so explicitly at that row. (The old
  `Deferred-Settings.md` ledger existed for exactly that case and was retired once nothing was left
  deferred — don't recreate it.)
- **Everything in `Features.md` is in scope.** SnapCap has no paid tier, so no feature is held back
  as a premium one.

## Pitfalls — load-bearing and easy to undo

- **Config writability is a real probe.** When the program folder is ACL-restricted (e.g.
  `C:\Program Files`), `QFileInfo::isWritable()` returns `true` anyway — Qt does not consult NTFS
  ACLs by default — so `core/Settings.cpp` never took the `%LOCALAPPDATA%` fallback and the app
  silently persisted **nothing**. The check is now `dirAcceptsWrites()`, a create-and-delete probe.
  **Do not revert it to `isWritable()`.**
- **Autostart removal needs elevation too.** The UAC relaunch is offered on autostart *removal*,
  not just creation, and `app/Application`'s startup reconcile runs when elevated too (without
  that, the relaunch would remove nothing). The elevated form's Scheduled Task is a deliberate,
  disclosed exception to the no-registry-writes rule: it is the only way global hotkeys reach admin
  windows.
- **Licence documents: keep the `.txt` extensions and the filenames in agreement.**
  `LICENSE.txt`, `LICENSE.LGPLv3.txt`, `LICENSE.GPLv3.txt`, `LICENSE.Apache-2.0.txt` and
  `THIRD_PARTY_NOTICES.txt` ship at the repo root *and* beside the exe; Preferences → About has
  buttons opening the LGPLv3, GPLv3 and notices files. An extensionless `LICENSE.*`
  makes Windows show its "how do you want to open this file?" picker instead of the text viewer.
  The filenames must agree across `ui/PreferencesDialog.cpp`'s `kDocs` table, the `CMakeLists.txt`
  POST_BUILD copy, and the files themselves — nothing fails the build if they drift, the buttons
  just go silent.
- **GPU blur.** Blur goes through `render::blur()`, which dispatches to `core/render/GpuBlur`
  (offscreen GL) under `general/gpu_acceleration` = `auto` (default) | `cpu`, falling back to
  `render::boxBlur` on any failure. **Both backends run the same 3× box passes**, so output is
  identical everywhere (measured max per-channel delta 0–1) and the `test/golden/blur*.png`
  goldens cover what users see. `mosaic` is
  CPU-only by measurement (it was slower on the GPU). Before editing this area:
  - `setBlurMode()` **defaults to Cpu**, because `QTEST_GUILESS_MAIN` suites reach the blur path
    and a GL context needs a `QGuiApplication`.
  - The probe **must** keep rejecting software GL by its `GL_RENDERER` string: `windeployqt` ships
    Mesa llvmpipe, so context creation *succeeds* with no usable GPU.
  - `boxPass` **rounds** rather than truncates; truncation caused a 4-level systematic darkening
    that only the GPU comparison exposed.
- **LGPLv3 corresponding source.** `SNAPCAP_QT_SOURCE_URL` and `SNAPCAP_FFMPEG_SOURCE_URL` point at
  Releases in the public repo **`smolsquirrel936/SnapCap-thirdparty-src`** (`qt-6.11.1` and
  `ffmpeg-8c9502e9b0`). Full record: `Licence-Compliance-Plan.md`.
  - **`SNAPCAP_FFMPEG_SOURCE_URL` is a `CACHE STRING`** — editing the `set()` default does *not*
    update an existing build tree. Pass `-D…` or delete the cache entry. Qt's is a plain variable.
  - **A Qt kit bump is manual.** Nothing checks that the mirrored Qt matches the Qt linked (About
    reports Qt from the runtime `qVersion()`). Bumping means publishing a new `qt-<version>` release
    and editing the URL by hand.
  - **That repo must outlive every ZIP that points at it.** Making it private or deleting it breaks
    compliance retroactively for binaries already shipped. Archiving is safe.
  - **Rebuilding FFmpeg invalidates the published source bundle, and nothing warns you** — the gate
    matches the URL's *shape*, not its contents. So does **editing `tools/build_ffmpeg.{sh,ps1}`,
    even a comment**: both are release assets. Re-run `tools/fetch_ffmpeg_source.ps1`, then replace
    the changed assets, before shipping. The bundle's `README.md` is *generated* by that script from
    a here-string, so a new dependency must be added there as well as to `THIRD_PARTY_NOTICES.txt`,
    or regeneration silently changes nothing.
