#!/usr/bin/env bash

if [ ! -f appimagetool-i686.AppImage ]; then
    wget https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-i686.AppImage
    chmod +x appimagetool-i686.AppImage
fi

if [ ! -f linuxdeploy-i386.AppImage ]; then
    wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-i386.AppImage
    chmod +x linuxdeploy-i386.AppImage
fi

DESTDIR=AppDir make install
./linuxdeploy-i386.AppImage --appimage-extract-and-run --appdir=AppDir \
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
./appimagetool-i686.AppImage --appimage-extract-and-run AppDir
