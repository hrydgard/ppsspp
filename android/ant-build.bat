set ANDROID_HOME=path/to/android-sdk/
set NDK=path/to/android-ndk/
ant release -Dndkbuildopt="APP_ABI=armeabi-v7a -j6"