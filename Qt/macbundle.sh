#!/bin/bash

PPSSPP="${1}"
PPSSPPQt="${PPSSPP}/Contents/MacOS/PPSSPPQt"

if [ ! -f "${PPSSPPQt}" ]; then
  echo "No such file: ${PPSSPPQt}!"
  exit 0
fi

plutil -replace NSPrincipalClass -string NSApplication ${PPSSPP}/Contents/Info.plist
plutil -replace NSHighResolutionCapable -bool YES ${PPSSPP}/Contents/Info.plist

plutil -replace NSLocationWhenInUseUsageDescription -string "Your location may be used to emulate Go!Explore, a GPS accessory" ${PPSSPP}/Contents/Info.plist
plutil -replace NSCameraUsageDescription -string "Your camera may be used to emulate Go!Cam, a camera accessory" ${PPSSPP}/Contents/Info.plist
plutil -replace NSMicrophoneUsageDescription -string "Your microphone may be used to emulate Go!Cam/Talkman, a microphone accessory" ${PPSSPP}/Contents/Info.plist

# TODO: install SDL and Qt frameworks
