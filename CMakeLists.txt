cmake_minimum_required(VERSION 3.21)

project(SnapCap
    VERSION 0.1.0
    DESCRIPTION "Multifunctional screen-capture app, Windows-first / portable"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)        # Qt meta-object compiler
set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(SNAPCAP_BUILD_TESTS "Build unit tests" ON)

# --- Dependencies -----------------------------------------------------------
# Qt is linked DYNAMICALLY (LGPLv3 compliance — see claude-plan/Tech-Stack-Plan.md).
# Only LGPLv3-available modules are used here. Test is dev-only.
# Network: QLocalServer/QLocalSocket for the single-instance guard.
# OpenGL backs core/render/GpuBlur (Stage 7). It was dev-only while the GPU path
# lived solely in tst_render_bench's reference implementation; the shipped blur now
# uses it, so it is linked into snapcap_core and Qt6OpenGL.dll ships with the app.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network OpenGL)
if(SNAPCAP_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

# yaml-cpp powers the editable, out-of-code i18n string files (lang/*.yaml) and the
# default keymap. Prefer a system/vcpkg install; otherwise fetch & build from source.
#
# SNAPCAP_FETCH_YAMLCPP forces the source build — required when an incompatible
# yaml-cpp is on the system (e.g. an MSVC/conda build that find_package would pick
# up but can't link against this MinGW toolchain).
option(SNAPCAP_FETCH_YAMLCPP "Build yaml-cpp from source instead of find_package" OFF)
if(NOT SNAPCAP_FETCH_YAMLCPP)
    find_package(yaml-cpp CONFIG QUIET)
endif()
if(NOT yaml-cpp_FOUND)
    message(STATUS "yaml-cpp: building from source (FetchContent).")
    include(FetchContent)
    FetchContent_Declare(yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        0.8.0)
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
endif()

# --- ZXing-C++ (barcode decoding, optional) ---------------------------------
# Powers the snip code hint (platform/barcode/ZxingBarcodeDecode) — every symbology ZXing
# reads, 2D and 1D, not QR alone. Apache-2.0 — compatible with the app's LGPLv3
# obligations (see claude-plan/Tech-Stack-Plan.md).
# Optional in exactly the same sense as Tesseract: without it Platform::barcode stays
# null and the code hint never appears, so the pure core/barcode logic is always built
# + tested and the build never hard-depends on the network.
option(SNAPCAP_FETCH_ZXING "Build ZXing-C++ from source (FetchContent)" ON)
set(SNAPCAP_HAVE_ZXING OFF)
if(NOT SNAPCAP_FETCH_ZXING)
    find_package(ZXing CONFIG QUIET)
endif()
if(NOT ZXing_FOUND AND SNAPCAP_FETCH_ZXING)
    message(STATUS "Barcode: building ZXing-C++ from source (FetchContent).")
    include(FetchContent)
    # GIT_SHALLOW skips ~2000 commits of upstream history (253 MB of the 353 MB
    # clone) — we only ever build one tag. It requires a branch/tag NAME, not a
    # raw commit SHA, so GIT_TAG must stay a tag if this stays on.
    FetchContent_Declare(ZXing
        GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
        GIT_TAG        v2.2.1
        GIT_SHALLOW    TRUE)
    # NOTE: v2.2.1 spells these BUILD_* (the ZXING_* names arrived in 2.3), so they
    # must match the pinned tag above. Reader only — the app decodes codes, it
    # never generates them — and no examples, whose CMakeLists would otherwise
    # register ZXingReaderTest/ZXingWriterTest into *our* ctest suite.
    set(BUILD_WRITERS  OFF CACHE BOOL "" FORCE)
    set(BUILD_READERS  ON  CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_UNIT_TESTS     OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ZXing)
endif()
if(TARGET ZXing::ZXing)
    set(SNAPCAP_HAVE_ZXING ON)
    message(STATUS "Barcode: ZXing-C++ backend enabled.")
else()
    message(STATUS "Barcode: no ZXing backend — code hint disabled at runtime.")
endif()

# --- Tesseract OCR (optional) -----------------------------------------------
# Powers the OCR feature (platform/ocr/TesseractOcr). Apache-2.0 + Leptonica
# (BSD-2) — compatible with the app's LGPLv3 obligations. Optional: the app builds
# and runs without it (Platform::ocr stays null and the OCR button no-ops), so the
# pure core/ocr logic is always built + tested.
#
# The dependency is built ONCE with this same MinGW kit via tools/build_tesseract.ps1,
# which installs a static Leptonica + Tesseract into third_party/tesseract. We then
# find_package() it from there — same compiler = no ABI mismatch, and a self-contained
# exe (static). A system/vcpkg install already on CMAKE_PREFIX_PATH is also honoured.
set(SNAPCAP_TESSERACT_ROOT "${CMAKE_SOURCE_DIR}/third_party/tesseract"
    CACHE PATH "Prefix where tools/build_tesseract.ps1 installed Tesseract+Leptonica")
set(SNAPCAP_HAVE_TESSERACT OFF)
set(SNAPCAP_TESSERACT_LIBS)
list(APPEND CMAKE_PREFIX_PATH "${SNAPCAP_TESSERACT_ROOT}")
find_package(Leptonica CONFIG QUIET)     # Tesseract's config find_dependency()s this
find_package(Tesseract CONFIG QUIET)
if(Tesseract_FOUND)
    set(SNAPCAP_HAVE_TESSERACT ON)
    set(SNAPCAP_TESSERACT_LIBS Tesseract::libtesseract)
    message(STATUS "OCR: Tesseract backend enabled (${Tesseract_DIR}).")
else()
    message(STATUS "OCR: no Tesseract backend — run tools/build_tesseract.ps1 to enable "
                   "(OCR disabled at runtime).")
endif()

# --- FFmpeg (screen recording, optional) ------------------------------------
# Powers the recorder + preview player.
# Optional in the same sense as Tesseract/ZXing: without it Platform::recorder stays
# null and the toolbar's record button is disabled, so the pure core/recording logic
# is always built + tested and the build never hard-depends on the network.
#
# BUILT FROM SOURCE by tools/build_ffmpeg.ps1, NOT downloaded. That is a compliance
# decision as much as a size one (Licence-Compliance-Plan.md §1.1): a general-purpose FFmpeg
# statically links ~50 external libraries, over a dozen of them copyleft, and shipping
# it would mean conveying — and hosting corresponding source for — all of them. Our
# build enables only what SnapCap calls: 25 MB instead of 136 MB, seven dependencies,
# all permissive, leaving FFmpeg itself as the only source-hosting obligation.
set(SNAPCAP_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/third_party/ffmpeg"
    CACHE PATH "Prefix of the FFmpeg build produced by tools/build_ffmpeg.ps1")
set(SNAPCAP_HAVE_FFMPEG OFF)

# The seven libraries the recorder + player link. avdevice carries gdigrab (the
# fallback grabber) and avfilter carries ddagrab, so neither is optional despite the
# code never touching their other formats.
set(SNAPCAP_FFMPEG_LIBNAMES avformat avcodec avfilter avdevice avutil swscale swresample)
if(EXISTS "${SNAPCAP_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    set(SNAPCAP_HAVE_FFMPEG ON)
    set(SNAPCAP_FFMPEG_LIBS)
    foreach(_l IN LISTS SNAPCAP_FFMPEG_LIBNAMES)
        if(NOT EXISTS "${SNAPCAP_FFMPEG_ROOT}/lib/lib${_l}.dll.a")
            set(SNAPCAP_HAVE_FFMPEG OFF)
        endif()
        list(APPEND SNAPCAP_FFMPEG_LIBS "${SNAPCAP_FFMPEG_ROOT}/lib/lib${_l}.dll.a")
    endforeach()
endif()
if(SNAPCAP_HAVE_FFMPEG)
    message(STATUS "Recording: FFmpeg backend enabled (${SNAPCAP_FFMPEG_ROOT}).")
else()
    message(STATUS "Recording: no FFmpeg backend — run tools/build_ffmpeg.ps1 to enable "
                   "(recording + preview disabled at runtime).")
endif()

# --- core library (platform-agnostic; shared by app + tests) ----------------
# Keeping core/ in its own library lets the unit tests exercise the logic without
# the GUI or Win32 backend (see claude-plan/Architecture-FileMap.md).
add_library(snapcap_core STATIC
    core/i18n/I18n.h               core/i18n/I18n.cpp
    core/action/ActionId.h
    core/action/KeySequence.h
    core/action/KeySequenceCodec.h core/action/KeySequenceCodec.cpp
    core/action/ActionRegistry.h   core/action/ActionRegistry.cpp
    core/Settings.h                core/Settings.cpp
    core/Logging.h                 core/Logging.cpp
    core/SelectionModel.h          core/SelectionModel.cpp
    core/ClickSlop.h
    core/ImageExport.h             core/ImageExport.cpp
    core/FilenameTemplate.h        core/FilenameTemplate.cpp
    core/ManualSave.h              core/ManualSave.cpp
    core/FontNameTable.h           core/FontNameTable.cpp
    core/ScreenComposite.h         core/ScreenComposite.cpp
    core/ScreenLayout.h            core/ScreenLayout.cpp
    core/ColorPicker.h             core/ColorPicker.cpp
    core/RoundedCorners.h          core/RoundedCorners.cpp
    core/annotation/Annotation.h           core/annotation/Annotation.cpp
    core/annotation/AnnotationDocument.h   core/annotation/AnnotationDocument.cpp
    core/annotation/ShapeConstraints.h     core/annotation/ShapeConstraints.cpp
    core/render/Raster.h           core/render/Raster.cpp
    core/render/GpuBlur.h          core/render/GpuBlur.cpp
    core/ClipboardContent.h        core/ClipboardContent.cpp
    core/PinModel.h                core/PinModel.cpp
    core/PinManager.h              core/PinManager.cpp
    core/GroupRegistry.h           core/GroupRegistry.cpp
    core/History.h                 core/History.cpp
    core/AreaHistory.h             core/AreaHistory.cpp
    core/HistoryStrip.h            core/HistoryStrip.cpp
    core/SessionStore.h            core/SessionStore.cpp
    core/TipsPlacement.h           core/TipsPlacement.cpp
    core/ocr/OcrSelection.h        core/ocr/OcrSelection.cpp
    core/scroll/ScrollStitcher.h   core/scroll/ScrollStitcher.cpp
    core/recording/RecordingSpec.h core/recording/RecordingSpec.cpp
    core/barcode/BarcodeHint.h               core/barcode/BarcodeHint.cpp
    core/codec/TgaEncoder.h        core/codec/TgaEncoder.cpp
    core/codec/Quantize.h          core/codec/Quantize.cpp
    core/codec/GifEncoder.h        core/codec/GifEncoder.cpp
    core/codec/AnimatedImage.h     core/codec/AnimatedImage.cpp
    core/codec/PdfEncoder.h        core/codec/PdfEncoder.cpp
    platform/platform.h
)
target_include_directories(snapcap_core PUBLIC ${CMAKE_SOURCE_DIR})

# Shown on Preferences -> About. PUBLIC so both SnapCap and the prefs_demo harness
# (which compiles PreferencesDialog.cpp too) see them.
#
# SNAPCAP_QT_SOURCE_URL is the LGPLv3 s4(d)(0) Corresponding-Source link for the Qt
# DLLs we ship. GPLv3 s6(d) does permit that source to sit on a third-party server,
# so the old upstream download.qt.io link was not itself a violation. What s6(d) does
# NOT do is move the obligation: "you remain obligated to ensure that it is available
# for as long as needed". The Qt Company retires old point releases from archive/ on
# its own schedule and will not tell us, so we mirror it ourselves.
#
# Resolved 2026-08-04: qt-everywhere-src-6.11.1.tar.xz (1,017,723,080 bytes, upstream
# MD5 25d4d1dd74c92b978f164e8f20805985) is published at the release below. This
# retires the ${Qt6_VERSION}-derived URL that stood here before -- a release page
# carries no Qt version to derive from, so the guard it provided is gone with it:
#
#   BUMPING THE Qt KIT NOW MEANS publishing a new qt-<version> release and editing
#   this URL by hand. Nothing checks that the version here matches the Qt actually
#   linked (About reports Qt from runtime qVersion(), so the two can disagree
#   silently). See claude-plan/Licence-Compliance-Plan.md section 4.
set(SNAPCAP_QT_SOURCE_URL
    "https://github.com/smolsquirrel936/SnapCap-thirdparty-src/releases/tag/qt-6.11.1")
# LGPLv3 s4(d)(0) again, this time for the FFmpeg we BUILD and ship. Unlike Qt's, this
# one can never point upstream: we compile our own configure line, so the Corresponding
# Source is OUR drop -- FFmpeg at the pinned commit plus the build script, which GPLv3
# explicitly counts as part of it ("the scripts used to control compilation").
# Published 2026-08-04; the five assets there are the complete Corresponding Source.
# CACHED: editing this default does NOT change an existing build tree -- either
# reconfigure with -DSNAPCAP_FFMPEG_SOURCE_URL=... or delete the entry from
# build/CMakeCache.txt.
set(SNAPCAP_FFMPEG_SOURCE_URL
    "https://github.com/smolsquirrel936/SnapCap-thirdparty-src/releases/tag/ffmpeg-8c9502e9b0"
    CACHE STRING "Release page hosting the Corresponding Source for the shipped FFmpeg")
target_compile_definitions(snapcap_core PUBLIC
    SNAPCAP_VERSION="${PROJECT_VERSION}"
    SNAPCAP_QT_SOURCE_URL="${SNAPCAP_QT_SOURCE_URL}"
    SNAPCAP_FFMPEG_SOURCE_URL="${SNAPCAP_FFMPEG_SOURCE_URL}")
target_link_libraries(snapcap_core PUBLIC
    Qt6::Core
    Qt6::Gui          # QImage/QClipboard for ImageExport (cross-platform)
    Qt6::OpenGL       # core/render/GpuBlur -- offscreen GL blur (Stage 7, LGPLv3-available)
    yaml-cpp::yaml-cpp
)

# --- Application ------------------------------------------------------------
set(SNAPCAP_PLATFORM_SOURCES)
if(WIN32)
    list(APPEND SNAPCAP_PLATFORM_SOURCES
        platform/win/WinGlobalHotkey.h  platform/win/WinGlobalHotkey.cpp
        platform/win/WinMonitorTable.h  platform/win/WinMonitorTable.cpp
        platform/win/WinScreenCapture.h platform/win/WinScreenCapture.cpp
        platform/win/WinElementDetect.h platform/win/WinElementDetect.cpp
        platform/win/WinPinSurface.h    platform/win/WinPinSurface.cpp
        platform/win/WinAutostart.h     platform/win/WinAutostart.cpp
        platform/win/WinClipboard.h     platform/win/WinClipboard.cpp
        platform/win/WinKeyboardFilter.h platform/win/WinKeyboardFilter.cpp
        platform/win/WinQuickDragGesture.h platform/win/WinQuickDragGesture.cpp
        platform/win/WinWheelWatch.h       platform/win/WinWheelWatch.cpp
        platform/win/WinPlatform.cpp)
endif()

add_executable(SnapCap WIN32
    app/main.cpp
    app/Application.h app/Application.cpp
    ui/TrayIcon.h       ui/TrayIcon.cpp
    ui/SnipOverlay.h    ui/SnipOverlay.cpp
    ui/SnipScreenView.h ui/SnipScreenView.cpp
    ui/PaintBar.h       ui/PaintBar.cpp
    ui/OcrBar.h         ui/OcrBar.cpp
    ui/OcrTextLayer.h   ui/OcrTextLayer.cpp
    ui/RecordBar.h      ui/RecordBar.cpp
    ui/ScrollCaptureBar.h  ui/ScrollCaptureBar.cpp
    ui/ScrollStripWindow.h ui/ScrollStripWindow.cpp
    ui/TextBoxEditor.h  ui/TextBoxEditor.cpp
    ui/ColorDialog.h    ui/ColorDialog.cpp
    ui/FontPickerDialog.h ui/FontPickerDialog.cpp
    ui/PinWindow.h      ui/PinWindow.cpp
    ui/PinEditController.h ui/PinEditController.cpp
    ui/GroupManagerDialog.h ui/GroupManagerDialog.cpp
    ui/PreferencesDialog.h ui/PreferencesDialog.cpp
    ui/HotkeyEdit.h     ui/HotkeyEdit.cpp
    ui/KeyBindingsDialog.h ui/KeyBindingsDialog.cpp
    ui/NamingRulesDialog.h ui/NamingRulesDialog.cpp
    ui/CustomSnipDialog.h ui/CustomSnipDialog.cpp
    ui/PrefsStyle.h     ui/PrefsStyle.cpp
    ui/WheelGuard.h     ui/WheelGuard.cpp
    ui/SaveFeedback.h   ui/SaveFeedback.cpp
    ${SNAPCAP_PLATFORM_SOURCES}
)
target_link_libraries(SnapCap PRIVATE
    snapcap_core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)
if(WIN32)
    # user32/gdi32: RegisterHotKey, BitBlt. ole32/oleaut32: UI Automation COM.
    # dwmapi: DWM cloaked / extended-frame-bounds queries for element detection.
    # advapi32: Run-key registry + process-token elevation. shell32: ShellExecute (runas).
    # shcore: GetDpiForMonitor (size the captured cursor to the OS pointer size).
    # oleacc: MSAA IAccessible hit testing (element-detection diagnostics).
    target_link_libraries(SnapCap PRIVATE user32 gdi32 ole32 oleaut32 dwmapi advapi32 shell32 shcore oleacc)
endif()

# OCR backend (only when the Tesseract dependency is available). The backend is
# OS-neutral, so it is compiled into the app (not the WIN32-gated block); the
# compile-def gates the createPlatform() wiring in WinPlatform.cpp.
if(SNAPCAP_HAVE_TESSERACT)
    target_sources(SnapCap PRIVATE
        platform/ocr/TesseractOcr.h platform/ocr/TesseractOcr.cpp)
    # The imported Tesseract target doesn't reliably export its include dir, so set it
    # on the backend source directly (from the config's *_INCLUDE_DIRS vars). Scoping
    # it to this one source keeps the rest of the target's objects untouched.
    set_source_files_properties(platform/ocr/TesseractOcr.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${Tesseract_INCLUDE_DIRS};${Leptonica_INCLUDE_DIRS}")
    target_link_libraries(SnapCap PRIVATE ${SNAPCAP_TESSERACT_LIBS})
    target_compile_definitions(SnapCap PRIVATE SNAPCAP_HAVE_TESSERACT)
endif()

# Barcode backend (only when the ZXing dependency is available). Like the OCR backend
# it is OS-neutral, so it is compiled into the app rather than the WIN32 block;
# the compile-def gates the createPlatform() wiring in WinPlatform.cpp.
if(SNAPCAP_HAVE_ZXING)
    target_sources(SnapCap PRIVATE
        platform/barcode/ZxingBarcodeDecode.h platform/barcode/ZxingBarcodeDecode.cpp)
    target_link_libraries(SnapCap PRIVATE ZXing::ZXing)
    target_compile_definitions(SnapCap PRIVATE SNAPCAP_HAVE_ZXING)
endif()

# Recording backend (only when the FFmpeg dependency is available). Windows-only:
# ddagrab is DXGI Desktop Duplication and the audio capture is WASAPI, so unlike the
# OCR/barcode backends there is nothing OS-neutral to compile elsewhere. The
# compile-def gates the createPlatform() wiring in WinPlatform.cpp and the Record
# action in app/Application.cpp.
if(WIN32 AND SNAPCAP_HAVE_FFMPEG)
    target_sources(SnapCap PRIVATE
        platform/win/WinRecorder.h platform/win/WinRecorder.cpp
        platform/win/WinAudioCapture.h platform/win/WinAudioCapture.cpp
        platform/win/WinAudioRender.h platform/win/WinAudioRender.cpp
        platform/media/FfmpegPlayer.h platform/media/FfmpegPlayer.cpp
        platform/media/FfmpegTrim.cpp
        ui/RecordPreviewDialog.h ui/RecordPreviewDialog.cpp)
    target_include_directories(SnapCap PRIVATE "${SNAPCAP_FFMPEG_ROOT}/include")
    target_link_libraries(SnapCap PRIVATE ${SNAPCAP_FFMPEG_LIBS})
    target_compile_definitions(SnapCap PRIVATE SNAPCAP_HAVE_FFMPEG)

    # Dev tree only: put the FFmpeg DLLs beside the exe so build\SnapCap.exe runs.
    # The PACKAGE gets them from the install() rule instead (Phase 5).
    set(_snapcap_ff_dlls)
    file(GLOB _snapcap_ff_dlls "${SNAPCAP_FFMPEG_ROOT}/bin/av*.dll"
                               "${SNAPCAP_FFMPEG_ROOT}/bin/sw*.dll")
    add_custom_command(TARGET SnapCap POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_snapcap_ff_dlls} "$<TARGET_FILE_DIR:SnapCap>"
        COMMENT "Copying FFmpeg runtime DLLs to output directory")
endif()

# --- Resources: copy editable language files + default keymap next to the exe -
# Translators edit lang/*.yaml; keymap.default.yaml is the default bindings. The
# running app loads them from <exe-dir>/lang and <exe-dir>/res.
add_custom_command(TARGET SnapCap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/lang" "$<TARGET_FILE_DIR:SnapCap>/lang"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/res" "$<TARGET_FILE_DIR:SnapCap>/res"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/icons" "$<TARGET_FILE_DIR:SnapCap>/icons"
    # LGPLv3 + third-party attribution must accompany the binary, so they sit beside
    # the exe -- which is what Preferences -> About's three buttons open.
    #
    # CAVEAT (dev tree only): POST_BUILD runs only when SnapCap.exe actually relinks, so
    # editing lang/, res/, icons/ or these documents alone does NOT refresh build/.
    # Re-copy by hand (`cmake -E copy_if_different <file> build/`) or force a relink.
    # The PACKAGE is unaffected -- install() reads the source tree at package time.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/LICENSE.txt"
            "${CMAKE_SOURCE_DIR}/LICENSE.LGPLv3.txt" "${CMAKE_SOURCE_DIR}/LICENSE.GPLv3.txt"
            "${CMAKE_SOURCE_DIR}/LICENSE.Apache-2.0.txt"
            "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt" "$<TARGET_FILE_DIR:SnapCap>"
    COMMENT "Copying lang/, res/, icons/ and the license files to output directory")

# --- Dev demos (off by default) ---------------------------------------------
# Throwaway visual harnesses for standalone widgets not yet wired into the app.
option(SNAPCAP_BUILD_DEMOS "Build standalone widget demo executables" OFF)
if(SNAPCAP_BUILD_DEMOS)
    add_executable(colordialog_demo
        tools/colordialog_demo.cpp
        ui/PrefsStyle.h ui/PrefsStyle.cpp
        ui/WheelGuard.h ui/WheelGuard.cpp
        ui/ColorDialog.h ui/ColorDialog.cpp)
    target_link_libraries(colordialog_demo PRIVATE snapcap_core Qt6::Gui Qt6::Widgets)

    add_executable(prefs_demo
        tools/prefs_demo.cpp
        ui/PreferencesDialog.h ui/PreferencesDialog.cpp
        ui/HotkeyEdit.h ui/HotkeyEdit.cpp
        ui/KeyBindingsDialog.h ui/KeyBindingsDialog.cpp
        ui/NamingRulesDialog.h ui/NamingRulesDialog.cpp
        ui/CustomSnipDialog.h ui/CustomSnipDialog.cpp
        ui/PrefsStyle.h ui/PrefsStyle.cpp
        ui/WheelGuard.h ui/WheelGuard.cpp
        ui/FontPickerDialog.h ui/FontPickerDialog.cpp
        ui/OcrBar.h ui/OcrBar.cpp
        ui/ColorDialog.h ui/ColorDialog.cpp)
    target_link_libraries(prefs_demo PRIVATE snapcap_core Qt6::Gui Qt6::Widgets Qt6::Network)
endif()

# --- Tests ------------------------------------------------------------------
if(SNAPCAP_BUILD_TESTS)
    enable_testing()
    foreach(t IN ITEMS tst_recording_spec tst_i18n tst_keyseq_codec tst_action_registry tst_settings tst_selection tst_click_slop tst_image_export tst_filename_template tst_manual_save tst_font_name_table tst_screen_composite tst_screen_layout tst_color tst_annotation_document tst_annotation_golden tst_render tst_shifted_shapes tst_clipboard_content tst_pin_model tst_recover_ring tst_group_registry tst_session_store tst_history tst_area_history tst_history_strip tst_tips_placement tst_ocr_selection tst_barcode_hint tst_image_codecs tst_rounded_corners tst_scroll_stitcher)
        add_executable(${t} test/${t}.cpp)
        target_link_libraries(${t} PRIVATE snapcap_core Qt6::Test)
        # Lets a test read repo data files (e.g. res/keymap.default.yaml) that are
        # shipped next to the exe at runtime but not next to the test binary.
        target_compile_definitions(${t} PRIVATE SNAPCAP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        add_test(NAME ${t} COMMAND ${t})
    endforeach()

    # Stage 7 perf measurement for the CPU redaction path. Built, but deliberately
    # NOT add_test()'d: it asserts nothing about wall-clock (flaky across machines)
    # and its 4K cases take seconds. Run by hand: build\tst_render_bench.exe
    add_executable(tst_render_bench test/tst_render_bench.cpp)
    target_link_libraries(tst_render_bench PRIVATE snapcap_core Qt6::Test)
    target_link_libraries(tst_render_bench PRIVATE Qt6::OpenGL)
endif()

# --- Bundled OCR language: English ------------------------------------------
# SnapCap ships English only, in <exe>/tessdata/; further languages are
# fetched in-app via Preferences -> OCR. Downloaded at configure time from the same
# tessdata_fast source the in-app downloader uses (ui/PreferencesDialog.cpp), rather
# than committed, so a 4 MB binary stays out of version control.
#
# It lands directly in the build dir, which is both where the dev build's exe looks
# and what install() ships -- so `tessdata/` is populated the same way in both.
#
# NOTE: we redistribute this file, so it is attributed in THIRD_PARTY_NOTICES.txt
# (Apache-2.0). If this download is ever dropped, that entry must go too.
set(SNAPCAP_ENG_TRAINEDDATA "${CMAKE_BINARY_DIR}/tessdata/eng.traineddata")
if(NOT EXISTS "${SNAPCAP_ENG_TRAINEDDATA}")
    message(STATUS "OCR: downloading bundled English language pack (tessdata_fast)...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main/eng.traineddata"
        "${SNAPCAP_ENG_TRAINEDDATA}"
        SHOW_PROGRESS
        STATUS _snapcap_eng_status
        TLS_VERIFY ON)
    list(GET _snapcap_eng_status 0 _snapcap_eng_code)
    if(NOT _snapcap_eng_code EQUAL 0)
        # Not fatal: OCR degrades to "no languages until you download one" rather
        # than failing the build for everyone behind a proxy.
        file(REMOVE "${SNAPCAP_ENG_TRAINEDDATA}")     # don't leave a truncated file
        message(WARNING "OCR: could not download eng.traineddata (${_snapcap_eng_status}). "
                        "The build continues, but the package will ship no OCR language "
                        "and users must download one via Preferences -> OCR.")
    endif()
endif()

# --- Packaging: portable ZIP (Stage 7 Phase C) ------------------------------
# SnapCap ships as a PORTABLE ZIP -- no installer, by decision (2026-07-26). The
# deployed folder IS the product: unzip and run. That is also the cleanest way to
# meet the LGPLv3 relinking obligation, since every Qt DLL sits beside the exe and
# is replaceable with a file copy (MSIX was ruled out for the opposite reason -- its
# package integrity would prevent exactly that).
#
#   cmake --build build --target package      (or: cpack -G ZIP --config build/CPackConfig.cmake)
#
# The staging tree is built from these install() rules, NOT by zipping build/ --
# which matters, because build/ also accumulates test exes, config.ini, splog.txt,
# history/ and session/ that must never reach a user. Whitelisting via install() is
# what keeps those out, and keeping the file list here rather than in a Windows-only
# packaging script is what keeps a future macOS/Linux bundle cheap.
install(TARGETS SnapCap RUNTIME DESTINATION .)

# Runtime data the app loads from <exe-dir>. Mirrors the POST_BUILD copy above; that
# one serves the dev build, these serve the package.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/lang/"  DESTINATION lang)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/res/"   DESTINATION res)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/icons/" DESTINATION icons)

# LGPLv3 + attribution must accompany the binary (Phase A). LICENSE.LGPLv3.txt,
# LICENSE.GPLv3.txt and THIRD_PARTY_NOTICES.txt are also what Preferences ->
# About's three buttons open, so their names must not drift.
#
# LICENSE.Apache-2.0.txt has no About button: it is here because Apache-2.0
# section 4(a) requires giving recipients a COPY of the license (a URL is not a
# copy), and SnapCap redistributes three Apache-2.0 works -- Tesseract and
# ZXing-C++ (statically linked into the exe) and tessdata/eng.traineddata.
#
# LICENSE.txt is SnapCap's own (MIT) license. Its name is also what GitHub's
# license detection keys on: the third-party LICENSE.<name>.txt files score zero
# there, so this stays the only file it counts.
install(FILES
    "${CMAKE_SOURCE_DIR}/LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/LICENSE.LGPLv3.txt"
    "${CMAKE_SOURCE_DIR}/LICENSE.GPLv3.txt"
    "${CMAKE_SOURCE_DIR}/LICENSE.Apache-2.0.txt"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt"
    DESTINATION .)

# The bundled English OCR pack (see above). OPTIONAL so a failed/skipped download
# degrades to "no bundled language" instead of breaking `package` outright.
install(FILES "${SNAPCAP_ENG_TRAINEDDATA}" DESTINATION tessdata OPTIONAL)

# The FFmpeg runtime. WITHOUT THIS THE PACKAGED EXE DOES NOT START: SnapCap.exe carries
# load-time imports of all seven DLLs, so the Windows loader fails before main() rather
# than merely disabling recording. windeployqt does not cover them -- it only knows Qt.
# Our build produces no ffmpeg.exe/ffplay.exe, so there is nothing extra to exclude.
if(WIN32 AND SNAPCAP_HAVE_FFMPEG)
    file(GLOB SNAPCAP_FFMPEG_DLLS "${SNAPCAP_FFMPEG_ROOT}/bin/av*.dll"
                                  "${SNAPCAP_FFMPEG_ROOT}/bin/sw*.dll")
    install(FILES ${SNAPCAP_FFMPEG_DLLS} DESTINATION .)
endif()

if(WIN32)
    # windeployqt lives beside qmake in the Qt kit being built against, so derive it
    # from the imported target rather than trusting PATH (which may hold another Qt).
    if(NOT SNAPCAP_WINDEPLOYQT)
        get_target_property(_snapcap_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_snapcap_qt_bin "${_snapcap_qmake}" DIRECTORY)
        find_program(SNAPCAP_WINDEPLOYQT windeployqt HINTS "${_snapcap_qt_bin}")
    endif()

    if(SNAPCAP_WINDEPLOYQT)
        # Runs at INSTALL time, against the already-installed exe, so the Qt DLL and
        # plugin set is derived from the real binary's dependencies. This is what
        # makes the packaged tree self-contained -- and it removes the standing
        # "windeployqt is a snapshot" hazard for packaging: every `package` run
        # re-derives the set, so a newly linked Qt module (e.g. Qt6OpenGL.dll, added
        # by the Stage 7 GPU blur) can no longer be silently missing from the ZIP.
        # NOTE: no --no-compiler-runtime. This is a MinGW kit, so libgcc_s_seh-1.dll,
        # libstdc++-6.dll and libwinpthread-1.dll are genuine runtime dependencies
        # (and are attributed in THIRD_PARTY_NOTICES.txt).
        install(CODE "
            message(STATUS \"Running windeployqt on the staged SnapCap.exe...\")
            execute_process(
                COMMAND \"${SNAPCAP_WINDEPLOYQT}\" --release --verbose 0
                        \"\${CMAKE_INSTALL_PREFIX}/SnapCap.exe\"
                RESULT_VARIABLE _wdq_result
                OUTPUT_VARIABLE _wdq_out
                ERROR_VARIABLE  _wdq_out)
            if(NOT _wdq_result EQUAL 0)
                message(FATAL_ERROR \"windeployqt failed (\${_wdq_result}):\\n\${_wdq_out}\")
            endif()
        ")
    else()
        message(WARNING "windeployqt not found - `package` would produce a ZIP that "
                        "only runs where Qt is already on PATH. Set SNAPCAP_WINDEPLOYQT.")
    endif()
endif()

# LGPLv3 release gate. SNAPCAP_QT_SOURCE_URL is a placeholder while it points at
# download.qt.io: lawful under GPLv3 s6(d), but it leaves our availability
# obligation resting on a server we do not control (see the note at its
# definition). This check runs at INSTALL time, so it fires when a package is
# actually produced rather than on every dev configure.
#
# Warning by default, because the clean-VM checklist needs to build test ZIPs.
# -DSNAPCAP_RELEASE=ON promotes it to an error: that is the flag for building the
# artifact that gets published, and it must not be able to ship a placeholder.
option(SNAPCAP_RELEASE "Building a package for public release - fail if compliance placeholders remain" OFF)
if(SNAPCAP_RELEASE)
    set(_snapcap_src_level FATAL_ERROR)
else()
    set(_snapcap_src_level WARNING)
endif()

# The same obligation for the FFmpeg we BUILD and ship. Checked separately from Qt's so
# that resolving one cannot mask the other, and matched against a real release URL
# rather than a placeholder domain: the Corresponding Source here is our own source
# drop, so there is no upstream address that would ever be correct.
#
# Ordering matters and is easy to get wrong: the source must be published BEFORE (or
# with) the first ZIP carrying these DLLs. The obligation attaches at conveyance;
# uploading afterwards does not cure a release that already went out.
if(SNAPCAP_HAVE_FFMPEG AND NOT SNAPCAP_FFMPEG_SOURCE_URL MATCHES "/releases/(tag|download)/")
    install(CODE "
        message(${_snapcap_src_level}
            \"SNAPCAP_FFMPEG_SOURCE_URL does not point at a published release:\\n\"
            \"    ${SNAPCAP_FFMPEG_SOURCE_URL}\\n\"
            \"The shipped FFmpeg DLLs are built by tools/build_ffmpeg.ps1, so their \"
            \"Corresponding Source -- FFmpeg at the pinned commit, that script, and the \"
            \"configure line -- must be published first. See \"
            \"claude-plan/Licence-Compliance-Plan.md section 4. Configure with \"
            \"-DSNAPCAP_RELEASE=ON to make this an error.\")
    ")
endif()

if(SNAPCAP_QT_SOURCE_URL MATCHES "download\\.qt\\.io")
    set(_snapcap_qt_src_level ${_snapcap_src_level})
    install(CODE "
        message(${_snapcap_qt_src_level}
            \"SNAPCAP_QT_SOURCE_URL still points at upstream Qt:\\n\"
            \"    ${SNAPCAP_QT_SOURCE_URL}\\n\"
            \"Before publishing this ZIP, host qt-everywhere-src alongside it and \"
            \"repoint the URL at that release page -- see the Stage 7 release \"
            \"checklist in claude-plan/Implementation-Plan.md. Configure with \"
            \"-DSNAPCAP_RELEASE=ON to make this an error.\")
    ")
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "SnapCap")
set(CPACK_PACKAGE_VENDOR "SnapCap")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "SnapCap-${PROJECT_VERSION}-win64")
# Unzip yields ONE folder rather than spraying files into the download dir -- the
# folder is the portable install, and config.ini lands inside it next to the exe.
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SnapCap-${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.LGPLv3.txt")
set(CPACK_SOURCE_GENERATOR "")                # binary packages only
include(CPack)

# --- windeployqt in the DEV tree (build/) -----------------------------------
# The packaging path above re-derives everything, but `build/` is still populated by
# hand: `cmake --build` never refreshes it, and windeployqt copies whatever exists AT
# THE MOMENT IT RUNS -- a snapshot, not a live link.
#   windeployqt --release "$<TARGET_FILE:SnapCap>"
# Re-run it whenever the Qt INSTALLATION changes (new module, version bump) OR when
# SnapCap starts linking a new Qt module. Not image-specific: platforms/, styles/,
# tls/ and the Qt DLLs all go stale the same way.
#
# Save formats: TGA and GIF are written by core/codec/, so they need no plugin at all.
# TIFF is the one that does -- it ships in the optional Qt Image Formats module (Qt
# Maintenance Tool -> Qt 6.11.1 -> Additional Libraries). Installing it is NOT enough
# on its own: until windeployqt is re-run, build/imageformats/ still lacks qtiff.dll
# and the app behaves exactly as if the module were missing. Once the plugin is
# actually deployed, TIFF appears in the Save dialog with no code change, because
# ImageExport::supportedSaveFormats() probes at runtime.
