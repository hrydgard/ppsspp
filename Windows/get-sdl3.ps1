# Downloads SDL3 VC development binaries to SDL3/ at the repo root.
# Safe to run repeatedly — exits early if already present.
# Update $SDL3_VERSION to upgrade.

$SDL3_VERSION = "3.2.14"
$SDL3_DIR = Join-Path $PSScriptRoot "..\ext\SDL3"
$SENTINEL = Join-Path $SDL3_DIR "include\SDL3\SDL.h"

if (Test-Path $SENTINEL) {
    Write-Host "SDL3 $SDL3_VERSION already present."
    exit 0
}

$URL     = "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-devel-$SDL3_VERSION-VC.zip"
$ZIP     = Join-Path $env:TEMP "SDL3-devel-$SDL3_VERSION-VC.zip"
$EXTRACT = Join-Path $env:TEMP "SDL3-extract-$SDL3_VERSION"

Write-Host "Downloading SDL3 $SDL3_VERSION from GitHub releases..."
Invoke-WebRequest -Uri $URL -OutFile $ZIP -UseBasicParsing

Write-Host "Extracting..."
if (Test-Path $EXTRACT) { Remove-Item -Recurse -Force $EXTRACT }
Expand-Archive -Path $ZIP -DestinationPath $EXTRACT -Force

if (Test-Path $SDL3_DIR) { Remove-Item -Recurse -Force $SDL3_DIR }
Move-Item (Join-Path $EXTRACT "SDL3-$SDL3_VERSION") $SDL3_DIR

Remove-Item -Force $ZIP
Remove-Item -Recurse -Force $EXTRACT -ErrorAction SilentlyContinue

Write-Host "SDL3 $SDL3_VERSION ready at $SDL3_DIR"
