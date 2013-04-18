xcopy ..\flash0 assets\flash0 /s /y
xcopy ..\lang assets\lang /s /y
SET NDK=C:\AndroidNDK
SET NDK_MODULE_PATH=..;..\native\ext
REM Need to force target-platform to android-9 to get access to OpenSL headers.
REM Hopefully this won't negatively affect anything else.
%NDK%/ndk-build TARGET_PLATFORM=android-9 -j9 %1
