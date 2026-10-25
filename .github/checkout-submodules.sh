#!/bin/bash
# Checks out the submodules like actions/checkout's "submodules: recursive" does, but shallow.
#
# Run from the repository root, after a checkout without submodules:
#   .github/checkout-submodules.sh

set -e

git submodule update --init --recursive --depth 1 --jobs 8
