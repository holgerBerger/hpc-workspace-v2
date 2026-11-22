#!/bin/bash

sudo mkdir -p /var/spool/postfix
sudo postfix set-permissions
sudo newaliases
sudo postfix start

command="$1"


run_bats_test=false
run_ctest_test=false

case "$command" in
  bats)
    run_bats_test=true
    ;;
  ctest)
    run_ctest_test=true
    ;;
  testall)
    run_bats_test=true
    run_ctest_test=true
    ;;
  *)
    echo "available commands are: bats, ctest, testall"
    exit 1
    ;;
esac


if [ "$run_bats_test" = true ] || [ "$run_ctest_test" = true ]; then

  git clone https://github.com/holgerBerger/hpc-workspace-v2.git
  cd hpc-workspace-v2

  cmake --preset debug-ninja -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"
  cmake --build --preset debug -j

  if [ "$run_bats_test" = true ]; then
    cmake --preset release-ninja -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"
    cmake --build --preset release -j

    cp build/debug/bin/ws_restore build/debug/bin/ws_restore_notest
    cp build/release/bin/ws_restore build/release/bin/ws_restore_notest

    # prepare setuid executable
    mkdir /tmp/setuid
    cp build/release/bin/ws_allocate /tmp/setuid
    cp build/release/bin/ws_release /tmp/setuid
    cp build/release/bin/ws_restore /tmp/setuid
    cp build/release/bin/ws_restore_notest /tmp/setuid
    sudo chown root /tmp/setuid/ws_allocate /tmp/setuid/ws_release /tmp/setuid/ws_restore /tmp/setuid/ws_restore_notest
    sudo chmod u+s /tmp/setuid/ws_allocate
    sudo chmod u+s /tmp/setuid/ws_release
    sudo chmod u+s /tmp/setuid/ws_restore
    sudo chmod u+s /tmp/setuid/ws_restore_notest

    # prepare capability executable
    mkdir /tmp/cap
    cp build/release/bin/ws_allocate /tmp/cap
    cp build/release/bin/ws_release /tmp/cap
    cp build/release/bin/ws_restore /tmp/cap
    cp build/release/bin/ws_restore_notest /tmp/cap
    sudo setcap "CAP_DAC_OVERRIDE=p CAP_CHOWN=p CAP_FOWNER=p" /tmp/cap/ws_allocate
    sudo setcap "CAP_DAC_OVERRIDE=p CAP_CHOWN=p CAP_FOWNER=p" /tmp/cap/ws_release
    sudo setcap "CAP_DAC_OVERRIDE=p CAP_DAC_READ_SEARCH=p CAP_FOWNER=p" /tmp/cap/ws_restore
    sudo setcap "CAP_DAC_OVERRIDE=p CAP_DAC_READ_SEARCH=p CAP_FOWNER=p" /tmp/cap/ws_restore_notest
  fi

  # generate a config identical to the VM setup (which copies bats/ws.conf to
  # /etc), except the database owner is the current user, not bin:bin
  export MYUID=$(id -u)
  export MYGID=$(id -g)
  sed -e "s/^dbuid:.*/dbuid: $MYUID/" -e "s/^dbgid:.*/dbgid: $MYGID/" bats/ws.conf | sudo tee /etc/ws.conf >/dev/null

  if [ "$run_ctest_test" = true ]; then
    ctest --preset debug
  fi
  if [ "$run_bats_test" = true ]; then
    OLDPATH=$PATH

    echo .......................................
    echo ...preparing environment...............
    echo .......................................
    sudo rm -rf /tmp/ws
    mkdir /tmp/ws

    echo "##################"
    echo "running user tests"
    echo "##################"
    export PATH=build/debug/bin:$PATH
    bats bats/test
    export PATH=$OLDPATH

    # delete old stuff
    sudo rm -rf /tmp/ws
    sudo mkdir /tmp/ws
    sudo chown daemon:daemon /tmp/ws

    # create structure
    echo .......................................
    echo ...preparing environment...............
    echo .......................................
    sudo build/release/bin/ws_prepare

    # silence leak errors for setuid as it does not work
    export ASAN_OPTIONS=detect_leaks=0

    echo "####################"
    echo "running setuid tests"
    echo "####################"
    export PATH=/tmp/setuid:build/debug/bin:$PATH
    bats bats/test_setuid
    export PATH=$OLDPATH

    if [ "$(cat /proc/sys/fs/suid_dumpable)" == "2" ]
    then
        echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
        echo capability tests will show error with ASAN
        echo as /proc/sys/fs/suid_dumpable is 2
        echo see https://github.com/llvm/llvm-project/issues/55944
        echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    fi

    export ASAN_OPTIONS=detect_leaks=0
    echo "########################"
    echo "running capability tests"
    echo "########################"
    export PATH=/tmp/cap:build/debug/bin:$PATH
    bats bats/test_cap
    export PATH=$OLDPATH

  fi
fi
