xcopy ..\flash0 assets\flash0 /s /y
xcopy ..\lang assets\lang /s /y
xcopy ..\assets\shaders assets\shaders /s /y
copy ..\assets\langregion.ini assets\langregion.ini
copy ..\assets\*.png assets
SET NDK=C:\AndroidNDK
SET NDK_MODULE_PATH=..;..\native\ext
%NDK%/ndk-build -j9 %1
