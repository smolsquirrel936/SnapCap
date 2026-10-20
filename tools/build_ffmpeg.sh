#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# SnapCap — stripped FFmpeg superbuild (claude-plan/Licence-Compliance-Plan.md)
#
# Builds ONLY what SnapCap calls, instead of shipping a general-purpose FFmpeg.
# BtbN's prebuilt DLLs enable ~50 external libraries (72 build scripts) and weigh
# 136 MB; this build needs five, and measured 12 MB before they were added.
#
# That matters twice over:
#   * size    — the portable ZIP stays small;
#   * licence — every library statically linked into the DLLs is CONVEYED, so its
#               licence terms apply. Dropping ~45 unused ones (over a dozen of them
#               copyleft) leaves five dependencies that are ALL permissive/BSD-family,
#               so the corresponding-source obligation reduces to FFmpeg itself.
#               See claude-plan/Licence-Compliance-Plan.md §1.1.
#
# Pins match BtbN's scripts.d at commit 8c736b2 — i.e. the exact library versions
# the Phase 0-4 verification ran against, so this build is not a fresh unknown.
#
# Invoked by tools/build_ffmpeg.ps1; run that, not this.
# ---------------------------------------------------------------------------
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/third_party/ffmpeg-build"
PREFIX="$WORK/deps-prefix"          # static dependencies land here
OUT="$ROOT/third_party/ffmpeg"      # final DLLs + headers + import libs
JOBS="${JOBS:-8}"

# The Qt MinGW kit SnapCap links against — deliberately NOT MSYS2's own toolchain.
# bash is invoked as a LOGIN shell, so MSYS2's profile rebuilds PATH and the caller's
# Windows PATH does not survive. Every Windows-side tool therefore has to be named
# here. Ninja ships with Qt; meson does not (pip install meson), so the wrapper
# resolves it and passes its directory in SNAPCAP_MESON_DIR rather than this script
# guessing a per-user Python path.
export PATH="/c/Qt/Tools/mingw1310_64/bin:/c/Qt/Tools/CMake_64/bin:/c/Qt/Tools/Ninja:${SNAPCAP_MESON_DIR:+$SNAPCAP_MESON_DIR:}$PATH"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"

# repo|commit|name — pinned to BtbN scripts.d @ 8c736b2
# Full 40-char SHAs throughout: `git fetch --depth 1 origin <sha>` rejects abbreviated
# ones. 8c9502e9b0 is how the version string spells it (n8.1.2-31-g8c9502e9b0).
PIN_FFMPEG_REPO="https://github.com/FFmpeg/FFmpeg.git";                  PIN_FFMPEG="8c9502e9b048e21e1cae96477e338ac0635645ba"
PIN_KVAZAAR_REPO="https://github.com/ultravideo/kvazaar.git";            PIN_KVAZAAR="d6815293f34a094e26ba6c50b8644660ddc13e09"
PIN_SVTAV1_REPO="https://gitlab.com/AOMediaCodec/SVT-AV1.git";           PIN_SVTAV1="d3c4cb3947a8bfed0aa5a2be996b37bb117fa1bd"
PIN_OPENH264_REPO="https://github.com/cisco/openh264.git";              PIN_OPENH264="a8e04adb69c79757da014007d4694684a64c7b74"
PIN_LIBVPX_REPO="https://chromium.googlesource.com/webm/libvpx";        PIN_LIBVPX="8592391cdb3ef142c56d835788d71d6d4de36a63"
PIN_OPUS_REPO="https://github.com/xiph/opus.git";                       PIN_OPUS="3da9f7a6db1c05c3996cb363a9d1931a978bf1be"
# dav1d is the AV1 DECODER, and it is not optional despite AV1 being encoded by
# SVT-AV1. FFmpeg's native `av1` decoder has no software path at all -- av1dec.c
# returns AVERROR(ENOSYS) unless a hwaccel is attached ("the av1 decoder doesn't
# support native decode", its own comment) -- so without dav1d every AV1 recording is
# unplayable in SnapCap's own preview. Found 2026-08-04 by measuring send rc -40.
# 54706fc6 is the commit tag 1.5.4 points at (BSD-2). Pinned by SHA like every other
# dependency here: a tag can be moved, a commit cannot.
PIN_DAV1D_REPO="https://code.videolan.org/videolan/dav1d.git";           PIN_DAV1D="54706fc6bc0cdecab7e9593974a4039cc038fca7"
# Hardware-encoder SDKs. NVENC and AMF are HEADER-ONLY (no library is linked or
# shipped — the driver provides the runtime), which is why they cost ~nothing in size
# or licence surface. libvpl is a real library, but it is only the Intel QSV
# dispatcher. Omitting these is what silently demoted every codec to software.
# n12.2.72.0. NOT BtbN's first pin: theirs is 13.1.15.0.0, whose declared NVENC API is
# newer than current drivers report supporting, so every *_nvenc encoder COMPILES but
# refuses to open at runtime (silently demoting recordings to software). BtbN gets away
# with it by pinning three header versions, one per FFmpeg branch. FFmpeg 8.1 accepts
# >= 12.1.14.0; 12.2 is the widely-compatible choice.
PIN_FFNVCODEC_REPO="https://github.com/FFmpeg/nv-codec-headers.git";     PIN_FFNVCODEC="c69278340ab1d5559c7d7bf0edf615dc33ddbba7"
PIN_AMF_REPO="https://github.com/GPUOpen-LibrariesAndSDKs/AMF.git";      PIN_AMF="6ec029531e356102aafe1e236cfd0ddf739939da"
PIN_LIBVPL_REPO="https://github.com/intel/libvpl.git";                   PIN_LIBVPL="d77f9195cf495b937631607333288fd917ae8939"

mkdir -p "$WORK" "$PREFIX"
LOG="$WORK/build.log"
: > "$LOG"

say()  { echo "==> $*"; }
fail() { echo "!! FAILED: $*"; echo "   last 30 lines of $LOG:"; tail -30 "$LOG"; exit 1; }

# Shallow-fetch one commit. Cheaper than a full clone (FFmpeg alone is ~200 MB of
# history) and pins exactly what we build.
fetch() {
    local dir="$1" repo="$2" commit="$3"
    # Guard on a sentinel written only AFTER a successful checkout. Testing for .git
    # is not enough: `git init` creates it before the fetch runs, so a failed fetch
    # would leave a bare .git that then looks like a completed download forever.
    if [ -f "$WORK/$dir/.snapcap-fetched" ]; then say "$dir: already fetched"; return 0; fi
    say "$dir: fetching $commit"
    rm -rf "${WORK:?}/$dir"
    mkdir -p "$WORK/$dir"
    ( cd "$WORK/$dir" \
      && git init -q \
      && git remote add origin "$repo" \
      && git fetch -q --depth 1 origin "$commit" \
      && git checkout -q FETCH_HEAD ) >>"$LOG" 2>&1 || fail "$dir fetch"
    echo "$commit" > "$WORK/$dir/.snapcap-fetched"
}

# ---------------------------------------------------------------- dependencies

build_opus() {
    [ -f "$PREFIX/lib/libopus.a" ] && { say "opus: already built"; return 0; }
    fetch opus "$PIN_OPUS_REPO" "$PIN_OPUS"
    say "opus: building"
    ( cd "$WORK/opus" && cmake -G "Unix Makefiles" -B bld \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF \
      && cmake --build bld -j"$JOBS" && cmake --install bld ) >>"$LOG" 2>&1 || fail "opus"
}

build_kvazaar() {
    [ -f "$PREFIX/lib/libkvazaar.a" ] && { say "kvazaar: already built"; return 0; }
    fetch kvazaar "$PIN_KVAZAAR_REPO" "$PIN_KVAZAAR"
    # NOT LGPL, despite what the plan said before this build was run: kvazaar
    # relicensed to 3-clause BSD at 2.0, and this pin is 2.3.2. Its own LICENSE and
    # kvazaar.pc both say so. See claude-plan/Licence-Compliance-Plan.md §1.1.
    say "kvazaar: building   (BSD-3)"
    ( cd "$WORK/kvazaar" && cmake -G "Unix Makefiles" -B bld \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
      && cmake --build bld -j"$JOBS" && cmake --install bld ) >>"$LOG" 2>&1 || fail "kvazaar"
}

build_svtav1() {
    [ -f "$PREFIX/lib/libSvtAv1Enc.a" ] && { say "svt-av1: already built"; return 0; }
    fetch svtav1 "$PIN_SVTAV1_REPO" "$PIN_SVTAV1"
    say "svt-av1: building"
    ( cd "$WORK/svtav1" && cmake -G "Unix Makefiles" -B bld \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_APPS=OFF -DBUILD_TESTING=OFF \
      && cmake --build bld -j"$JOBS" && cmake --install bld ) >>"$LOG" 2>&1 || fail "svt-av1"
}

build_openh264() {
    [ -f "$PREFIX/lib/libopenh264.a" ] && { say "openh264: already built"; return 0; }
    fetch openh264 "$PIN_OPENH264_REPO" "$PIN_OPENH264"
    say "openh264: building"
    # Makefile build; the meson path would drag in meson+ninja for no gain here.
    ( cd "$WORK/openh264" \
      && make -j"$JOBS" OS=mingw_nt ARCH=x86_64 PREFIX="$PREFIX" install-static ) \
      >>"$LOG" 2>&1 || fail "openh264"
}

build_libvpx() {
    [ -f "$PREFIX/lib/libvpx.a" ] && { say "libvpx: already built"; return 0; }
    fetch libvpx "$PIN_LIBVPX_REPO" "$PIN_LIBVPX"
    say "libvpx: building"
    # VP9 only: VP8 is dead weight here, and SnapCap never offers it.
    ( cd "$WORK/libvpx" && mkdir -p bld && cd bld \
      && ../configure --target=x86_64-win64-gcc --prefix="$PREFIX" \
           --disable-shared --enable-static --disable-examples --disable-tools \
           --disable-docs --disable-unit-tests --enable-vp9 --disable-vp8 \
           --enable-vp9-encoder --enable-vp9-decoder --enable-pic \
      && make -j"$JOBS" && make install ) >>"$LOG" 2>&1 || fail "libvpx"
}

build_ffnvcodec() {
    [ -f "$PREFIX/lib/pkgconfig/ffnvcodec.pc" ] && { say "ffnvcodec: already installed"; return 0; }
    fetch ffnvcodec "$PIN_FFNVCODEC_REPO" "$PIN_FFNVCODEC"
    say "ffnvcodec: installing headers (NVENC)"
    ( cd "$WORK/ffnvcodec" && make PREFIX="$PREFIX" install ) >>"$LOG" 2>&1 || fail "ffnvcodec"
}

build_amf() {
    [ -f "$PREFIX/include/AMF/core/Version.h" ] && { say "amf: already installed"; return 0; }
    fetch amf "$PIN_AMF_REPO" "$PIN_AMF"
    say "amf: installing headers"
    mkdir -p "$PREFIX/include/AMF"
    cp -r "$WORK/amf/amf/public/include/." "$PREFIX/include/AMF/" >>"$LOG" 2>&1 || fail "amf"
}

build_libvpl() {
    [ -f "$PREFIX/lib/libvpl.a" ] && { say "libvpl: already built"; return 0; }
    fetch libvpl "$PIN_LIBVPL_REPO" "$PIN_LIBVPL"
    say "libvpl: building (Intel QSV dispatcher)"
    ( cd "$WORK/libvpl" && cmake -G "Unix Makefiles" -B bld \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF \
        -DINSTALL_EXAMPLE_CODE=OFF \
      && cmake --build bld -j"$JOBS" && cmake --install bld ) >>"$LOG" 2>&1 || fail "libvpl"
}

build_dav1d() {
    [ -f "$PREFIX/lib/libdav1d.a" ] && { say "dav1d: already built"; return 0; }
    fetch dav1d "$PIN_DAV1D_REPO" "$PIN_DAV1D"
    say "dav1d: building (AV1 decoder)"
    command -v meson >/dev/null 2>&1 || fail "dav1d: meson not found (pip install meson)"
    command -v ninja >/dev/null 2>&1 || fail "dav1d: ninja not found (C:\\Qt\\Tools\\Ninja)"
    # The only meson dependency in the tree. meson must be pointed at the SAME Qt MinGW
    # toolchain everything else uses, or dav1d links against MSYS2's runtime and FFmpeg
    # fails at link time; a cross file is how meson is told that.
    cat > "$WORK/dav1d-cross.txt" <<EOF
[binaries]
c = 'gcc'
cpp = 'g++'
ar = 'ar'
strip = 'strip'
pkg-config = 'pkg-config'
nasm = 'nasm'

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF
    ( cd "$WORK/dav1d" && rm -rf bld \
      && meson setup bld --cross-file "$WORK/dav1d-cross.txt" \
           --prefix="$PREFIX" --buildtype=release --default-library=static \
           -Denable_tools=false -Denable_tests=false \
      && ninja -C bld -j"$JOBS" && ninja -C bld install ) >>"$LOG" 2>&1 || fail "dav1d"
}

# ---------------------------------------------------------------------- FFmpeg

build_ffmpeg() {
    fetch ffmpeg "$PIN_FFMPEG_REPO" "$PIN_FFMPEG"
    say "ffmpeg: configuring"
    rm -rf "${OUT:?}"

    # Every component below traces to a call site in SnapCap. --disable-autodetect
    # is load-bearing: without it configure would silently absorb whatever happens to
    # be installed on the build machine, which is exactly the licence-surface problem
    # this build exists to remove.
    #
    # --disable-iconv is needed ON TOP of it: configure still probes libc iconv under
    # --disable-autodetect, and the Qt MinGW kit ships libiconv.a, so without this flag
    # GNU libiconv (LGPL-2.1+) was silently linked into avcodec. Its only use is
    # subtitle charset recoding, which no component enabled here reaches.
    #
    # The `gif` PARSER is not optional: the GIF demuxer hands out fixed 1024-byte
    # slices of the file and never splits frames, so without the parser the preview
    # player decodes garbage. Like every omission here, that fails at RUNTIME only.
    #
    # Target declared explicitly because Qt's gcc is already a MinGW compiler; without
    # it configure refuses the MSYS environment and demands MSYS2's own toolchain.
    #
    # -DKVZ_STATIC_LIB is required: kvazaar.h declares its symbols __declspec(dllimport)
    # unless told the library is static, so linking the static .a without it fails with
    # "undefined reference to __imp_kvz_api_get". Any Windows library shipping one header
    # for both static and DLL use has this trap.
    #
    # --extra-libs=-lstdc++ is for libvpl (Intel QSV), which is written in C++ while
    # FFmpeg's configure link test uses the C compiler — without it the test fails on
    # `operator new[]`, `__cxa_begin_catch` and the type-info vtables. No new shipped
    # file: libstdc++-6.dll already accompanies the app for Qt.
    ( cd "$WORK/ffmpeg" && ./configure \
        --prefix="$OUT" \
        --target-os=mingw32 --arch=x86_64 \
        --disable-everything --disable-autodetect --disable-iconv --disable-doc --disable-programs \
        --disable-network --disable-static --enable-shared \
        --enable-version3 \
        --enable-w32threads --enable-d3d11va --enable-mediafoundation         --enable-nvenc --enable-ffnvcodec --enable-amf --enable-libvpl \
        --pkg-config-flags=--static \
        --extra-cflags="-I$PREFIX/include -DKVZ_STATIC_LIB" \
        --extra-ldflags="-L$PREFIX/lib" \
        --extra-libs="-lstdc++" \
        --enable-libopus --enable-libkvazaar --enable-libsvtav1 \
        --enable-libvpx --enable-libopenh264 --enable-libdav1d \
        --enable-encoder=aac,gif,libopus,libkvazaar,libsvtav1,libvpx_vp9,libopenh264         --enable-encoder=h264_nvenc,hevc_nvenc,av1_nvenc         --enable-encoder=h264_qsv,hevc_qsv,av1_qsv,vp9_qsv         --enable-encoder=h264_amf,hevc_amf,av1_amf         --enable-encoder=h264_mf,hevc_mf,av1_mf \
        --enable-decoder=h264,hevc,av1,libdav1d,vp9,aac,opus,gif,bmp,rawvideo \
        --enable-muxer=mp4,matroska,webm,gif \
        --enable-demuxer=mov,matroska,gif \
        --enable-parser=h264,hevc,av1,vp9,aac,opus,gif \
        --enable-filter=ddagrab,hwdownload,format,fps,scale,split,palettegen,paletteuse,buffer,buffersink,null \
        --enable-indev=gdigrab \
        --enable-protocol=file \
        --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,extract_extradata \
      ) >>"$LOG" 2>&1 || fail "ffmpeg configure"

    say "ffmpeg: building"
    ( cd "$WORK/ffmpeg" && make -j"$JOBS" && make install ) >>"$LOG" 2>&1 || fail "ffmpeg build"

    # The exact configure line is part of the Corresponding Source (GPLv3 defines it to
    # include "the scripts used to control compilation"), so record it next to the output.
    # config.log's FIRST line is the verbatim invocation ("# ./configure --prefix=...").
    # An earlier version grepped for "  configuration:" and silently wrote an EMPTY
    # file — which would have shipped a source bundle missing a required part.
    head -1 "$WORK/ffmpeg/ffbuild/config.log" > "$OUT/configure-line.txt" 2>/dev/null || true
    if [ ! -s "$OUT/configure-line.txt" ]; then
        echo "!! WARNING: configure-line.txt is empty — the source bundle needs it"
    fi
    cp "$WORK/ffmpeg/ffbuild/config.log" "$OUT/config.log" 2>/dev/null || true
}

build_ffnvcodec
build_amf
build_libvpl
build_opus
build_kvazaar
build_svtav1
build_openh264
build_libvpx
build_dav1d
build_ffmpeg

say "done — DLLs in $OUT/bin"
ls -l "$OUT"/bin/*.dll 2>/dev/null | awk '{printf "  %-26s %7.1f MB\n", $9, $5/1048576}'
echo "  TOTAL: $(du -cm "$OUT"/bin/*.dll 2>/dev/null | tail -1 | cut -f1) MB"
