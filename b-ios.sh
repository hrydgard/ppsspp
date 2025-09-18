#!/bin/sh

set -ex

# Assuming we're at the ppsspp (repo's root) directory
mkdir -p build # For the final IPA & DEB file
#rm -rf build-ios # Should we started from scratch instead of continuing from cache?
mkdir -p build-ios
cd build-ios
rm -rf PPSSPP.app # There seems to be an existing symlink, may be from ccache? We don't want to include old stuff that might be removed to be included in the final IPA file.
# It seems xcodebuild is looking for "git-version.cpp" file inside "build-ios" directory instead of at repo's root dir.
echo "const char *PPSSPP_GIT_VERSION = \"$(git describe --always)\";" > git-version.cpp
echo "#define PPSSPP_GIT_VERSION_NO_UPDATE 1" >> git-version.cpp
# Generate exportOptions.plist for xcodebuild
echo '<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
   <key>method</key>
   <string>development</string>
   <key>signingStyle</key>
   <string>manual</string>
</dict>
</plist>' > exportOptions.plist
# TODO: Generate a self-signed certificate (but probably not a good idea to generate a different cert all the time). Example at https://stackoverflow.com/questions/27474751/how-can-i-codesign-an-app-without-being-in-the-mac-developer-program/53562496#53562496

# Should we run this script first before building? (Seems to only generate icons for Gold version) 
#brew install pillow #python3 -m pip install --upgrade --break-system-packages --user Pillow
#pushd ../ios/assets.xcassets/AppIcon.appiconset
#python3 ../../generate_icons.py
#ls -la
#popd

# There are 2 ways to build PPSSPP for iOS, using make or xcodebuild
# Generate xcodeproject (only needed when building using xcode, similar to ./b.sh --ios-xcode)
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchains/ios.cmake -GXcode ..
# Build PPSSPP using xcode
#xcodebuild clean build -project PPSSPP.xcodeproj CODE_SIGNING_ALLOWED=NO -sdk iphoneos -configuration Release
xcodebuild -project PPSSPP.xcodeproj -scheme PPSSPP -sdk iphoneos -configuration Release clean build archive -archivePath ./build/PPSSPP.xcarchive CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO #CODE_SIGN_IDENTITY="iPhone Distribution: Your NAME / Company (TeamID)" #PROVISIONING_PROFILE="xxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
# Export IPA file from xcarchive (probably only works with signed build)
#xcodebuild -exportArchive -archivePath ./build/PPSSPP.xcarchive -exportPath ./build -exportOptionsPlist exportOptions.plist
# This folder only exist when building with xcodebuild
if [ -e Release-iphoneos ]; then
	# It seems there is an existing (from ccache?) PPSSPP.app symlink, so we copy & overwrites the content instead of the symlink, to the same location with what make use.
	mkdir -p PPSSPP.app
	cp -Rfa Release-iphoneos/PPSSPP.app/. PPSSPP.app/
fi

# Build PPSSPP using Makefile (Might have missing stuff like Icons, similar to ./b.sh --ios)
#cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchains/ios.cmake ..
#make -j4

cp ../ext/vulkan/iOS/Frameworks/libMoltenVK.dylib PPSSPP.app/Frameworks
ln -s ./ Payload
#ldid -w -S -IlibMoltenVK -K../../certificate.p12 -Upassword PPSSPP.app/Frameworks/libMoltenVK.dylib
if [ -e PPSSPP.app/Frameworks/libMoltenVK.dylib ]; then
	echo "Signing PPSSPP.app/Frameworks/libMoltenVK.dylib ..."
	ldid -S -IlibMoltenVK PPSSPP.app/Frameworks/libMoltenVK.dylib
fi

echo '<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>platform-application</key>
    <true/>
    <key>com.apple.private.security.storage.AppDataContainers</key>
    <true/>
    <key>com.apple.private.security.storage.MobileDocuments</key>
    <true/>
    <key>com.apple.private.security.container-required</key>
    <false/>
    <key>com.apple.private.security.no-container</key>
    <true/>
    <key>com.apple.private.security.no-sandbox</key>
    <true/>
    <key>com.apple.developer.kernel.extended-virtual-addressing</key>
    <true/>
    <key>com.apple.developer.kernel.increased-memory-limit</key>
    <true/>
    <key>com.apple.security.iokit-user-client-class</key>
    <array>
        <string>AGXDeviceUserClient</string>
        <string>IOMobileFramebufferUserClient</string>
        <string>IOSurfaceRootUserClient</string>
    </array>
    <key>get-task-allow</key>
    <true/>
</dict>
</plist>' > ent.xml
#ldid -S ent.xml Payload/PPSSPP.app/PPSSPP
#ldid -w -Sent.xml -K../../certificate.p12 -Upassword PPSSPP.app
if [ -e PPSSPP.app/PPSSPP ]; then
	echo "Signing PPSSPP.app/PPSSPP ..."
	ldid -Sent.xml PPSSPP.app/PPSSPP
fi
version_number=`echo "$(git describe --always --match="v*" | sed -e 's@-\([^-]*\)-\([^-]*\)$@-\1-\2@;s@^v@@;s@%@~@g')"`
echo ${version_number} > PPSSPP.app/Version.txt
sudo -S chown -R 1004:3 Payload

# Put the xcarchive file in the artifact too, just to examine the contents
#cp -a build/PPSSPP.xcarchive ../build/

echo "Making ipa ..."
zip -r9 ../build/PPSSPP-iOS-v${version_number}.ipa Payload/PPSSPP.app
echo "IPA DONE :)"

echo "Making deb ..."
package_name="org.ppsspp.ppsspp-dev-latest_v${version_number}_iphoneos-arm"
mkdir $package_name
mkdir ${package_name}/DEBIAN
# TODO: Generate Preferences folder and it's contents too. Example of the contents at https://github.com/Halo-Michael/ppsspp-builder/tree/master/Preferences

echo "Package: org.ppsspp.ppsspp-dev-latest
Name: PPSSPP (Dev-Latest)
Architecture: iphoneos-arm
Description: A PSP emulator 
Icon: file:///Library/PPSSPPRepoIcons/org.ppsspp.ppsspp-dev-latest.png
Homepage: https://build.ppsspp.org/
Conflicts: com.myrepospace.theavenger.PPSSPP, net.angelxwind.ppsspp, net.angelxwind.ppsspp-testing, org.ppsspp.ppsspp, org.ppsspp.ppsspp-dev-working
Provides: com.myrepospace.theavenger.PPSSPP, net.angelxwind.ppsspp, net.angelxwind.ppsspp-testing
Replaces: com.myrepospace.theavenger.PPSSPP, net.angelxwind.ppsspp, net.angelxwind.ppsspp-testing
Depiction: https://cydia.ppsspp.org/?page/org.ppsspp.ppsspp-dev-latest
Maintainer: Henrik Rydgård
Author: Henrik Rydgårdq
Section: Games
Version: 0v${version_number}
" > ${package_name}/DEBIAN/control
chmod 0755 ${package_name}/DEBIAN/control
mkdir ${package_name}/Library
mkdir ${package_name}/Library/PPSSPPRepoIcons
# Seems to be 120x120 pixels (ie. 60x60@2x ?)
if [ -e ../ios/org.ppsspp.ppsspp.png ]; then
	cp ../ios/org.ppsspp.ppsspp.png ${package_name}/Library/PPSSPPRepoIcons/org.ppsspp.ppsspp-dev-latest.png
 	chmod 0755 ${package_name}/Library/PPSSPPRepoIcons/org.ppsspp.ppsspp-dev-latest.png
# Let's use AppIcon60x60@2x.png as alternative
elif [ -e 'PPSSPP.app/AppIcon60x60@2x.png' ]; then
	cp 'PPSSPP.app/AppIcon60x60@2x.png' ${package_name}/Library/PPSSPPRepoIcons/org.ppsspp.ppsspp-dev-latest.png
 	chmod 0755 ${package_name}/Library/PPSSPPRepoIcons/org.ppsspp.ppsspp-dev-latest.png
fi
mkdir ${package_name}/Library/PreferenceLoader
if [ -e ../ios/Preferences ]; then
	cp -a ../ios/Preferences ${package_name}/Library/PreferenceLoader/
fi
mkdir ${package_name}/Applications
cp -a PPSSPP.app ${package_name}/Applications/PPSSPP.app
sudo -S chown -R 1004:3 ${package_name}
sudo -S dpkg -b ${package_name} ../build/${package_name}.deb
sudo -S rm -r ${package_name}
echo "User = $USER"
sudo -S chown $USER ../build/${package_name}.deb
echo "Done, you should get the ipa and deb now :)"
