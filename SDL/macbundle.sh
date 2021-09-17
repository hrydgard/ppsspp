#!/bin/bash

PPSSPP="${1}"
PPSSPPSDL="${PPSSPP}/Contents/MacOS/PPSSPPSDL"

if [ ! -f "${PPSSPPSDL}" ]; then
  echo "No such file: ${PPSSPPSDL}!"
  exit 0
fi

SDL=$(otool -L "${PPSSPPSDL}" | grep -v @executable_path | grep -Eo /.+libSDL.+dylib)
if [ "${SDL}" != "" ]; then
  if [ ! -f "${SDL}" ]; then
    echo "Cannot locate SDL: ${SDL}!"
    exit 0
  fi

  echo "Installing SDL from ${SDL}..."

  SDLNAME=$(basename "${SDL}")
  mkdir -p "${PPSSPP}/Contents/Frameworks" || exit 0
  cp -r "$SDL" "${PPSSPP}/Contents/Frameworks" || exit 0
  install_name_tool -change "${SDL}" "@executable_path/../Frameworks/${SDLNAME}" "${PPSSPPSDL}" || exit 0
elif [ "$(otool -L "${PPSSPPSDL}" | grep @rpath/SDL)" != "" ]; then
  cd "$(dirname "$0")"
  RPATH="$(pwd)/macOS"
  cd -
  SDL="${RPATH}/SDL2.framework"
  if [ ! -d "${SDL}" ]; then
    echo "Cannot locate SDL.framework: ${SDL}!"
    exit 0
  fi

  rm -rf "${PPSSPP}/Contents/Frameworks/SDL2.framework" || exit 0
  mkdir -p "${PPSSPP}/Contents/Frameworks" || exit 0
  cp -a "$SDL" "${PPSSPP}/Contents/Frameworks" || exit 0
  install_name_tool -rpath "${RPATH}" "@executable_path/../Frameworks" "${PPSSPPSDL}" || echo "Already patched."
fi

GIT_VERSION_LINE=$(grep "PPSSPP_GIT_VERSION = " "$(dirname "${0}")/../git-version.cpp")
echo "Setting version to ${GIT_VERSION_LINE}..."
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
codesign -fs - --deep "${PPSSPP}" || exit 1
