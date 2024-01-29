#!/bin/bash

PPSSPP="${1}"
CMAKE_BINARY_DIR="${2}"
PPSSPPiOS="${PPSSPP}/PPSSPP"

if [ ! -f "${PPSSPPiOS}" ]; then
  echo "No such file: ${PPSSPPiOS}!"
  exit 0
fi

GIT_VERSION_LINE=$(grep "PPSSPP_GIT_VERSION = " "${CMAKE_BINARY_DIR}/git-version.cpp")
SHORT_VERSION_MATCH='.*"v([0-9]+(\.[0-9]+)*).*";'
LONG_VERSION_MATCH='.*"v(.*)";'
if [[ "${GIT_VERSION_LINE}" =~ ^${SHORT_VERSION_MATCH}$ ]]; then
	plutil -replace CFBundleShortVersionString -string $(echo ${GIT_VERSION_LINE} | perl -pe "s/-/./g; s/${SHORT_VERSION_MATCH}/\$1/g") ${PPSSPP}/Info.plist
	plutil -replace CFBundleVersion            -string $(echo ${GIT_VERSION_LINE} | perl -pe "s/${LONG_VERSION_MATCH}/\$1/g")  ${PPSSPP}/Info.plist
else
	plutil -replace CFBundleShortVersionString -string "" ${PPSSPP}/Info.plist
	plutil -replace CFBundleVersion            -string "" ${PPSSPP}/Info.plist
fi
