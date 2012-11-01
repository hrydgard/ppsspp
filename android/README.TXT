First, build the C++ static library:
> cd android
> ./ab.sh

Start Eclipse, import the android directory as an existing project
Build and run. 

If you modify the C++ code, you need to rebuild the static library, of
course. To get Eclipse to understand that you have in fact changed something
if you haven't also changed any Java code, just add a space character to 
PPSSPPActivity.java, or right click the project and choose Refresh, and then
relaunch the app on the device.

A real Android device is strongly recommended for testing. Don't trust
the emulator.
