# Check Blackberry NDK
BB_OS=`cat ${QNX_TARGET}/etc/qversion 2>/dev/null`
if [ -z "$BB_OS" ]; then
    echo "Could not find your Blackberry NDK. Please source bbndk-env.sh"
    exit 1
fi

# Strict errors. Any non-zero return exits this script
set -e
echo "Building for Blackberry ${BB_OS}"

if [[ "$1" == "--simulator" ]]; then
	SIM="-DSIMULATOR=ON"
fi

cmake ${SIM} -DCMAKE_TOOLCHAIN_FILE=bb.toolchain.cmake -DBLACKBERRY=${BB_OS} .. | (grep -v "^-- " || true)

# Compile and create unsigned PPSSPP.bar with debugtoken
make -j4
if [[ "$1" != "--no-package" ]]; then
	DEBUG="-devMode -debugToken ${HOME}/debugtoken.bar"
	blackberry-nativepackager -package PPSSPP.bar bar-descriptor.xml $DEBUG
fi
