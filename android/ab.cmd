xcopy ..\assets\flash0 assets\flash0\ /s /y <d.txt
xcopy ..\assets\lang assets\lang\ /s /y <d.txt
xcopy ..\assets\shaders assets\shaders\ /s /y <d.txt
copy ..\assets\langregion.ini assets\langregion.ini
copy ..\assets\compat.ini assets\compat.ini
copy ..\assets\Roboto-Condensed.ttf assets\Roboto-Condensed.ttf
copy ..\assets\*.png assets\
REM SET NDK=C:\Android\sdk\ndk\21.3.6528147
SET NDK=C:\Android\ndk
SET NDK_MODULE_PATH=..\ext;..\ext\native\ext
%NDK%/ndk-build -j32 %*
