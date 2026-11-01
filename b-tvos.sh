#!/bin/bash
# Builds the tvOS app (Apple TV, arm64) into build-tvos-xcode/.
#
# Without settings it's an unsigned build, as in CI. To sign it, and install and launch it on a
# paired Apple TV (see `xcrun devicectl list devices`):
#   DEVELOPMENT_TEAM=<team> DEVICE=<udid> ./b-tvos.sh
# DEVELOPMENT_TEAM alone signs without installing. Extra arguments go to xcodebuild.

set -e

FOLDER_NAME="build-tvos-xcode"
CONFIGURATION="${CONFIGURATION:-Release}"
APP="$FOLDER_NAME/$CONFIGURATION-appletvos/PPSSPP.app"

if [[ ! -d $FOLDER_NAME ]]; then
	cmake -S . -B $FOLDER_NAME -GXcode -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/ios.cmake -DIOS_PLATFORM=TVOS
fi
# Xcode looks for git-version.cpp before any script phase gets to generate it.
cmake -DSOURCE_DIR=. -DOUTPUT_DIR=$FOLDER_NAME -P git-version.cmake

if [[ -n "$DEVELOPMENT_TEAM" ]]; then
	# Device registration adds the Apple TV to the team's provisioning profile the first time.
	SIGNING=(-allowProvisioningUpdates -allowProvisioningDeviceRegistration DEVELOPMENT_TEAM=$DEVELOPMENT_TEAM CODE_SIGN_STYLE=Automatic "CODE_SIGN_IDENTITY=Apple Development")
else
	SIGNING=(CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO)
fi

if [[ -n "$DEVICE" ]]; then
	DESTINATION="id=$DEVICE"
else
	DESTINATION="generic/platform=tvOS"
fi

xcodebuild -project $FOLDER_NAME/PPSSPP.xcodeproj -scheme PPSSPP -configuration $CONFIGURATION -destination "$DESTINATION" -quiet build "${SIGNING[@]}" "$@"

echo "Built $APP"

if [[ -n "$DEVICE" && -n "$DEVELOPMENT_TEAM" ]]; then
	xcrun devicectl device install app --device "$DEVICE" "$APP"
	BUNDLE_ID=$(/usr/libexec/PlistBuddy -c "Print CFBundleIdentifier" "$APP/Info.plist")
	xcrun devicectl device process launch --device "$DEVICE" --terminate-existing --console "$BUNDLE_ID"
fi
