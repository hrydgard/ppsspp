mkdir assets > nul
xcopy ..\assets\flash0 assets\flash0\ /s /y <d.txt
xcopy ..\assets\lang assets\lang\ /s /y <d.txt
xcopy ..\assets\shaders assets\shaders\ /s /y <d.txt
xcopy ..\assets\themes assets\themes\ /s /y <d.txt
copy ..\assets\*.ini assets\
copy ..\assets\Roboto-Condensed.ttf assets\Roboto-Condensed.ttf
copy ..\assets\*.png assets\
copy ..\assets\*.zim assets\
copy ..\assets\*.meta assets\
copy ..\assets\*.wav assets\
SET NDK=C:\Android\ndk\27.0.12077973
REM SET NDK=C:\Android\ndk
SET NDK_MODULE_PATH=..\ext
%NDK%/ndk-build -j32 %*
