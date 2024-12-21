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

  git clone https://github.com/holgerBerger/hpc-workspace-v2.git
  cd hpc-workspace-v2/external
  ./get_externals.sh
  cd ..

  if [ "$run_coverage" = true ]; then
    cmake --preset debug -DCMAKE_CXX_FLAGS=--coverage
  else
    cmake --preset debug
  fi

  cmake --build --preset debug -j

  if [ "$run_coverage" = true ]; then
    lcov --capture --initial --config-file ../.lcovrc --directory . -o ws_base.info
  fi

  if [ "$run_ctest_test" = true ]; then
    ctest --preset debug
  fi
  if [ "$run_bats_test" = true ]; then
    bats bats/test
  fi

  if [ "$run_coverage" = true ]; then
    lcov --capture --config-file ../.lcovrc --directory . -o ws_test.info
    lcov -a ws_base.info -a ws_test.info -o ws_total.info
    genhtml  --ignore-errors source --output-directory ../coverage_report ws_total.info
  fi
fi
