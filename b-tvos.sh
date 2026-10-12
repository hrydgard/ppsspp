#!/bin/bash
# Builds the tvOS app (Apple TV, arm64). Without arguments it's an unsigned build, as in CI.
# To sign for a device, pass the signing settings, which then go to xcodebuild instead:
#   ./b-tvos.sh -allowProvisioningUpdates DEVELOPMENT_TEAM=<team> CODE_SIGN_STYLE=Automatic "CODE_SIGN_IDENTITY=Apple Development"

set -e

FOLDER_NAME="build-tvos-xcode"
CONFIGURATION="${CONFIGURATION:-Release}"

if [[ ! -d $FOLDER_NAME ]]; then
	cmake -S . -B $FOLDER_NAME -GXcode -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/ios.cmake -DIOS_PLATFORM=TVOS
fi
# Xcode looks for git-version.cpp before any script phase gets to generate it.
cmake -DSOURCE_DIR=. -DOUTPUT_DIR=$FOLDER_NAME -P git-version.cmake

if [[ $# -eq 0 ]]; then
	set -- CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO
fi

xcodebuild -project $FOLDER_NAME/PPSSPP.xcodeproj -scheme PPSSPP -configuration $CONFIGURATION -destination 'generic/platform=tvOS' -quiet build "$@"

echo "Built $FOLDER_NAME/$CONFIGURATION-appletvos/PPSSPP.app"
