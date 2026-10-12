#!/bin/bash

command="$1"


run_bats_test=false
run_ctest_test=false
run_coverage=false

case "$command" in
  bats)
    run_bats_test=true
    ;;
  ctest)
    run_ctest_test=true
    ;;
  lcov)
    run_bats_test=true
    run_ctest_test=true
    run_coverage=true
    ;;
  testall)
    run_bats_test=true
    run_ctest_test=true
    ;;
  *)
    echo "available commands are: bats, ctest, lcov, testall"
    echo "for lcov please mount the container directory /ws/coverage_report to get your results"
    exit 1
    ;;
esac


if [ "$run_bats_test" = true ] || [ "$run_ctest_test" = true ]; then

  cd /tmp

  git clone https://github.com/holgerBerger/hpc-workspace-v2.git
  cd hpc-workspace-v2/external
  ./get_externals.sh
  cd ..

  if [ "$run_coverage" = true ]; then
    cmake --preset debug -DCMAKE_CXX_FLAGS=--coverage -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"
  else
    cmake --preset debug -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"
  fi

  cmake --build --preset debug -j

  cmake --preset release -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"
  cmake --build --preset release -j

  cp build/debug/bin/ws_restore build/debug/bin/ws_restore_notest
  cp build/release/bin/ws_restore build/release/bin/ws_restore_notest

  # prepare setuid executable
  mkdir /tmp/setuid
  cp build/release/bin/ws_allocate /tmp/setuid
  cp build/release/bin/ws_release /tmp/setuid
  cp build/release/bin/ws_restore /tmp/setuid
  cp build/release/bin/ws_restore_notest /tmp/setuid
  sudo chown root /tmp/setuid/ws_allocate
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

  # create a config for setuid 
  export MYUID=$(id -u)
  export MYGID=$(id -g)
  sudo tee /etc/ws.conf >/dev/null <<SUDO
dbuid: $MYUID
dbgid: $MYGID
admins: [root]
adminmail: [root@a.com]
clustername: bats
duration: 10
maxextensions: 3
smtphost: mailhost
default: ws2
workspaces:
  ws1:
    database: /tmp/ws/ws1-db
    deleted: .removed
    keeptime: 7
    spaces: [/tmp/ws/ws1]
  ws2:
    database: /tmp/ws/ws2-db
    deleted: .removed
    spaceseletion: mostspace
    keeptime: 7
    spaces: [/tmp/ws/ws2/1, /tmp/ws/ws2/2]
SUDO

  if [ "$run_coverage" = true ]; then
    lcov --capture --initial --config-file /tmp/ws/.lcovrc --directory . -o ws_base.info
  fi

  if [ "$run_ctest_test" = true ]; then
    ctest --preset debug
  fi
  if [ "$run_bats_test" = true ]; then
    OLDPATH=$PATH

    echo "##################"
    echo "running user tests"
    echo "##################"
    export PATH=build/debug/bin:$PATH
    bats bats/test
    export PATH=$OLDPATH

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

  if [ "$run_coverage" = true ]; then
    lcov --capture --config-file /tmp/ws/.lcovrc --directory . -o ws_test.info
    lcov -a ws_base.info -a ws_test.info -o ws_total.info
    genhtml  --ignore-errors source --output-directory /tmp/ws/coverage_report ws_total.info
  fi
fi
