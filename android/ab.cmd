SET NDK=C:\Android\sdk\ndk\29.0.14206865
REM SET NDK=C:\Android\ndk
SET NDK_MODULE_PATH=..\ext
%NDK%/ndk-build -j%NUMBER_OF_PROCESSORS% %*
