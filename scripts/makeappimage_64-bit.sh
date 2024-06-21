#!/usr/bin/env bash

if [ ! -f appimagetool-x86_64.AppImage ]; then
	APPIMAGETOOL=$(wget -q https://api.github.com/repos/probonopd/go-appimage/releases -O - | sed 's/"/ /g; s/ /\n/g' | grep -o 'https.*continuous.*tool.*86_64.*mage$')
	wget -q "$APPIMAGETOOL" -O ./appimagetool-x86_64.AppImage
    chmod +x appimagetool-x86_64.AppImage
fi

if [ ! -f linuxdeploy-x86_64.AppImage ]; then
    wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x linuxdeploy-x86_64.AppImage
fi

mkdir ./AppDir/
mkdir ./AppDir/usr/
mkdir ./AppDir/usr/bin/
mkdir ./AppDir/usr/share/
mkdir ./AppDir/usr/share/applications/
mkdir ./AppDir/usr/share/icons/
mkdir ./AppDir/usr/share/icons/hicolor/
mkdir ./AppDir/usr/share/icons/hicolor/256x256/
mkdir ./AppDir/usr/share/icons/hicolor/256x256/apps/

cp ~/ppsspp/SDL/PPSSPPSDL.desktop ./AppDir/
cp ~/ppsspp/SDL/PPSSPPSDL.desktop ./AppDir/usr/share/applications/
cp ~/ppsspp/build/PPSSPPSDL ./AppDir/usr/bin/
cp -R ~/ppsspp/build/assets ./AppDir/usr/bin/
cp ~/ppsspp/icons/hicolor/256x256/apps/ppsspp.png ./AppDir/usr/share/icons/hicolor/256x256/apps/

DESTDIR=AppDir make install
./linuxdeploy-x86_64.AppImage --appimage-extract-and-run --appdir=AppDir \
	--exclude-library="libX*" \
	--exclude-library="libglib*" \
	--exclude-library="libgobject*" \
	--exclude-library="libgdk_pixbuf*" \
	--exclude-library="libwayland*" \
	--exclude-library="libgmodule*" \
	--exclude-library="libgio*" \
	--exclude-library="libxcb*" \
	--exclude-library="libxkbcommon*" \
	--exclude-library="libdb*"

rm AppDir/ppsspp.png
pushd AppDir
ln -s usr/share/icons/hicolor/256x256/apps/ppsspp.png
chmod +x AppRun
popd
ARCH=x86_64
VERSION=$(./AppDir/AppRun --version) ./appimagetool-x86_64.AppImage --appimage-extract-and-run AppDir
