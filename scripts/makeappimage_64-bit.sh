#!/bin/sh

set -ex

export ARCH="$(uname -m)"
export VERSION=${1:-test}
export ADD_HOOKS="self-updater.hook"
export UPINFO="gh-releases-zsync|${GITHUB_REPOSITORY%/*}|${GITHUB_REPOSITORY#*/}|latest|*$ARCH.AppImage.zsync"
export ICON="$PWD"/icons/hicolor/256x256/apps/ppsspp.png
export DESKTOP="$PWD"/SDL/PPSSPPSDL.desktop
export DEPLOY_OPENGL=1
export DEPLOY_VULKAN=1

QUICK_SHARUN="https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/refs/heads/main/useful-tools/quick-sharun.sh"

# ADD LIBRARIES
wget "$QUICK_SHARUN" -O ./quick-sharun
chmod +x ./quick-sharun

./quick-sharun ./build/PPSSPPSDL

# copy assets dir needs to be next to the binary
cp -vr ./build/assets ./AppDir/bin

# Make AppImage with uruntime
quick-sharun --make-appimage

