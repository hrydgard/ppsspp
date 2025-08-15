#!/bin/bash

PPSSPP="${1}"
PPSSPPiOS="${PPSSPP}/PPSSPP"

if [ ! -f "${PPSSPPiOS}" ]; then
  echo "macbundle.sh: No such file: ${PPSSPPiOS}!"
  exit 0
fi

GIT_VERSION_LINE=$(grep "PPSSPP_GIT_VERSION = " "$(dirname "${0}")/../git-version.cpp")
SHORT_VERSION_MATCH='.*"v([0-9\.]+(-[0-9]+)?).*";'
LONG_VERSION_MATCH='.*"v(.*)";'
if [[ "${GIT_VERSION_LINE}" =~ ^${SHORT_VERSION_MATCH}$ ]]; then
	plutil -replace CFBundleShortVersionString -string $(echo ${GIT_VERSION_LINE} | perl -pe "s/${SHORT_VERSION_MATCH}/\$1/g") ${PPSSPP}/Info.plist
	plutil -replace CFBundleVersion            -string $(echo ${GIT_VERSION_LINE} | perl -pe "s/${LONG_VERSION_MATCH}/\$1/g")  ${PPSSPP}/Info.plist
else
	plutil -replace CFBundleShortVersionString -string "" ${PPSSPP}/Info.plist
	plutil -replace CFBundleVersion            -string "" ${PPSSPP}/Info.plist
fi

echo "macbundle.sh: Updated ${PPSSPP}/Info.plist"