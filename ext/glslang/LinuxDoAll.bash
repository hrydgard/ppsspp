#! /bin/bash

svn update

rm -f StandAlone/glslangValidator
rm -f Test/glslangValidator
rm -f glslang/MachineIndependent/lib/libglslang.so
rm -f Install/Linux/libglslang.so
rm -f Install/Linux/glslangValidator

cd StandAlone
make clean
cd ../glslang/MachineIndependent
make clean
cd ../..

# build the StandAlone app and all it's dependencies
make -C StandAlone

# so we can find the shared library
#LD_LIBRARY_PATH=`pwd`/glslang/MachineIndependent/lib
#export LD_LIBRARY_PATH

# install
cd Install/Linux
./install
cp glslangValidator ../../Test
LD_LIBRARY_PATH=/usr/local/lib
export LD_LIBRARY_PATH

# run using test data
cd ../../Test
chmod +x runtests
./runtests
