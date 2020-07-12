#/bin/bash

export USE_CCACHE=1
export NDK_CCACHE=ccache
export HOMEBREW_NO_INSTALL_CLEANUP=1
export HOMEBREW_NO_AUTO_UPDATE=1
export HOMEBREW_NO_ANALYTICS=1
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

brew_make_bottle() {
    echo "Rebuilding $1 as bottle..."
    brew uninstall -f --ignore-dependencies $1 && brew install --ignore-dependencies --build-bottle $1 || true
    brew bottle $1 && brew postinstall $1 || true
    rm $HOME/Library/Caches/Homebrew/$1-*.bottle.*.tar.gz || true
    mv ./$1-*.bottle.*.tar.gz $HOME/Library/Caches/Homebrew/ || true
}

travis_before_install() {
    git submodule update --init --recursive

    if [ "$TRAVIS_OS_NAME" = osx ]; then
        # Depends on Python, wastes time updating...
        brew uninstall -f mercurial || true

        # To check version numbers, we want jq.  Try to cache this too.
        for PKG in automake oniguruma; do
            if ! brew info --json $PKG | grep built_as_bottle > /dev/null; then
                if [ -f $HOME/Library/Caches/Homebrew/$PKG*.bottle.*.tar.gz ]; then
                    brew install -f $HOME/Library/Caches/Homebrew/$PKG*.bottle.*.tar.gz || true
                else
                    brew_make_bottle $PKG
                fi
            fi
        done
        brew install jq || true

        # Try to install as many at once as possible.
        TO_UPGRADE=""
        TO_UNINSTALL=""
        for PKG in ccache openssl@1.1 pyenv pkg-config readline gdbm sqlite xz python sdl2; do
            PKG_VER="`brew info $PKG --json | jq '.[0].versions.stable' | tr -d '"'`"
            if [ -f $HOME/Library/Caches/Homebrew/$PKG--$PKG_VER*.bottle.*.tar.gz ]; then
                TO_UPGRADE="$TO_UPGRADE $HOME/Library/Caches/Homebrew/$PKG--$PKG_VER*.bottle.*.tar.gz"
                TO_UNINSTALL="$TO_UNINSTALL $PKG"
            fi
        done

        for PKG in ccache openssl@1.1 pyenv pkg-config readline gdbm sqlite xz python sdl2; do
            PKG_VER="`brew info $PKG --json | jq '.[0].versions.stable' | tr -d '"'`"
            if [ ! -f $HOME/Library/Caches/Homebrew/$PKG--$PKG_VER*.bottle.*.tar.gz ]; then
                brew_make_bottle $PKG
            fi
        done

        if [ "$TO_UPGRADE" != "" ]; then
            brew uninstall -f --ignore-dependencies $TO_UNINSTALL
            brew install -f --ignore-dependencies $TO_UPGRADE || true
        fi

        # In case there were issues with all at once, now let's try installing any others from cache.
        for PKG in ccache openssl@1.1 pyenv pkg-config readline gdbm sqlite xz python sdl2; do
            PKG_VER="`brew info $PKG --json | jq '.[0].versions.stable' | tr -d '"'`"
            if [ -f $HOME/Library/Caches/Homebrew/$PKG--$PKG_VER*.bottle.*.tar.gz ]; then
                brew upgrade $HOME/Library/Caches/Homebrew/$PKG--$PKG_VER*.bottle.*.tar.gz || brew install -f $HOME/Library/Caches/Homebrew/$PKG-*.bottle.*.tar.gz || true
            fi
        done
    fi
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

        if [ "$LIBRETRO" = "TRUE" ]; then
            ./b.sh --libretro_android ppsspp_libretro
        else
            pushd android
            ./ab.sh -j2 APP_ABI=$APP_ABI
            popd
        fi

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

        if [ "$UWP" == "TRUE" ]; then
            msbuild.exe UWP\\PPSSPP_UWP.sln -m -p:CLToolExe=clcache.exe -p:Configuration=Release -p:Platform=x64 -p:TrackFileAccess=false -p:AppxPackageSigningEnabled=false
        else
            msbuild.exe Windows\\PPSSPP.sln -m -p:CLToolExe=clcache.exe -p:Configuration=Release -p:Platform=x64 -p:TrackFileAccess=false
        fi
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
