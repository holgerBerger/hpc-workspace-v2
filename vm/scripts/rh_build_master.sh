#!/bin/bash

cd /home/vagrant
git clone https://github.com/holgerBerger/hpc-workspace-v2
cd hpc-workspace-v2
git pull
rm -rf build
cd ..
echo .......................................
echo ...debug build.........................
echo .......................................
cd hpc-workspace-v2/;  cmake --preset debug-ninja -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"; cmake --build --preset debug -j; sudo cp build/debug/bin/ws_restore buil
bug/bin/ws_restore_notest

echo .......................................
echo ...unit tests..........................
echo .......................................
#cd hpc-workspace-v2 
ctest --preset debug .

cd
echo .......................................
echo ...release build.......................
echo .......................................
cd hpc-workspace-v2/; cmake --preset release-ninja -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"; cmake --build --preset release -j; sudo cp build/release/bin/ws_restore build/release/bin/ws_restore_notest
echo .......................................

# user tests
sudo rm -rf /tmp/ws
mkdir /tmp/ws

#cd hpc-workspace-v2 
echo .......................................
echo ...bats tests as user..................
echo .......................................
sudo rm -f /tmp/ws_expirer.log
env PATH=/home/vagrant/hpc-workspace-v2/build/debug/bin:$PATH /usr/local/bin/bats bats/test

# prepare tests with privileges

sudo rm -rf /tmp/ws
sudo mkdir /tmp/ws
sudo chown daemon:daemon /tmp/ws

#cd hpc-workspace-v2 
sudo chown root build/release/bin/ws_restore build/release/bin/ws_allocate build/release/bin/ws_release build/release/bin/ws_restore_notest
sudo chmod u+s build/release/bin/ws_restore build/release/bin/ws_allocate build/release/bin/ws_release build/release/bin/ws_restore_notest

# get a valid config
sudo cp bats/ws.conf /etc
# create structure 
sudo build/release/bin/ws_prepare 

export PRESET=release

echo .......................................
echo ...bats tests with setuid..............
echo .......................................
sudo rm -f /tmp/ws_expirer.log
env PATH=/home/vagrant/hpc-workspace-v2/build/release/bin:$PATH /usr/local/bin/bats bats/test_setuid

sudo chmod u-s build/release/bin/ws_restore build/release/bin/ws_allocate build/release/bin/ws_release build/release/bin/ws_restore_notest
sudo setcap "CAP_DAC_OVERRIDE=p CAP_CHOWN=p CAP_FOWNER=p" build/release/bin/ws_allocate
sudo setcap "CAP_DAC_OVERRIDE=p CAP_CHOWN=p CAP_FOWNER=p" build/release/bin/ws_release
sudo setcap "CAP_DAC_OVERRIDE=p CAP_DAC_READ_SEARCH=p" build/release/bin/ws_restore
sudo setcap "CAP_DAC_OVERRIDE=p CAP_DAC_READ_SEARCH=p" build/release/bin/ws_restore_notest

echo .......................................
echo ...bats tests with setcap..............
echo .......................................
sudo rm -f /tmp/ws_expirer.log
env PATH=/home/vagrant/hpc-workspace-v2/build/release/bin:$PATH /usr/local/bin/bats bats/test_cap
   

