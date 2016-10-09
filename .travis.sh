#/bin/bash

export USE_CCACHE=1
export NDK_CCACHE=ccache
NDK_VER=android-ndk-r12b

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

	if [ ! "$TRAVIS_OS_NAME" = "osx" ]; then
		sudo apt-get update -qq
		sudo apt-get install software-properties-common aria2 pv build-essential libgl1-mesa-dev libglu1-mesa-dev -qq

		if [ "$CMAKE" = "TRUE" ]; then
			sudo apt-get install lib32stdc++6 lib32z1 lib32z1-dev cmake -qq
		fi
	fi
}

setup_ccache_script() {
	if [ ! -e "$1" ]; then
		mkdir "$1"
	fi

	echo "#!/bin/bash" > "$1/$3"
	echo "ccache $2/$3 \$*" >> "$1/$3"
	chmod +x "$1/$3"
}

travis_install() {
	# Ubuntu Linux + GCC 4.8
	if [ "$PPSSPP_BUILD_TYPE" = "Linux" ]; then
		# For libsdl2-dev.
		sudo add-apt-repository ppa:zoogie/sdl2-snapshots -y
		if [ "$CXX" = "g++" ]; then
			sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
		fi
		if [ "$QT" = "TRUE" ]; then
			sudo add-apt-repository --yes ppa:ubuntu-sdk-team/ppa
		fi

		sudo apt-get update
		sudo apt-get install libsdl2-dev -qq
		if [ "$CXX" = "g++" ]; then
			sudo apt-get install g++-4.8 -qq
		fi

		if [ "$QT" = "TRUE" ]; then
			sudo apt-get install -qq qt5-qmake qtmultimedia5-dev qtsystems5-dev qtbase5-dev qtdeclarative5-dev qttools5-dev-tools libqt5webkit5-dev libsqlite3-dev qt5-default
		fi
	fi

	# Android NDK + GCC 4.8
	if [ "$PPSSPP_BUILD_TYPE" = "Android" ]; then
		free -m
		sudo apt-get install ant -qq
		download_extract_zip http://dl.google.com/android/repository/${NDK_VER}-linux-x86_64.zip ${NDK_VER}-linux-x86_64.zip
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
		if [ "$QT" = "TRUE" ]; then
			./b.sh --qt
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
	fi
	if [ "$PPSSPP_BUILD_TYPE" = "iOS" ]; then
		./b.sh --ios
		pushd build
		xcodebuild -configuration Release
		popd build
	fi
}

travis_after_success() {
	ccache -s

	if [ "$PPSSPP_BUILD_TYPE" = "Linux" ]; then
		./test.py
	fi
}

set -e
set -x

$1;
