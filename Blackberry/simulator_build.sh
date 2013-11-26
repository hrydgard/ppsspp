#!/bin/bash

BB_OS=`cat ${QNX_TARGET}/etc/qversion 2>/dev/null`
if [ -z "$BB_OS" ]; then
    echo "Could not find your Blackberry NDK. Please source bbndk-env.sh"
    exit 1
fi
echo "Building for Blackberry ${BB_OS} Simulator"

# Set up cmake with GCC 4.6.3 cross-compiler from PATH
CC=ntox86-gcc CXX=ntox86-g++ cmake -DSIMULATOR=ON -DBLACKBERRY=${BB_OS} ..

# Compile and create unsigned PPSSPP.bar with debugtoken
DEBUG="-devMode -debugToken ${HOME}/debugtoken.bar"
make -j4 && blackberry-nativepackager -package PPSSPP.bar bar-descriptor.xml $DEBUG
