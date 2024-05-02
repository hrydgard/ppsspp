# Build script for iOS app store

# Set the development team ID as a DEVTEAM env variable.

mkdir build-ios
pushd build-ios

cmake .. -DIOS_APP_STORE=ON -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/ios.cmake -DDEVELOPMENT_TEAM_ID=$(DEVTEAM) -DIOS_PLATFORM=OS -GXcode
# TODO: Get a MoltenVK somewhere.
#cp ../MoltenVK/iOS/Frameworks/libMoltenVK.dylib PPSSPP.app/Frameworks
popd

# To open the xcode project:
# open build-ios/PPSSPP.xcodeproj