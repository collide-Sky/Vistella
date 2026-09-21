# SPDX-License-Identifier: MIT
#
# scripts/download_models.ps1
#
#   Downloads the InsightFace buffalo_l ONNX weights needed by the Liquify
#   face-aware tool. Verifies SHA-256 of each extracted file. Idempotent: skips
#   download + extract when files are already in place and hashes match.
#
#   Source: https://github.com/deepinsight/insightface/releases/tag/v0.7
#   See:   docs/MODELS.md
#
# Usage:
#   .\scripts\download_models.ps1                  # default: GitHub release URL
#   .\scripts\download_models.ps1 -Force          # re-download even if hashes match
#   .\scripts\download_models.ps1 -Source HuggingFace   # use the HF mirror instead
#

[CmdletBinding()]
param(
    [switch]$Force,
    [ValidateSet('GitHub', 'HuggingFace')]
    [string]$Source = 'GitHub'
)

$ErrorActionPreference = 'Stop'

# -- Source URLs ----------------------------------------------------------------
$URLS = @{
    'GitHub'      = 'https://github.com/deepinsight/insightface/releases/download/v0.7/buffalo_l.zip'
    'HuggingFace' = 'https://huggingface.co/Anyisam/buffalo_l/resolve/main/buffalo_l.zip'
}

# -- Expected SHA-256 of the two .onnx files inside buffalo_l.zip ----------------
# Keep in sync with docs/MODELS.md.
$Expected = @{
    'det_10g.onnx'  = '5838f7fe053675b1c7a08b633df49e7af5495cee0493c7dcf6697200b85b5b91'
    '2d106det.onnx' = 'f001b856447c413801ef5c42091ed0cd516fcd21f2d6b79635b1e733a7109dbf'
}

# -- Resolve destination under repo root ----------------------------------------
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot   = (Resolve-Path (Join-Path $ScriptDir '..')).Path
$LandmarkDir = Join-Path $RepoRoot 'third_party/landmark'

if (-not (Test-Path $LandmarkDir)) {
    New-Item -ItemType Directory -Path $LandmarkDir -Force | Out-Null
}

# -- Check existing files --------------------------------------------------------
$need = @()
foreach ($name in @('det_10g.onnx', '2d106det.onnx')) {
    $path = Join-Path $LandmarkDir $name
    if ($Force) {
        $need += $name
        continue
    }
    if (-not (Test-Path $path)) {
        $need += $name
        continue
    }
    $hash = (Get-FileHash $path -Algorithm SHA256).Hash.ToLower()
    if ($hash -ne $Expected[$name]) {
        Write-Host "[!] $name hash mismatch (got $hash). Will re-fetch." -ForegroundColor Yellow
        $need += $name
    } else {
        Write-Host "[OK] $name already present (SHA-256 matches)." -ForegroundColor Green
    }
}

if (-not $need) {
    Write-Host "`nAll required ONNX models already in place. Nothing to do." -ForegroundColor Green
    return
}

# -- Download + extract ----------------------------------------------------------
$url = $URLS[$Source]
$zipPath = Join-Path $LandmarkDir 'buffalo_l.zip'

Write-Host "`nDownloading $Source buffalo_l pack from:" -ForegroundColor Cyan
Write-Host "  $url"

try {
    Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
} catch {
    Write-Host "[X] Download failed: $_" -ForegroundColor Red
    Write-Host "    Manual fallback: see docs/MODELS.md -> Option B." -ForegroundColor Yellow
    exit 1
}

if (-not (Test-Path $zipPath)) {
    Write-Host "[X] Downloaded archive not found at $zipPath" -ForegroundColor Red
    exit 1
}

Write-Host "`nExtracting archive..." -ForegroundColor Cyan
$extractDir = Join-Path $LandmarkDir '_extract_tmp'
if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
New-Item -ItemType Directory -Path $extractDir -Force | Out-Null
Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force

# -- Move needed files, verify, clean up ----------------------------------------
foreach ($name in @('det_10g.onnx', '2d106det.onnx')) {
    # buffalo_l.zip extracts to <extractDir>/buffalo_l/<name>
    $candidates = @(
        (Join-Path $extractDir $name),
        (Join-Path $extractDir "buffalo_l/$name")
    )
    $src = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $src) {
        Write-Host "[X] $name not found in archive." -ForegroundColor Red
        exit 1
    }
    $dst = Join-Path $LandmarkDir $name
    Move-Item -Force $src $dst

    # Verify SHA-256
    $hash = (Get-FileHash $dst -Algorithm SHA256).Hash.ToLower()
    if ($hash -ne $Expected[$name]) {
        Write-Host "[X] $name SHA-256 mismatch after extract." -ForegroundColor Red
        Write-Host "    Got:      $hash" -ForegroundColor Red
        Write-Host "    Expected: $($Expected[$name])" -ForegroundColor Red
        exit 1
    }
    Write-Host "[OK] $name extracted and verified (SHA-256 matches)." -ForegroundColor Green
}

# -- Cleanup --------------------------------------------------------------------
Remove-Item -Recurse -Force $extractDir -ErrorAction SilentlyContinue
Remove-Item -Force $zipPath -ErrorAction SilentlyContinue

Write-Host "`nDone. Required models are in place under third_party/landmark/" -ForegroundColor Green
Write-Host "See docs/MODELS.md for details." -ForegroundColor Cyan