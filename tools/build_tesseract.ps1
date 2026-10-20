<#
.SYNOPSIS
  Build a static Leptonica + Tesseract with SnapCap's own MinGW kit and install
  them into third_party/tesseract, so CMake's find_package(Tesseract) enables the
  OCR backend.

.DESCRIPTION
  Run ONCE (re-run to rebuild). Builds with the SAME compiler SnapCap uses
  (C:\Qt\Tools\mingw1310_64), so there is no libstdc++/ABI mismatch and the
  resulting SnapCap.exe statically links OCR (no extra DLLs — good for portable).

  We feed Tesseract raw RGBA pixels, so Leptonica is built minimal (no zlib/png/
  jpeg/tiff/...); Tesseract is LSTM-only (legacy engine + training tools off).

  After it finishes, reconfigure SnapCap normally — the OCR message will read
  "Tesseract backend enabled".
#>
[CmdletBinding()]
param(
    [string]$Qt    = 'C:\Qt\6.11.1\mingw_64',
    [string]$MinGW = 'C:\Qt\Tools\mingw1310_64\bin',
    [string]$Ninja = 'C:\Qt\Tools\Ninja',
    [string]$CMake = 'C:\Qt\Tools\CMake_64\bin',
    [string]$LeptonicaTag = '1.85.0',
    [string]$TesseractTag = '5.5.0'
)

$ErrorActionPreference = 'Stop'
$env:PATH = "$MinGW;$Ninja;$CMake;$Qt\bin;$env:PATH"

$Repo   = Split-Path -Parent $PSScriptRoot
$Src    = Join-Path $Repo 'third_party\src'
$Prefix = Join-Path $Repo 'third_party\tesseract'
New-Item -ItemType Directory -Force -Path $Src | Out-Null

function Invoke-Checked {
    param([string]$What, [scriptblock]$Block)
    Write-Host ">> $What" -ForegroundColor Cyan
    & $Block
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit $LASTEXITCODE)" }
}

function Clone-IfNeeded($url, $tag, $dir) {
    if (Test-Path (Join-Path $dir '.git')) { Write-Host ">> reuse $dir"; return }
    Invoke-Checked "clone $url @ $tag" { git clone --depth 1 --branch $tag $url $dir }
}

# Shared configure flags (semicolon list stays one arg to CMake).
$Common = @(
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_C_COMPILER=gcc',
    '-DCMAKE_CXX_COMPILER=g++',
    "-DCMAKE_PREFIX_PATH=$Qt;$Prefix",
    "-DCMAKE_INSTALL_PREFIX=$Prefix",
    '-DBUILD_SHARED_LIBS=OFF'
)

# --- Leptonica (minimal, static) -------------------------------------------
$leptSrc   = Join-Path $Src 'leptonica'
$leptBuild = Join-Path $leptSrc 'build'
Clone-IfNeeded 'https://github.com/DanBloomberg/leptonica.git' $LeptonicaTag $leptSrc
Invoke-Checked 'configure leptonica' {
    cmake -S $leptSrc -B $leptBuild @Common `
        -DSW_BUILD=OFF -DBUILD_PROG=OFF `
        -DENABLE_ZLIB=OFF -DENABLE_PNG=OFF -DENABLE_JPEG=OFF -DENABLE_TIFF=OFF `
        -DENABLE_GIF=OFF -DENABLE_WEBP=OFF -DENABLE_OPENJPEG=OFF
}
Invoke-Checked 'build + install leptonica' { cmake --build $leptBuild --target install }

# --- Tesseract (static, LSTM-only) -----------------------------------------
$tessSrc   = Join-Path $Src 'tesseract'
$tessBuild = Join-Path $tessSrc 'build'
Clone-IfNeeded 'https://github.com/tesseract-ocr/tesseract.git' $TesseractTag $tessSrc
Invoke-Checked 'configure tesseract' {
    cmake -S $tessSrc -B $tessBuild @Common `
        -DSW_BUILD=OFF `
        -DBUILD_TRAINING_TOOLS=OFF -DBUILD_TESTS=OFF `
        -DDISABLE_CURL=ON -DDISABLE_ARCHIVE=ON -DGRAPHICS_DISABLED=ON `
        -DDISABLED_LEGACY_ENGINE=ON -DINSTALL_CONFIGS=OFF
}
Invoke-Checked 'build + install tesseract' { cmake --build $tessBuild --target install }

Write-Host ''
Write-Host "OK — installed to $Prefix" -ForegroundColor Green
Write-Host 'Reconfigure SnapCap; find_package(Tesseract) will enable the OCR backend.'
