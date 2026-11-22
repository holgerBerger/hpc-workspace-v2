Docker container based on RockyLinux:9.5 that contains all the necessary
packages to build and run hpc-workspace-v2 with all tests.

Container can be run with three commands: 'ctest', 'bats', and 'testall'.
The 'bats' command runs all three suites: the user-mode tests (bats/test),
the setuid tests (bats/test_setuid) and the capability tests (bats/test_cap),
just like the ubuntu container and the Alma Linux VM.
