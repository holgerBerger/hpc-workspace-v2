Docker container based on ubuntu:24.04 that contains all the necessary
packages to build and run hpc-workspace-v2 with all tests.
Can generate coverage data, as well.

Container can be run with four commands: 'ctest', 'bats', 'testall', and
'lcov'. The first three just run the tests. The 'lcov' command will
run all tests and produce a coverage report. The coverage test results
will be in the /ws/coverage_report folder in the container. Either
copy the folder manually to the host after the lcov run or bind mount
the folder to the host when starting the container.

