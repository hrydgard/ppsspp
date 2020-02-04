#!/bin/bash

PPSSPP="${1}"
PPSSPPQt="${PPSSPP}/Contents/MacOS/PPSSPPQt"

if [ ! -f "${PPSSPPQt}" ]; then
  echo "No such file: ${PPSSPPQt}!"
  exit 0
fi

plutil -replace NSPrincipalClass -string NSApplication ${PPSSPP}/Contents/Info.plist
plutil -replace NSHighResolutionCapable -bool YES ${PPSSPP}/Contents/Info.plist

# TODO: install SDL and Qt frameworks
