# Build script for iOS app store

echo "PPSSPP App Store XCode generator script"

# Terminate Xcode if it's already running, since we're about to regenerate the project it has open.
if pgrep -x "Xcode" > /dev/null; then
  echo "Xcode is currently running, quitting it..."
  osascript -e 'tell application "Xcode" to quit' 2>/dev/null
  while pgrep -x "Xcode" > /dev/null; do
    sleep 0.5
  done
  echo "Xcode has quit."
fi

# Set the development team ID as a DEVTEAM env variable.

if [[ -z "${DEVTEAM}" ]]; then
  echo "DEVTEAM not in environment, exiting"
  exit 1
fi

if [[ -z "${GOLD}" ]]; then
  echo "GOLD is not set (should be YES or NO), exiting"
  exit 1
fi

if [[ -z "${USE_IAP}" ]]; then
  echo "USE_IAP is not set (should be YES or NO), exiting"
  exit 1
fi

FOLDER_NAME="build-ios"

if [[ "$GOLD" = "YES" ]]; then
  echo "GOLD is set to YES, setting folder to build-ios-gold"
  FOLDER_NAME="build-ios-gold"
else
  echo "Non-GOLD build."
fi

if [[ "$USE_IAP" = "YES" ]]; then
  if [[ "$GOLD" = "YES" ]]; then
    echo "IAP and GOLD are both set to YES, which is invalid"
    exit 1
  fi
  echo "IAP on."
else
  echo "IAP off."
fi

echo "Clearing and re-creating output directory"
rm -rf $FOLDER_NAME
mkdir $FOLDER_NAME

pushd $FOLDER_NAME

BUILD_TYPE=Release

cmake .. -DIOS_APP_STORE=ON -DGOLD=$GOLD -DUSE_IAP=$USE_IAP -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/ios.cmake -DDEVELOPMENT_TEAM_ID=${DEVTEAM} -DIOS_PLATFORM=OS -GXcode
# TODO: Get a MoltenVK somewhere.
#cp ../MoltenVK/iOS/Frameworks/libMoltenVK.dylib PPSSPP.app/Frameworks
popd

# Very gross hack
# Avoid XCode race condition (???) by pre-generating git-version.cpp
cmake -DSOURCE_DIR=. -DOUTPUT_DIR=$FOLDER_NAME -P git-version.cmake

echo
echo "*** Done."
read -p "Launch Xcode with $FOLDER_NAME/PPSSPP.xcodeproj now? [y/N] " LAUNCH_XCODE
case "$LAUNCH_XCODE" in
  [Yy]*)
    open "$FOLDER_NAME/PPSSPP.xcodeproj"
    ;;
  *)
    echo "Not launching. You can open it later with:"
    echo "  open $FOLDER_NAME/PPSSPP.xcodeproj"
    ;;
esac
