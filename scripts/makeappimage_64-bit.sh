#!/bin/sh

set -ex

export ARCH="$(uname -m)"
export VERSION=${1:-test}
export ICON="$PWD"/icons/hicolor/256x256/apps/ppsspp.png
export DEPLOY_OPENGL=1
export DEPLOY_VULKAN=1

QUICK_SHARUN="https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/refs/heads/main/useful-tools/quick-sharun.sh"

# ADD LIBRARIES
wget "$QUICK_SHARUN" -O ./quick-sharun
chmod +x ./quick-sharun

(
	export ADD_HOOKS="self-updater.hook"
	# PPSSPP-, so the headless AppImage's .zsync can't match.
	export UPINFO="gh-releases-zsync|${GITHUB_REPOSITORY%/*}|${GITHUB_REPOSITORY#*/}|latest|PPSSPP-*$ARCH.AppImage.zsync"
	export DESKTOP="$PWD"/SDL/PPSSPPSDL.desktop

	./quick-sharun ./build/PPSSPPSDL

	# copy assets dir needs to be next to the binary
	cp -vr ./build/assets ./AppDir/bin

	# Make AppImage with uruntime
	./quick-sharun --make-appimage
)

# PPSSPPHeadless, in an AppImage of its own.
(
	export APPDIR="$PWD"/AppDir-headless
	export UPINFO="gh-releases-zsync|${GITHUB_REPOSITORY%/*}|${GITHUB_REPOSITORY#*/}|latest|PPSSPPHeadless-*$ARCH.AppImage.zsync"
	export DESKTOP="$PWD"/PPSSPPHeadless.desktop
	cat > "$DESKTOP" <<-EOF
	[Desktop Entry]
	Name=PPSSPPHeadless
	Exec=PPSSPPHeadless
	Comment=PPSSPP without a UI, for tests and scripting
	Icon=ppsspp
	Type=Application
	Terminal=true
	NoDisplay=true
	Categories=Game;Emulator;
	EOF

	./quick-sharun ./build/PPSSPPHeadless
	cp -vr ./build/assets "$APPDIR"/bin
	./quick-sharun --make-appimage
)
# Old PPSSPP AppImages update from any *$ARCH.AppImage.zsync in the release, so don't ship this one.
rm -f ./PPSSPPHeadless*.zsync
