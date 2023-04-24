#!/bin/bash

PPSSPP="${1}"
PPSSPPSDL="${PPSSPP}/Contents/MacOS/PPSSPPSDL"

ls ${PPSSPPSDL}

if [ ! -f "${PPSSPPSDL}" ]; then
  echo "No such file: ${PPSSPPSDL}!"
  exit 0
fi

# We only support using the local SDL framework.
# All the old detection code has been eliminated.
SDL=../SDL/macOS/SDL2.framework

echo "Copying SDL2.framework..."

mkdir -p "${PPSSPP}/Contents/Frameworks" || exit 0
cp -r "$SDL" "${PPSSPP}/Contents/Frameworks" || exit 0

install_name_tool -change "${SDL}" "@executable_path/../Frameworks/${SDLNAME}" "${PPSSPPSDL}" || exit 0

echo "Done."

GIT_VERSION_LINE=$(grep "PPSSPP_GIT_VERSION = " "$(dirname "${0}")/../git-version.cpp")
echo "Setting version to '${GIT_VERSION_LINE}'..."
SHORT_VERSION_MATCH='.*"v([0-9\.]+(-[0-9]+)?).*";'
LONG_VERSION_MATCH='.*"v(.*)";'
if [[ "${GIT_VERSION_LINE}" =~ ^${SHORT_VERSION_MATCH}$ ]]; then
	plutil -replace CFBundleShortVersionString -string $(echo ${GIT_VERSION_LINE} | perl -pe "s/${SHORT_VERSION_MATCH}/\$1/g") ${PPSSPP}/Contents/Info.plist
	plutil -replace CFBundleVersion            -string $(echo ${GIT_VERSION_LINE} | perl -pe "s/${LONG_VERSION_MATCH}/\$1/g")  ${PPSSPP}/Contents/Info.plist
else
	plutil -replace CFBundleShortVersionString -string "" ${PPSSPP}/Contents/Info.plist
	plutil -replace CFBundleVersion            -string "" ${PPSSPP}/Contents/Info.plist
fi

# AdHoc codesign is required for Apple Silicon.
echo "Signing..."
#codesign -fs - --deep "${PPSSPP}" || exit 1
