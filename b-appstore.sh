# Build script for iOS app store

echo "PPSSPP App Store XCode generator script"

# Set the development team ID as a DEVTEAM env variable.

if [[ -z "${DEVTEAM}" ]]; then
  echo "DEVTEAM not in environment, exiting"
  exit 1
fi

if [[ -z "${GOLD}" ]]; then
  echo "GOLD is not set (should be YES or NO), exiting"
  exit 1
fi

FOLDER_NAME="build-ios"

if [[ "$GOLD" = "YES" ]]; then
  echo "GOLD is set to YES, setting folder to build-ios-gold"
  FOLDER_NAME="build-ios-gold"
else
  echo "Non-GOLD build."
fi

echo "Clearing and re-creating output directory"
rm -rf $FOLDER_NAME
mkdir $FOLDER_NAME

pushd $FOLDER_NAME

BUILD_TYPE=Release

cmake .. -DIOS_APP_STORE=ON -DGOLD=$GOLD -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/ios.cmake -DDEVELOPMENT_TEAM_ID=${DEVTEAM} -DIOS_PLATFORM=OS -GXcode
# TODO: Get a MoltenVK somewhere.
#cp ../MoltenVK/iOS/Frameworks/libMoltenVK.dylib PPSSPP.app/Frameworks
popd

# Very gross hack
# Avoid XCode race condition (???) by pre-generating git-version.cpp
cmake -DSOURCE_DIR=. -DOUTPUT_DIR=$FOLDER_NAME -P git-version.cmake

echo
echo "*** Done. Now run the following command to open in XCode, then run or archive:"
echo "  open $FOLDER_NAME/PPSSPP.xcodeproj"

# To open the xcode project:
# open build-ios/PPSSPP.xcodeproj