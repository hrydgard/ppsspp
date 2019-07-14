#/bin/bash

export USE_CCACHE=1
export NDK_CCACHE=ccache
NDK_VER=android-ndk-r18b

download_extract() {
    aria2c -x 16 $1 -o $2
    tar -xf $2
}

# This is used for the Android NDK.
download_extract_zip() {
    aria2c --file-allocation=none --timeout=120 --retry-wait=5 --max-tries=20 -Z -c $1 -o $2
    # This resumes the download, in case it failed.
    aria2c --file-allocation=none --timeout=120 --retry-wait=5 --max-tries=20 -Z -c $1 -o $2

    unzip $2 2>&1 | pv > /dev/null
}

travis_before_install() {
    git submodule update --init --recursive
}

travis_install() {
    # Ubuntu Linux + GCC 4.8
    if [ "$PPSSPP_BUILD_TYPE" = "Linux" ]; then
        if [ "$CXX" = "g++" ]; then
            sudo apt-get install -qq g++-4.8
        fi

        if [ "$QT" = "TRUE" ]; then
            sudo apt-get install -qq qt5-qmake qtmultimedia5-dev qtsystems5-dev qtbase5-dev qtdeclarative5-dev qttools5-dev-tools libqt5webkit5-dev libqt5opengl5-dev libsqlite3-dev qt5-default
        fi

        download_extract "https://cmake.org/files/v3.6/cmake-3.6.2-Linux-x86_64.tar.gz" cmake-3.6.2-Linux-x86_64.tar.gz
    fi

    # Android NDK + GCC 4.8
    if [ "$PPSSPP_BUILD_TYPE" = "Android" ]; then
        download_extract_zip http://dl.google.com/android/repository/${NDK_VER}-linux-x86_64.zip ${NDK_VER}-linux-x86_64.zip
    fi

    if [ "$PPSSPP_BUILD_TYPE" = "Windows" ]; then
        curl -L https://github.com/frerich/clcache/releases/download/v4.2.0/clcache.4.2.0.nupkg --output clcache.4.2.0.nupkg
        choco install clcache --source=.
    fi

    # Ensure we're using ccache
    if [[ "$CXX" = "clang" && "$CC" == "clang" ]]; then
        export CXX="ccache clang" CC="ccache clang"
    fi
    if [[ "$PPSSPP_BUILD_TYPE" == "Linux" && "$CXX" == "g++" ]]; then
        # Also use gcc 4.8, instead of whatever default version.
        export CXX="ccache g++-4.8" CC="ccache gcc-4.8"
    fi
    if [[ "$CXX" != *ccache* ]]; then
        export CXX="ccache $CXX"
    fi
    if [[ "$CC" != *ccache* ]]; then
        export CC="ccache $CC"
    fi
}

travis_script() {
    # Compile PPSSPP
    if [ "$PPSSPP_BUILD_TYPE" = "Linux" ]; then
        if [ -d cmake-3.6.2-Linux-x86_64 ]; then
            export PATH=$(pwd)/cmake-3.6.2-Linux-x86_64/bin:$PATH
        fi

        if [ "$QT" = "TRUE" ]; then
            ./b.sh --qt
        elif [ "$LIBRETRO" = "TRUE" ]; then
            ./b.sh --libretro
        else
            ./b.sh --headless
        fi
    fi
    if [ "$PPSSPP_BUILD_TYPE" = "Android" ]; then
        export ANDROID_HOME=$(pwd)/${NDK_VER} NDK=$(pwd)/${NDK_VER}
        if [[ "$CXX" = *clang* ]]; then
            export NDK_TOOLCHAIN_VERSION=clang
        fi

        pushd android
        ./ab.sh -j2 APP_ABI=$APP_ABI
        popd

#        When we can get this to work...
#        chmod +x gradlew
#        ./gradlew assembleRelease
    fi
    if [ "$PPSSPP_BUILD_TYPE" = "iOS" ]; then
        ./b.sh --ios
    fi
    if [ "$PPSSPP_BUILD_TYPE" = "macOS" ]; then
        ./b.sh --headless
    fi
    if [ "$PPSSPP_BUILD_TYPE" = "Windows" ]; then
        export "MSBUILD_PATH=/c/Program Files (x86)/Microsoft Visual Studio/2017/BuildTools/MSBuild/15.0/Bin"
        export "PATH=$MSBUILD_PATH:$PATH"
        export CLCACHE_OBJECT_CACHE_TIMEOUT_MS=120000

        # Set DebugInformationFormat to nothing, the default is ProgramDatabase which breaks clcache.
        # Turns out it's not possible to pass this on the msbuild command line.
        for f in `find . -name *.vcxproj`; do
            sed -i 's/>ProgramDatabase<\/DebugInformationFormat>/><\/DebugInformationFormat>/g' $f
        done

        msbuild.exe Windows\\PPSSPP.sln -m -p:CLToolExe=clcache.exe -p:Configuration=Release -p:Platform=x64 -p:TrackFileAccess=false
    fi
}

travis_after_success() {
    if [ "$PPSSPP_BUILD_TYPE" != "Windows" ]; then
        ccache -s
    else
        clcache -s
        clcache -z
    fi

    if [ "$PPSSPP_BUILD_TYPE" = "Linux" ]; then
        ./test.py
    fi
}

set -e
set -x

$1;
