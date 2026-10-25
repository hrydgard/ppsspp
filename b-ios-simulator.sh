#!/bin/bash
# Builds the App Store variant for the iOS simulator (Apple Silicon Macs), and runs it in the
# simulator. Useful for App Store screenshots of devices we don't have.
#
# Usage: ./b-ios-simulator.sh ["Device name"]
#
# With a device name ("iPhone Duo", "iPad Pro 13-inch (M5)" - see xcrun simctl list devicetypes),
# runs on a simulator of that type, creating and booting one if needed. Without one, runs on the
# booted simulator, or boots the newest iPhone if none is booted.

set -e

FOLDER_NAME="build-ios-sim"
CONFIGURATION="${CONFIGURATION:-Debug}"
BUNDLE_ID="org.ppsspp.ppsspp-free"
DEVICE_NAME="$1"

if [[ ! -d $FOLDER_NAME ]]; then
	cmake -S . -B $FOLDER_NAME -GXcode -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/ios.cmake -DIOS_PLATFORM=SIMULATOR -DIOS_APP_STORE=ON
	# Xcode looks for git-version.cpp before any script phase gets to generate it.
	cmake -DSOURCE_DIR=. -DOUTPUT_DIR=$FOLDER_NAME -P git-version.cmake
fi

xcodebuild -project $FOLDER_NAME/PPSSPP.xcodeproj -scheme PPSSPP -configuration $CONFIGURATION -destination 'generic/platform=iOS Simulator' -quiet build

APP="$FOLDER_NAME/$CONFIGURATION-iphonesimulator/PPSSPP.app"
echo "Built $APP"

# Finds an existing simulator with the given name, on any runtime.
find_device() {
	xcrun simctl list devices available | grep -F "    $1 (" | head -1 | sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/'
}

if [[ -z $DEVICE_NAME ]]; then
	UDID=$(xcrun simctl list devices booted | grep -E -o '[0-9A-F-]{36}' | head -1)
	if [[ -z $UDID ]]; then
		# Newest device type the installed runtimes support. The newest type overall may have none.
		DEVICE_NAME=$(xcrun simctl list devices available | sed -n '/^-- iOS/,/^-- [^i]/p' | grep -E '^    iPhone [0-9]+ Pro \(' | tail -1 | sed -E 's/^    (.*) \([0-9A-F-]{36}\).*/\1/')
	fi
fi

if [[ -n $DEVICE_NAME ]]; then
	UDID=$(find_device "$DEVICE_NAME")
	if [[ -z $UDID ]]; then
		echo "Creating a simulator: $DEVICE_NAME"
		# Picks the newest runtime the device type supports.
		UDID=$(xcrun simctl create "$DEVICE_NAME" "$DEVICE_NAME")
	fi
fi

xcrun simctl boot "$UDID" 2>/dev/null || true
# The simulator UI: DeviceHub from Xcode 27, Simulator before that.
open -b com.apple.dt.Devices 2>/dev/null || open -a Simulator
xcrun simctl bootstatus "$UDID" -b > /dev/null

xcrun simctl install "$UDID" "$APP"
xcrun simctl launch --terminate-running-process "$UDID" $BUNDLE_ID
