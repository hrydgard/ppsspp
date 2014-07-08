# This file is to add features deemed necessary that don't exist in qmake.conf
# There is no linux setting in qmake.conf

# For our purposes, maemo5 and meego perform the same.
maemo5|contains(MEEGO_EDITION,harmattan): CONFIG += maemo

# All supported platforms that require tweaks
PLATFORM_NAME="generic"
win32: PLATFORM_NAME="Windows"
unix: PLATFORM_NAME="linux"
qnx: PLATFORM_NAME="blackberry"
mac: PLATFORM_NAME="macosx"
ios: PLATFORM_NAME="ios"
maemo: PLATFORM_NAME="maemo"
symbian: PLATFORM_NAME="symbian"
android: PLATFORM_NAME="android"

!equals(PLATFORM_NAME, "generic"): include($$PLATFORM_NAME".pri")

