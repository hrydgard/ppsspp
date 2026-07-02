#!/bin/bash
# Sets up the loongarch64 cross-compilation environment.
# Run once with sudo.  After this, ./b.sh --loongarch64 works on a fresh checkout.

set -e

SYSROOT=/usr/loongarch64-linux-gnu
CROSS_GCC=loongarch64-linux-gnu-gcc-14

if ! command -v $CROSS_GCC &>/dev/null; then
    echo "Error: $CROSS_GCC not found. Install it first:"
    echo "  sudo apt install gcc-14-loongarch64-linux-gnu g++-14-loongarch64-linux-gnu binutils-loongarch64-linux-gnu"
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run with sudo."
    exit 1
fi

# ── OpenGL headers ───────────────────────────────────────────────────────────
# GL headers are platform-independent C headers; copy from the host.
echo "Installing GL headers into sysroot..."
mkdir -p $SYSROOT/include/GL
for h in gl.h glext.h glcorearb.h glu.h; do
    [ -f /usr/include/GL/$h ] && cp /usr/include/GL/$h $SYSROOT/include/GL/ && echo "  $h"
done

# ── GL stub library ──────────────────────────────────────────────────────────
# Generate a loongarch64 libGL.so that exports all symbols from the host
# libGL so that GLEW's static archive can be linked via PLT entries (a direct
# B26 branch to address 0 would overflow on LoongArch).
echo "Generating libGL.so stub..."

HOST_GL=""
for candidate in \
        /usr/lib/x86_64-linux-gnu/libGL.so.1 \
        /usr/lib/x86_64-linux-gnu/libGL.so \
        /usr/lib/libGL.so.1 ; do
    [ -f "$candidate" ] && HOST_GL="$candidate" && break
done

STUB_C=$(mktemp /tmp/gl_stub_XXXXXX.c)
echo "/* Loongarch64 GL/GLX stub - for cross-compilation only */" > "$STUB_C"
echo "void __loongarch64_gl_placeholder(void) {}" >> "$STUB_C"

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

# ── Dynamic linker symlink ───────────────────────────────────────────────────
# Without this, binfmt_misc can't find the loongarch64 dynamic linker.
LD_LINUX=$SYSROOT/lib/ld-linux-loongarch-lp64d.so.1
if [ -f "$LD_LINUX" ]; then
    mkdir -p /lib64
    ln -sf "$LD_LINUX" /lib64/ld-linux-loongarch-lp64d.so.1
    echo "Created /lib64/ld-linux-loongarch-lp64d.so.1 -> $LD_LINUX"
fi

echo ""
echo "Setup complete."
echo "Build:  ./b.sh --loongarch64"
echo "Run:    qemu-loongarch64-static -L $SYSROOT <binary>"
echo "  (after setup the /lib64 symlink lets you run loongarch64 binaries directly)"
