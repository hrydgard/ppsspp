#/bin/bash

NDK_VER=android-ndk-r10d

download_extract() {
	aria2c -x 16 $1 -o $2
	tar -xf $2
}

# This is used for the Android NDK.
download_extract_xz() {
	aria2c --file-allocation=none --timeout=120 --retry-wait=5 --max-tries=20 -Z -c $1 -o $2
	stat -c 'ATTEMPT 1 - %s' $2
	md5sum $2
	# This resumes the download, in case it failed.
	aria2c --file-allocation=none --timeout=120 --retry-wait=5 --max-tries=20 -Z -c $1 -o $2
	stat -c 'ATTEMPT 2 - %s' $2
	md5sum $2

	# Keep some output going during the extract, so the build doesn't timeout.
	pv $2 | xz -vd | tar -x
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
		download_extract_xz http://hdkr.co/${NDK_VER}-x86_64.tar.xz ${NDK_VER}-x86_64.tar.xz
	fi

	# Blackberry NDK: 10.3.0.440 + GCC: 4.8.2
	if [ "$PPSSPP_BUILD_TYPE" = "Blackberry" ]; then
		download_extract http://downloads.blackberry.com/upr/developers/update/bbndk/10_3_beta/ndktarget_10.3.0.440/ndktargetrepo_10.3.0.440/packages/bbndk.linux.libraries.10.3.0.440.tar.gz libs.tar.gz
		download_extract http://downloads.blackberry.com/upr/developers/update/bbndk/10_3_beta/ndktarget_10.3.0.440/ndktargetrepo_10.3.0.440/packages/bbndk.linux.tools.10.3.0.2702.tar.gz tools.tar.gz
		sed -i 's/-g../&-4.8.2/g' Blackberry/bb.toolchain.cmake
	fi

	# Symbian NDK: Belle + GCC: 4.8.3
	if [ "$PPSSPP_BUILD_TYPE" = "Symbian" ]; then
		sudo apt-get install lib32stdc++6 lib32bz2-1.0 -qq
		download_extract https://github.com/xsacha/SymbianGCC/releases/download/4.8.3/gcc4.8.3_x86-64.tar.bz2 compiler.tar.bz2
		download_extract https://github.com/xsacha/SymbianGCC/releases/download/4.8.3/ndk-new.tar.bz2 ndk.tar.bz2
		export EPOCROOT=$(pwd)/SDKs/SymbianSR1Qt474/ SBS_GCCE483BIN=$(pwd)/gcc4.8.3_x86-64/bin
		cp ffmpeg/symbian/armv6/lib/* $EPOCROOT/epoc32/release/armv5/urel/
	fi
}

travis_script() {
	# Compile PPSSPP
	if [ "$PPSSPP_BUILD_TYPE" = "Linux" ]; then
		if [ "$CXX" = "g++" ]; then
			export CXX="g++-4.8" CC="gcc-4.8"
		fi

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
		./ab.sh -j1
		popd
	fi
	if [ "$PPSSPP_BUILD_TYPE" = "Blackberry" ]; then
		export QNX_TARGET="$(pwd)/target_10_3_0_440/qnx6" QNX_HOST="$(pwd)/host_10_3_0_2702/linux/x86" && PATH="$QNX_HOST/usr/bin:$PATH"

		./b.sh --release --no-package
	fi
	if [ "$PPSSPP_BUILD_TYPE" = "Symbian" ]; then
		export EPOCROOT=$(pwd)/SDKs/SymbianSR1Qt474/ SBS_GCCE483BIN=$(pwd)/gcc4.8.3_x86-64/bin
		PATH=$SBS_GCCE483BIN:$(pwd)/tools/sbs/bin:$EPOCROOT/epoc32/tools:$EPOCROOT/bin:$(pwd)/tools/sbs/linux-x86_64-libc2_15/bin:$PATH
		QMAKE_ARGS="CONFIG+=no_assets" ./b.sh --debug --no-package
	fi
	if [ "$PPSSPP_BUILD_TYPE" = "iOS" ]; then
		./b.sh --ios
		pushd build
		xcodebuild -configuration Release
		popd build
	fi
}

travis_after_success() {
	if [ "$PPSSPP_BUILD_TYPE" = "Linux" ]; then
		./test.py
	fi
}

set -e
set -x

$1;
