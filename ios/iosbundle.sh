#!/bin/bash

echo "iosbundle.sh params 0: ${0} 1: ${1} 2: ${2}"
echo $(pwd)

PPSSPP="${1}"

GIT_VERSION_FILE="${2}/git-version.cpp"
if [ ! -f "${GIT_VERSION_FILE}" ]; then
  echo "iosbundle.sh: No git-version.cpp file: ${GIT_VERSION_FILE}"
  exit 0
fi
echo "GIT_VERSION_FILE: ${GIT_VERSION_FILE}"

GIT_VERSION_LINE=$(grep "PPSSPP_GIT_VERSION = " $GIT_VERSION_FILE)

SHORT_VERSION_MATCH='.*"v([0-9\.]+(-[0-9]+)?).*";'
LONG_VERSION_MATCH='.*"v(.*)";'
FULL_VERSION=$(echo ${GIT_VERSION_LINE} | perl -pe "s/${LONG_VERSION_MATCH}/\$1/g")

echo "Full version string: $FULL_VERSION"

# Crunches to version number to something that XCode will validate.
SHORT_VERSION=$(perl $2/../ios/version-transform.pl $FULL_VERSION)
#LONG_VERSION=$FULL_VERSION

# Turns out we can't have anything except numbers or dots, or XCode will crash
# during validation. So for now, let's set them to the same thing. Not really sure
# why you'd differentiate.
LONG_VERSION=$SHORT_VERSION

echo "Writing versions to Info.plist. Short: $SHORT_VERSION Long: $LONG_VERSION"

if [[ "${GIT_VERSION_LINE}" =~ ^${SHORT_VERSION_MATCH}$ ]]; then
	plutil -replace CFBundleShortVersionString -string $SHORT_VERSION ${PPSSPP}/Info.plist
	plutil -replace CFBundleVersion            -string $LONG_VERSION ${PPSSPP}/Info.plist
else
	plutil -replace CFBundleShortVersionString -string "" ${PPSSPP}/Info.plist
	plutil -replace CFBundleVersion            -string "" ${PPSSPP}/Info.plist
fi

echo "iosbundle.sh: Updated ${PPSSPP}/Info.plist"
