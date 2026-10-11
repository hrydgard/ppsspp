#!/bin/sh
# Prefer a runnable CMake on PATH; Windows SDK installations need not be on PATH.
try_cmake() {
    [ -n "$1" ] && "$1" --version >/dev/null 2>&1 || return 1
    if command -v cygpath >/dev/null 2>&1; then
        printf '"%s"\n' "$(cygpath -m "$1")"
    else
        printf '"%s"\n' "$1"
    fi
}
try_cmake "$(command -v cmake 2>/dev/null)" && exit 0
for candidate in "/c/Program Files/CMake/bin/cmake.exe" \
    "/c/Program Files/Microsoft Visual Studio"/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe \
    "/c/Program Files (x86)/Microsoft Visual Studio"/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe; do
    try_cmake "$candidate" && exit 0
done
# Keep a usable command for the recipe's explicit dependency diagnostic.
printf 'cmake\n'
