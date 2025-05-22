#!/bin/sh

set -ex

export ARCH="$(uname -m)"
LIB4BN="https://raw.githubusercontent.com/VHSgunzo/sharun/refs/heads/main/lib4bin"
APPIMAGETOOL="https://github.com/pkgforge-dev/appimagetool-uruntime/releases/download/continuous/appimagetool-$ARCH.AppImage"
UPINFO="gh-releases-zsync|$(echo "$GITHUB_REPOSITORY" | tr '/' '|')|latest|*$ARCH.AppImage.zsync"
export VERSION=test

SYS_LIB_DIR="/usr/lib"
if [ -d /usr/lib/"$ARCH"-linux-gnu ]; then
	SYS_LIB_DIR=/usr/lib/"$ARCH"-linux-gnu
fi

# Prepare AppDir
mkdir -p ./AppDir/bin
cd ./AppDir

cp -v  ../SDL/PPSSPPSDL.desktop ./
cp -v  ../icons/hicolor/256x256/apps/ppsspp.png ./
cp -v  ../icons/hicolor/256x256/apps/ppsspp.png ./.DirIcon

# ADD LIBRARIES
wget "$LIB4BN" -O ./lib4bin
chmod +x ./lib4bin
xvfb-run -a -- ./lib4bin -p -v -e -s -k \
	../build/PPSSPPSDL \
	"$SYS_LIB_DIR"/libSDL* \
	"$SYS_LIB_DIR"/lib*GL* \
	"$SYS_LIB_DIR"/libvulkan* \
	"$SYS_LIB_DIR"/dri/* \
	"$SYS_LIB_DIR"/libXss.so* \
	"$SYS_LIB_DIR"/pulseaudio/* \
	"$SYS_LIB_DIR"/pipewire-0.3/* \
	"$SYS_LIB_DIR"/spa-0.2/*/*

# copy assets dir needs to be next oteh binary
cp -vr ../build/assets ./bin

# Prepare sharun
echo "Preparing sharun..."
ln -s ./bin/PPSSPPSDL ./AppRun
./sharun -g

# Make AppImage with uruntime
cd ..
wget "$APPIMAGETOOL" -O ./appimagetool
chmod +x ./appimagetool

./appimagetool -n -u "$UPINFO" ./AppDir

echo "All Done!"
