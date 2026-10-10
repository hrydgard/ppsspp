Building for iOS
================

Needs Xcode. iOS 15 is the minimum.

After `git submodule update --init`, generate an Xcode project with one of:

* `./b-appstore.sh` - the App Store configuration, in `build-ios`. Needs your development team ID in
  the `DEVTEAM` environment variable.
* `./b.sh --ios-xcode` - the sideload configuration.

Then build and run from Xcode, or from the command line with `xcodebuild`. The app doesn't run in
the iOS Simulator.

`./b-ios.sh` builds an unsigned IPA for sideloading, the way CI does.

Updating with a self-built MoltenVK
===================================

Probably won't be needed again.

    cp -r ../dev/build-molten/MoltenVK/Package/Release/MoltenVK/static/MoltenVK.xcframework ios/MoltenVK
