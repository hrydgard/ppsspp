#!/bin/bash
rm -rf build
mkdir build
pushd build
cmake ..
cmake ..
make -j 2
make install
install/bin/glslangValidator -i ../Test/sample.vert ../Test/sample.frag
popd
