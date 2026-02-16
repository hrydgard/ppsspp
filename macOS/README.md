# MacOS notes

Entitlements.plist is to be used for code signing on macOS.

We enable "hardened runtime" in order to make notarization happy, but we need some exceptions.

## MoltenVK on Mac

To update MoltenVK, download the latest version and get the files from dylib/macOS and put them in ext/vulkan/macOS/Frameworks.
