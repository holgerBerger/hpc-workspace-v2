#!/bin/bash

# user tests
sudo rm -rf /tmp/ws
mkdir /tmp/ws

echo .......................................
echo ...details in /tmp/ws-tests
echo .......................................
echo 
mkdir /tmp/ws-tests

cd /home/vagrant/hpc-workspace-v2 
echo .......................................
echo ...bats tests as user..................
echo .......................................
sudo rm -f /tmp/ws_expirer.log
env PATH=/home/vagrant/hpc-workspace-v2/build/debug/bin:$PATH /usr/local/bin/bats -p bats/test | tee /tmp/ws-tests/user-tests.out | grep failures

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
echo .......................................
echo ...preapring environment...............
echo .......................................
sudo build/release/bin/ws_prepare 

export PRESET=release

echo .......................................
echo ...bats tests with setuid..............
echo .......................................
sudo rm -f /tmp/ws_expirer.log
env PATH=/home/vagrant/hpc-workspace-v2/build/release/bin:$PATH /usr/local/bin/bats -p bats/test_setuid | tee /tmp/ws-tests/setuid-tests.out | grep failures

sudo chmod u-s build/release/bin/ws_restore build/release/bin/ws_allocate build/release/bin/ws_release build/release/bin/ws_restore_notest
sudo setcap "CAP_DAC_OVERRIDE=p CAP_CHOWN=p CAP_FOWNER=p" build/release/bin/ws_allocate
sudo setcap "CAP_DAC_OVERRIDE=p CAP_CHOWN=p CAP_FOWNER=p" build/release/bin/ws_release
sudo setcap "CAP_DAC_OVERRIDE=p CAP_DAC_READ_SEARCH=p" build/release/bin/ws_restore
sudo setcap "CAP_DAC_OVERRIDE=p CAP_DAC_READ_SEARCH=p" build/release/bin/ws_restore_notest

echo .......................................
echo ...bats tests with setcap..............
echo .......................................
sudo rm -f /tmp/ws_expirer.log
env PATH=/home/vagrant/hpc-workspace-v2/build/release/bin:$PATH /usr/local/bin/bats -p bats/test_cap | tee /tmp/ws-tests/cap-tests.out | grep failures
   


