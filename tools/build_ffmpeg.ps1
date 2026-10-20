# ---------------------------------------------------------------------------
# SnapCap — stripped FFmpeg superbuild (entry point).
#
# Builds FFmpeg + the five libraries SnapCap actually calls, instead of shipping
# a general-purpose build. See claude-plan/Licence-Compliance-Plan.md §1.1 for why, and
# tools/build_ffmpeg.sh for the actual steps.
#
# Mirrors tools/build_tesseract.ps1: a one-time dependency build, run by hand, whose
# output lands in third_party/ and is picked up by CMake's find logic.
#
#   powershell -ExecutionPolicy Bypass -File tools\build_ffmpeg.ps1
#
# Prerequisites (checked below):
#   * MSYS2 with make, nasm, pkg-config, git, diffutils
#     -> pacman -S --needed make nasm pkgconf diffutils git
#   * the Qt MinGW kit at C:\Qt\Tools\mingw1310_64 (the SAME compiler SnapCap links
#     with — deliberately not MSYS2's own toolchain)
#   * CMake from the Qt tools directory
# ---------------------------------------------------------------------------
param(
    [string]$Msys2Root = "C:\msys64",
    [int]$Jobs = 8
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

Write-Host "SnapCap FFmpeg superbuild" -ForegroundColor Cyan

# --- prerequisites ---------------------------------------------------------
$bash = Join-Path $Msys2Root "usr\bin\bash.exe"
if (-not (Test-Path $bash)) {
    throw "MSYS2 not found at $Msys2Root. Install it (https://www.msys2.org) or pass -Msys2Root."
}
$missing = @()
foreach ($t in @("make", "nasm", "pkg-config", "git")) {
    if (-not (Test-Path (Join-Path $Msys2Root "usr\bin\$t.exe"))) { $missing += $t }
}
if ($missing.Count -gt 0) {
    throw ("MSYS2 is missing: {0}`nInstall with:  pacman -S --needed make nasm pkgconf diffutils git" -f ($missing -join ", "))
}
if (-not (Test-Path "C:\Qt\Tools\mingw1310_64\bin\gcc.exe")) {
    throw "Qt MinGW toolchain not found at C:\Qt\Tools\mingw1310_64 — the build must use the same kit SnapCap links against."
}
if (-not (Test-Path "C:\Qt\Tools\CMake_64\bin\cmake.exe")) {
    throw "CMake not found at C:\Qt\Tools\CMake_64 (ships with the Qt tools)."
}
if (-not (Test-Path "C:\Qt\Tools\Ninja\ninja.exe")) {
    throw "Ninja not found at C:\Qt\Tools\Ninja (ships with the Qt tools) - dav1d builds with meson+ninja."
}
# meson is the only dependency not supplied by MSYS2 or the Qt tools, and it is needed
# for dav1d (the AV1 decoder). Resolve it here rather than in the shell script: pip
# installs it under a per-user Python path that no script should hardcode.
$mesonCmd = Get-Command meson -ErrorAction SilentlyContinue
if (-not $mesonCmd) {
    throw "meson not found on PATH. Install it with:  pip install meson`nIt is required to build dav1d; without dav1d, AV1 recordings cannot be decoded for preview."
}
$mesonDir = (Split-Path -Parent $mesonCmd.Source) -replace '\\', '/' -replace '^([A-Za-z]):', '/$1'
$env:SNAPCAP_MESON_DIR = $mesonDir.ToLower().Substring(0, 2) + $mesonDir.Substring(2)
Write-Host "  prerequisites OK (meson: $($mesonCmd.Source))" -ForegroundColor Green

# --- run the build ---------------------------------------------------------
# CHERE_INVOKING keeps bash in the current directory instead of jumping to $HOME.
$env:MSYSTEM = "MSYS"
$env:CHERE_INVOKING = "1"
$env:JOBS = "$Jobs"

$script = ($PSScriptRoot -replace '\\', '/') -replace '^([A-Za-z]):', '/$1'
$script = $script.ToLower().Substring(0, 2) + $script.Substring(2) + "/build_ffmpeg.sh"

Write-Host "  running $script (this takes a while; log: third_party\ffmpeg-build\build.log)"
& $bash -lc "bash '$script'"
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg superbuild failed — see third_party\ffmpeg-build\build.log"
}

Write-Host "`nDone. Reconfigure SnapCap so CMake picks up the new build:" -ForegroundColor Cyan
Write-Host "  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ``"
Write-Host "        -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 -DCMAKE_CXX_COMPILER=g++ ``"
Write-Host "        -DSNAPCAP_FETCH_YAMLCPP=ON -DSNAPCAP_FETCH_FFMPEG=OFF"
