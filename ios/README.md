iOS Build Instructions
======================

Prerequisites:
--------------

* Xcode (from the Mac App Store) with command line tools installed
* MacPorts (from macports.org); easiest to install with their package installers
* cmake build system (from MacPorts); run "sudo port install cmake" from the command line
* gas-preprocessor (to compile ffmpeg libs); download the zip from https://github.com/mansr/gas-preprocessor, unzip and from the command line run:

        sudo cp gas-preprocessor.pl /usr/bin/
        sudo chmod +rw /usr/bin/gas-preprocessor.pl

* you may need pkg-config (from MacPorts); run "sudo port install pkgconfig" from the command line

Most of this is done from the command line:
-------------------------------------------

Change directory to wherever you want to install ppsspp (eg. "cd ~"), and then clone the main ppsspp repository:

    git clone https://github.com/hrydgard/ppsspp.git

Change directory to the newly created ppsspp directory and run:

    git submodule update --init

Change directory into ffmpeg and run (this will take a while):

    ./ios-build.sh

Change directory back up to the main ppsspp directory and do the following:

    mkdir build-ios
    cd build-ios
    cmake -DCMAKE_TOOLCHAIN_FILE=../ios/ios.toolchain.cmake -GXcode ..

You now should have an Xcode project file in the build-ios directory named PPSSPP.xcodeproj.  Open it up in Xcode and do Product->Build to build the debug version, or Product->Build For->Archiving to build the release version (which is much faster).  If your iOS device is plugged in, you may be able to just Run in Xcode to install and test it.  Otherwise, copy the PPSSPP app from build-ios/Debug-iphoneos/PPSSPP.app or build-ios/Release-iphoneos/PPSSPP.app to the /Applications directory on your device and from ssh or MobileTerminal do a "chmod +x PPSSPP" inside the PPSSPP.app directory.  If this is the first time you've installed the PPSSPP app, you'll have to respring or restart your device for the icon to show up.