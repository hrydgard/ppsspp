#!/bin/bash
# Sets up a cross-compilation environment for one of the headless-only targets.
# Run once with sudo.  After this, ./b.sh --<target> works on a fresh checkout.
#
# Usage: sudo cmake/scripts/setup-cross.sh <loongarch64|riscv64|armhf>

set -e

TARGET="$1"

case "$TARGET" in
    loongarch64)
        TRIPLE=loongarch64-linux-gnu
        LD_SO=ld-linux-loongarch-lp64d.so.1
        ;;
    riscv64)
        TRIPLE=riscv64-linux-gnu
        LD_SO=ld-linux-riscv64-lp64d.so.1
        ;;
    armhf)
        TRIPLE=arm-linux-gnueabihf
        LD_SO=ld-linux-armhf.so.3
        # 32-bit ARM looks for its dynamic linker in /lib, and qemu names it plain arm.
        LD_DIR=/lib
        QEMU=qemu-arm
        # The ARM build links EGL as well as GL.
        NEEDS_EGL=1
        ;;
    *)
        echo "Usage: sudo $0 <loongarch64|riscv64|armhf>"
        exit 1
        ;;
esac

SYSROOT=/usr/$TRIPLE
CROSS_GCC=$TRIPLE-gcc-14
LD_DIR=${LD_DIR:-/lib64}
QEMU=${QEMU:-qemu-$TARGET}

if ! command -v $CROSS_GCC &>/dev/null; then
    echo "Error: $CROSS_GCC not found. Install it first:"
    echo "  sudo apt install gcc-14-$TRIPLE g++-14-$TRIPLE binutils-$TRIPLE"
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run with sudo."
    exit 1
fi

HOST_MULTIARCH=$(gcc -print-multiarch 2>/dev/null || dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null)

# ── OpenGL headers ───────────────────────────────────────────────────────────
# GL headers are platform-independent C headers; copy from the host.
echo "Installing GL headers into sysroot..."
mkdir -p $SYSROOT/include/GL
for h in gl.h glext.h glcorearb.h glu.h; do
    [ -f /usr/include/GL/$h ] && cp /usr/include/GL/$h $SYSROOT/include/GL/ && echo "  $h"
done

# ── GL stub library ──────────────────────────────────────────────────────────
# Generate a libGL.so for the target that exports all symbols from the host
# libGL so that GLEW's static archive can be linked via PLT entries (a direct
# branch to address 0 would overflow the relocation on these targets).
echo "Generating libGL.so stub..."

HOST_GL=""
for candidate in \
        /usr/lib/$HOST_MULTIARCH/libGL.so.1 \
        /usr/lib/$HOST_MULTIARCH/libGL.so \
        /usr/lib/libGL.so.1 ; do
    [ -f "$candidate" ] && HOST_GL="$candidate" && break
done

STUB_C=$(mktemp /tmp/gl_stub_XXXXXX.c)
echo "/* $TARGET GL/GLX stub - for cross-compilation only */" > "$STUB_C"
echo "void __cross_gl_placeholder(void) {}" >> "$STUB_C"

if [ -n "$HOST_GL" ]; then
    nm -D "$HOST_GL" 2>/dev/null | awk '/^[0-9a-f]+ T /{ print "void "$3"(void){}" }' >> "$STUB_C"
    echo "  Extracted symbols from $HOST_GL"
fi
# Always include the minimal set GLEW needs (in case host GL wasn't found)
for sym in glXGetProcAddressARB glXGetClientString glXQueryVersion \
           glBindTexture glGetString glGetIntegerv glGetError; do
    grep -q "void ${sym}(" "$STUB_C" || echo "void ${sym}(void){}" >> "$STUB_C"
done

$CROSS_GCC -shared -fPIC -Wno-implicit-function-declaration \
    -o $SYSROOT/lib/libGL.so "$STUB_C"
rm "$STUB_C"
echo "  Installed to $SYSROOT/lib/libGL.so"

if [ -n "$NEEDS_EGL" ]; then
    echo "Generating libEGL.so stub..."
    STUB_C=$(mktemp /tmp/egl_stub_XXXXXX.c)
    echo "/* $TARGET EGL stub - for cross-compilation only */" > "$STUB_C"
    echo "void eglGetProcAddress(void){}" >> "$STUB_C"
    for lib in /usr/lib/$HOST_MULTIARCH/libEGL.so.1 /usr/lib/$HOST_MULTIARCH/libEGL.so; do
        if [ -f "$lib" ]; then
            nm -D "$lib" 2>/dev/null | awk '/^[0-9a-f]+ T /{ print "void "$3"(void){}" }' | grep -v ' eglGetProcAddress(' >> "$STUB_C"
            break
        fi
    done
    $CROSS_GCC -shared -fPIC -o $SYSROOT/lib/libEGL.so "$STUB_C"
    rm "$STUB_C"
    echo "  Installed to $SYSROOT/lib/libEGL.so"
fi

# ── Dynamic linker symlink ───────────────────────────────────────────────────
# Without this, binfmt_misc can't find the target's dynamic linker.
LD_LINUX=$SYSROOT/lib/$LD_SO
if [ -f "$LD_LINUX" ]; then
    mkdir -p $LD_DIR
    ln -sf "$LD_LINUX" $LD_DIR/$LD_SO
    echo "Created $LD_DIR/$LD_SO -> $LD_LINUX"
fi

echo ""
echo "Setup complete."
echo "Build:  ./b.sh --$TARGET"
echo "Run:    $QEMU -L $SYSROOT <binary>"
echo "  (after setup the $LD_DIR symlink lets you run $TARGET binaries directly)"
