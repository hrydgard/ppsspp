# Build script for iOS app store

# Set the development team ID as a DEVTEAM env variable.

if [[ -z "${DEVTEAM}" ]]; then
  echo "DEVTEAM not in environment, exiting"
  exit 1
fi

if [[ -z "${GOLD}" ]]; then
  echo "GOLD is not set (should be YES or NO), exiting"
  exit 1
fi

rm -rf build-ios
mkdir build-ios
pushd build-ios

BUILD_TYPE=Release

echo $DEVTEAM
echo $BUILD_TYPE

cmake .. -DIOS_APP_STORE=ON -DGOLD=$GOLD -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/ios.cmake -DDEVELOPMENT_TEAM_ID=${DEVTEAM} -DIOS_PLATFORM=OS -GXcode
# TODO: Get a MoltenVK somewhere.
#cp ../MoltenVK/iOS/Frameworks/libMoltenVK.dylib PPSSPP.app/Frameworks
popd

# To open the xcode project:
# open build-ios/PPSSPP.xcodeproj