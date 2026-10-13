#!/bin/bash
# Checks out the submodules like actions/checkout's "submodules: recursive" does, except that of
# FFmpeg, which holds prebuilt libraries for every platform (1.5 GB), only the given paths are
# fetched. They're sparse checkout patterns relative to ffmpeg/, like /linux/x86_64/. The source
# tree's headers always come along, since some builds have the ffmpeg folder on the include path.
#
# Run from the repository root, after a checkout without submodules:
#   .github/checkout-submodules.sh /linux/x86_64/

set -e

# On Windows, keep Git Bash from turning the patterns into Windows paths on the way to git.exe.
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

others=$(git config -f .gitmodules --get-regexp '^submodule\..*\.path$' | awk '$2 != "ffmpeg" {print $2}')
git submodule update --init --recursive --depth 1 --jobs 8 -- $others

# A partial clone, so only the blobs the sparse checkout needs get downloaded.
sha=$(git rev-parse HEAD:ffmpeg)
url=$(git config -f .gitmodules submodule.ffmpeg.url)
git -C ffmpeg init -q
git -C ffmpeg remote add origin "$url"
git -C ffmpeg sparse-checkout set --no-cone '/*.h' '/lib*/**/*.h' "$@"
git -C ffmpeg fetch -q --depth 1 --filter=blob:none origin "$sha"
git -C ffmpeg checkout -q FETCH_HEAD
du -sh ffmpeg
