#!/bin/bash

PPSSPP="${1}"
PPSSPPQt="${PPSSPP}/Contents/MacOS/PPSSPPQt"

if [ ! -f "${PPSSPPQt}" ]; then
  echo "No such file: ${PPSSPPQt}!"
  exit 0
fi

GIT_VERSION_LINE=$(grep "PPSSPP_GIT_VERSION = " "$(dirname "${0}")/../git-version.cpp")

echo "Running Qt macbundle.sh for $PPSSPP/Contents/Info.plist ($GIT_VERSION_LINE)"

# Why don't we just put these in the template?
plutil -replace NSPrincipalClass -string NSApplication ${PPSSPP}/Contents/Info.plist
plutil -replace NSHighResolutionCapable -bool YES ${PPSSPP}/Contents/Info.plist

plutil -replace NSLocationWhenInUseUsageDescription -string "Your location may be used to emulate Go!Explore, a GPS accessory" ${PPSSPP}/Contents/Info.plist
plutil -replace NSCameraUsageDescription -string "Your camera may be used to emulate Go!Cam, a camera accessory" ${PPSSPP}/Contents/Info.plist
plutil -replace NSMicrophoneUsageDescription -string "Your microphone may be used to emulate Go!Cam/Talkman, a microphone accessory" ${PPSSPP}/Contents/Info.plist

SHORT_VERSION_MATCH='.*"v([0-9\.]+(-[0-9]+)?).*";'
LONG_VERSION_MATCH='.*"v(.*)";'
if [[ "${GIT_VERSION_LINE}" =~ ^${SHORT_VERSION_MATCH}$ ]]; then
	plutil -replace CFBundleShortVersionString -string $(echo ${GIT_VERSION_LINE} | perl -pe "s/${SHORT_VERSION_MATCH}/\$1/g") ${PPSSPP}/Contents/Info.plist
	plutil -replace CFBundleVersion            -string $(echo ${GIT_VERSION_LINE} | perl -pe "s/${LONG_VERSION_MATCH}/\$1/g")  ${PPSSPP}/Contents/Info.plist
else
	plutil -replace CFBundleShortVersionString -string "" ${PPSSPP}/Contents/Info.plist
	plutil -replace CFBundleVersion            -string "" ${PPSSPP}/Contents/Info.plist
fi

# TODO: install SDL and Qt frameworks
