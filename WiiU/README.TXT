
### Build Instructions
tested with devkitPPC r38

make sure you have the latest ppsspp-ffmpeg submodule:
cd ffmpeg; git pull origin master

then create a build directory, then navigate to it and run:
cmake -DWIIU=ON path/to/ppsspp
make

if you need console output over network, set the PC_DEVELOPMENT_IP_ADDRESS define in ext/wiiu/logger.c and you can use the ext/wiiu/net_listen.sh script to listen to the log.
the run.sh script can be used to both send the compiled rpx to the HBL and listen to console output.


