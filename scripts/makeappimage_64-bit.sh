#!/bin/sh

set -ex

ARCH="$(uname -m)"
LIB4BN="https://raw.githubusercontent.com/VHSgunzo/sharun/refs/heads/main/lib4bin"
URUNTIME="https://github.com/VHSgunzo/uruntime/releases/latest/download/uruntime-appimage-dwarfs-$ARCH"
UPINFO="gh-releases-zsync|$(echo "$GITHUB_REPOSITORY" | tr '/' '|')|latest|*$ARCH.AppImage.zsync"
VERSION=${1:-test}

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
	"$SYS_LIB_DIR"/libEGL* \
	"$SYS_LIB_DIR"/libGL* \
	"$SYS_LIB_DIR"/libvulkan* \
	"$SYS_LIB_DIR"/dri/* \
	"$SYS_LIB_DIR"/libXss.so* \
	"$SYS_LIB_DIR"/pulseaudio/* \
	"$SYS_LIB_DIR"/pipewire-0.3/* \
	"$SYS_LIB_DIR"/spa-0.2/*/*

# copy assets dir needs to be next to the binary
cp -vr ../build/assets ./bin

# Prepare sharun
echo "Preparing sharun..."
ln -s ./bin/PPSSPPSDL ./AppRun
./sharun -g

# Make AppImage with uruntime
cd ..
wget "$URUNTIME" -O ./uruntime
chmod +x ./uruntime

#Add udpate info to runtime
echo "Adding update information \"$UPINFO\" to runtime..."
./uruntime --appimage-addupdinfo "$UPINFO"

echo "Generating AppImage..."
./uruntime --appimage-mkdwarfs -f \
	--set-owner 0 --set-group 0 \
	--no-history --no-create-timestamp \
	--compression zstd:level=22 -S26 -B8 \
	--header uruntime \
	-i ./AppDir -o PPSSPP-"$VERSION"-anylinux-"$ARCH".AppImage

echo "Generating zsync file..."
zsyncmake ./*.AppImage -u ./*.AppImage

echo "All Done!"
