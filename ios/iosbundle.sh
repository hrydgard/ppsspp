#!/bin/bash

echo "iosbundle.sh params 0: ${0} 1: ${1} 2: ${2}"

PPSSPP="${1}"
PPSSPPiOS="${PPSSPP}/PPSSPP"

if [ ! -f "${PPSSPPiOS}" ]; then
  echo "iosbundle.sh: No such file: ${PPSSPPiOS}!"
  exit 0
fi

GIT_VERSION_FILE="${1}/../../git-version.cpp"
if [ ! -f "${GIT_VERSION_FILE}" ]; then
  echo "iosbundle.sh: No git-version.cpp file: ${GIT_VERSION_FILE}"
  exit 0
fi
echo "GIT_VERSION_FILE: ${GIT_VERSION_FILE}"

GIT_VERSION_LINE=$(grep "PPSSPP_GIT_VERSION = " $GIT_VERSION_FILE)

SHORT_VERSION_MATCH='.*"v([0-9\.]+(-[0-9]+)?).*";'
LONG_VERSION_MATCH='.*"v(.*)";'

SHORT_VERSION=$(echo ${GIT_VERSION_LINE} | perl -pe "s/${SHORT_VERSION_MATCH}/\$1/g")
LONG_VERSION=$(echo ${GIT_VERSION_LINE} | perl -pe "s/${LONG_VERSION_MATCH}/\$1/g")

echo "Writing versions to Info.plist. Short, long: $SHORT_VERSION , $LONG_VERSION"

if [[ "${GIT_VERSION_LINE}" =~ ^${SHORT_VERSION_MATCH}$ ]]; then
	plutil -replace CFBundleShortVersionString -string $SHORT_VERSION ${PPSSPP}/Info.plist
	plutil -replace CFBundleVersion            -string $LONG_VERSION ${PPSSPP}/Info.plist
else
	plutil -replace CFBundleShortVersionString -string "" ${PPSSPP}/Info.plist
	plutil -replace CFBundleVersion            -string "" ${PPSSPP}/Info.plist
fi

echo "iosbundle.sh: Updated ${PPSSPP}/Info.plist"
