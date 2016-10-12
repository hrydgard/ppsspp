# This file is to add features deemed necessary that don't exist in qmake.conf
# There is no linux setting in qmake.conf

# All supported platforms that require tweaks
PLATFORM_NAME="generic"
win32: PLATFORM_NAME="Windows"
unix: PLATFORM_NAME="linux"
mac: PLATFORM_NAME="macosx"
ios: PLATFORM_NAME="ios"
android: PLATFORM_NAME="android"

!equals(PLATFORM_NAME, "generic"): include($$PLATFORM_NAME".pri")

