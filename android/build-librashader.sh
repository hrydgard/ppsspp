#!/usr/bin/env bash
# Cross-builds librashader's C API for Android and drops librashader.so into the APK's jniLibs.
# Usage: android/build-librashader.sh [ABI ...]   (default: arm64-v8a armeabi-v7a x86_64)
# Env:   LIBRASHADER_TAG (default librashader-v0.12.0), LIBRASHADER_SRC, ANDROID_NDK_HOME
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
TAG=${LIBRASHADER_TAG:-librashader-v0.12.0}
SRC=${LIBRASHADER_SRC:-$REPO/build/librashader-src}
OUT=$REPO/android/src/main/jniLibs
ABIS=("$@"); [ ${#ABIS[@]} -eq 0 ] && ABIS=(arm64-v8a armeabi-v7a x86_64)
PLATFORM=21   # matches -DANDROID_PLATFORM=android-21 in android/build.gradle.kts

need() { command -v "$1" >/dev/null 2>&1 || { echo "error: '$1' not found ($2)" >&2; exit 1; }; }
need cargo "install Rust stable >= 1.88 via rustup"
need rustup "install Rust via rustup"
cargo ndk --version >/dev/null 2>&1 || { echo "error: cargo-ndk not found (cargo install cargo-ndk)" >&2; exit 1; }

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
	NDK_VER=$(sed -n 's/.*ndkVersion = "\([^"]*\)".*/\1/p' "$REPO/android/build.gradle.kts" | head -1)
	for base in "${ANDROID_HOME:-}" "$HOME/Library/Android/sdk" "$HOME/Android/Sdk" /opt/homebrew/share/android-commandlinetools; do
		[ -n "$base" ] && [ -d "$base/ndk/$NDK_VER" ] && export ANDROID_NDK_HOME="$base/ndk/$NDK_VER" && break
	done
fi
[ -d "${ANDROID_NDK_HOME:-}" ] || { echo "error: set ANDROID_NDK_HOME (NDK not found)" >&2; exit 1; }
echo "NDK: $ANDROID_NDK_HOME"

target_for() { case "$1" in arm64-v8a) echo aarch64-linux-android;; armeabi-v7a) echo armv7-linux-androideabi;; x86_64) echo x86_64-linux-android;; x86) echo i686-linux-android;; *) echo "error: unknown ABI $1" >&2; exit 1;; esac; }
for abi in "${ABIS[@]}"; do
	t=$(target_for "$abi")
	rustup target list --installed | grep -qx "$t" || { echo "error: rustup target $t missing (rustup target add $t)" >&2; exit 1; }
done

if [ ! -d "$SRC/.git" ]; then
	git clone --depth 1 --branch "$TAG" https://github.com/SnowflakePowered/librashader.git "$SRC"
else
	(cd "$SRC" && git fetch --depth 1 origin "refs/tags/$TAG:refs/tags/$TAG" && git checkout -q "$TAG")
fi

HOST=$(uname -s | tr '[:upper:]' '[:lower:]')-x86_64
READELF="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST/bin/llvm-readelf"
NM="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST/bin/llvm-nm"
VERIFY=1
[ -x "$READELF" ] && [ -x "$NM" ] || { echo "warning: NDK llvm-readelf/llvm-nm not found at $READELF / $NM; skipping soname/export verification" >&2; VERIFY=0; }

for abi in "${ABIS[@]}"; do
	t=$(target_for "$abi")
	echo "== $abi ($t)"
	( cd "$SRC" && RUSTFLAGS="-C link-arg=-Wl,-soname,librashader.so" \
	  cargo ndk -t "$abi" --platform "$PLATFORM" build -p librashader-capi --release \
	    --no-default-features --features runtime-vulkan,runtime-opengl )
	ARTIFACT="$SRC/target/$t/release/liblibrashader_capi.so"
	if [ "$VERIFY" -eq 1 ]; then
		"$READELF" -d "$ARTIFACT" | grep -q 'SONAME.*\[librashader.so\]' || { echo "error: soname not set for $abi" >&2; exit 1; }
		n=$("$NM" -gD --defined-only "$ARTIFACT" | grep -c ' libra_' || true)
		[ "$n" -ge 40 ] || { echo "error: only $n libra_* exports for $abi" >&2; exit 1; }
	fi
	mkdir -p "$OUT/$abi"
	cp "$ARTIFACT" "$OUT/$abi/librashader.so"
	[ "$VERIFY" -eq 1 ] && echo "   ok: soname librashader.so, $n exports, $(du -h "$OUT/$abi/librashader.so" | cut -f1)"
done
echo "Done. Rebuild the APK (gradle packages android/src/main/jniLibs automatically)."
