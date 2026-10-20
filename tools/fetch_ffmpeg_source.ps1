# ---------------------------------------------------------------------------
# SnapCap — assemble the LGPL Corresponding Source bundle for the shipped FFmpeg.
#
# Shipping the FFmpeg DLLs means conveying FFmpeg, so LGPLv3 §4(d)(0) requires
# conveying its Corresponding Source. GPLv3 defines that to include "the scripts used
# to control compilation and installation" — so a source tarball ALONE is not enough;
# the build script and the exact configure line go with it.
#
# This exists so the bundle is reproducible rather than a remembered ritual: run it,
# upload the output folder's files as a GitHub Release, and point
# SNAPCAP_FFMPEG_SOURCE_URL at that release.
#
#   powershell -ExecutionPolicy Bypass -File tools\fetch_ffmpeg_source.ps1
#
# See claude-plan/Licence-Compliance-Plan.md §4 for the full procedure, including the
# ordering rule: the source must be published BEFORE (or with) the first ZIP that
# carries the DLLs.
# ---------------------------------------------------------------------------
param(
    # Default: a SnapCap-thirdparty-src folder beside this repo's checkout.
    [string]$OutDir = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "SnapCap-thirdparty-src")
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

# Must match tools/build_ffmpeg.sh. Read from it rather than duplicated, so the two
# cannot drift into shipping one version and documenting another.
$buildSh = Get-Content (Join-Path $PSScriptRoot "build_ffmpeg.sh") -Raw
function Get-Pin([string]$name) {
    if ($buildSh -match "PIN_$name=`"([0-9a-f]{40})`"") { return $Matches[1] }
    throw "Could not read PIN_$name from build_ffmpeg.sh"
}
$ffmpegCommit = Get-Pin "FFMPEG"

Write-Host "SnapCap — FFmpeg Corresponding Source bundle" -ForegroundColor Cyan
Write-Host "  FFmpeg commit: $ffmpegCommit"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$staging = Join-Path $OutDir "ffmpeg-$($ffmpegCommit.Substring(0,10))"
New-Item -ItemType Directory -Force -Path $staging | Out-Null

# --- 1. FFmpeg source at the EXACT commit ----------------------------------
# Not "FFmpeg 8.1": the obligation is the source that produced these binaries.
$srcArchive = Join-Path $staging "ffmpeg-$($ffmpegCommit.Substring(0,10))-source.tar.gz"
if (-not (Test-Path $srcArchive)) {
    Write-Host "  downloading FFmpeg source..."
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri "https://github.com/FFmpeg/FFmpeg/archive/$ffmpegCommit.tar.gz" `
                      -OutFile $srcArchive
}
Write-Host ("  ffmpeg source      {0,7:N1} MB" -f ((Get-Item $srcArchive).Length / 1MB))

# --- 2. The build scripts (part of Corresponding Source, per GPLv3) --------
Copy-Item (Join-Path $PSScriptRoot "build_ffmpeg.sh")  $staging -Force
Copy-Item (Join-Path $PSScriptRoot "build_ffmpeg.ps1") $staging -Force

# --- 3. The exact configure line, taken from the built artefact ------------
$conf = Join-Path $repoRoot "third_party\ffmpeg\configure-line.txt"
if (Test-Path $conf) {
    Copy-Item $conf $staging -Force
} else {
    Write-Warning "third_party\ffmpeg\configure-line.txt not found — run tools\build_ffmpeg.ps1 first."
}

# --- 4. A README mapping DLLs to source -----------------------------------
$version = (Select-String -Path (Join-Path $repoRoot "CMakeLists.txt") -Pattern 'VERSION (\d+\.\d+\.\d+)' |
            Select-Object -First 1).Matches[0].Groups[1].Value
@"
FFmpeg Corresponding Source for SnapCap $version
================================================

SnapCap ships these FFmpeg libraries:

    avcodec-62.dll   avdevice-62.dll   avfilter-11.dll   avformat-62.dll
    avutil-60.dll    swresample-6.dll  swscale-9.dll

They are licensed under the GNU Lesser General Public License. Because the build is
configured with --enable-version3, the applicable terms are LGPL version 3 or later.

This directory is the Corresponding Source for those binaries:

  ffmpeg-*-source.tar.gz   FFmpeg at commit $ffmpegCommit
                           (version string: n8.1.2-31-g8c9502e9b0)
  build_ffmpeg.sh          the script that configured and built it
  build_ffmpeg.ps1         its entry point / prerequisite checks
  configure-line.txt       the exact configure line, as recorded by the build

GPLv3 defines Corresponding Source to include "the scripts used to control
compilation and installation", which is why the build scripts are here and not just
the tarball.

This is NOT a general-purpose FFmpeg build. It enables only the components SnapCap
calls. The following libraries are statically linked into the DLLs; all are
permissive, and their pinned commits are recorded in build_ffmpeg.sh:

    libopenh264 (BSD-2)   kvazaar (BSD-3)   SVT-AV1 (BSD-3 + AOM patent licence)
    libvpx (BSD-3)        Opus (BSD-3)      libvpl (MIT)
    dav1d (BSD-2)         AV1 decoding -- FFmpeg's native av1 decoder has no software path

NVIDIA NVENC and AMD AMF are used via header-only SDKs; no NVIDIA or AMD library is
linked or redistributed.

To rebuild: install MSYS2 with make/nasm/pkg-config/git/diffutils, plus meson
(pip install meson) and ninja (ships with the Qt tools) which dav1d is built with,
then run build_ffmpeg.ps1. It checks every prerequisite up front, then fetches each
dependency at its pinned commit and reproduces this build.
"@ | Out-File -FilePath (Join-Path $staging "README.md") -Encoding utf8

Write-Host "`nBundle ready: $staging" -ForegroundColor Green
Get-ChildItem $staging | ForEach-Object { "  {0,-42} {1,8:N1} KB" -f $_.Name, ($_.Length / 1KB) }
Write-Host @"

Next steps (see claude-plan/Licence-Compliance-Plan.md section 4):
  1. Create (or reuse) the public repo  SnapCap-thirdparty-src
  2. Publish these files as a Release tagged  ffmpeg-$($ffmpegCommit.Substring(0,10))
  3. Set SNAPCAP_FFMPEG_SOURCE_URL to that release URL and rebuild
  4. Publish the source BEFORE or WITH the ZIP that carries the DLLs
"@ -ForegroundColor Cyan
