# hpc-workspace-v2

This is the construction site of next major version of hpc-workspace++ tool.

**DO NOT USE - it is incomplete**

This is very rough and not ment for usage, and I will for the time being also
not expect or accept contributions, until some things are settled.

Please use the discussion tab if you would like to share input.

## Motivation/Goals

The codebase got harder and scarier to maintain and needed a major cleanup and modernization.

- Separation of configuration and database implementation from the client tools
is the first goal.

- Better testing is the second goal.

- It is likely that all python tools will be replaced with C++ tools on the long run.

- backwards compatibility will be maintained, might have some restrictions (e.g. old config files will need some
  flags to be added)

Functional extension is possible after this is achieved.

## Environment

at the moment main development platform is

- Ubuntu 22.04.5 LTS
  - CMake 3.22.1
  - gcc 11.4.0

future test platforms:

- Ubuntu 22.04.5 LTS
  - CMake 3.22.1
  - gcc 11.4.0
  - clang 14.0.0

- Ubuntu 24.4 LTS
  - CMake 3.28.3
  - gcc 13.3.0
  - clang 18.1.3

- Rocky Linux 8.10
  - CMake 3.26.5
  - gcc 14

- Rocky Linux 9.5
  - Cmake 3.26.5
  - gcc 11.5

this list can be extended.

no intention to support old platforms like centos7, but it might work.

language level might evolve from c++17 to c++20 if there is reasons.

ws_list has a dependency to -fopenmp, can be removed from CMakeList.txt
if not available.

## Dependencies

source is fetched and build as part of this tool:
- {fmt} 
- yaml-cpp 
- rapidyaml
- Guidelines Support Library (GSL)

library taken from distribution
- boost program_options + boost system
- libcap (optional if capability support is wanted instead of setuid)

for testing:
- Catch2 
- bats

## Status

- [x] ws_allocate (C++)
- [ ] ws_extend (will remain a shell wrapper)
- [x] ws_find (new in C++)
- [x] ws_list (new in C++)
- [ ] ws_register (might get based on ws_list)
- [x] ws_release (C++) 
- [x] ws_restore (C++)
- [ ] ws_send_ical (might get based on ws_list)
- [ ] ws_share (will remain a shell script)
- [ ] ws_expirer (will be migrated to C++) 
- [ ] ws_validate_config (might be migrated to C++)
- [x] ws_prepare (new in C++)

 
## Todo

- [x] move from single file ws.conf to multifile ws.d
- [x] migrate config from yaml-cpp to ryaml and remove yaml-cpp dependency (on hold for the moment, uses both)
- [x] move to compiletime+runtime detected capability/setuid/usermode switch (usermode is for testing mainly, does not elevate privileges)
- [x] add more unit tests to existing code
- [x] build/select a better test framework for the tools
- [x] debug what is there
- [ ] migrate more tools: migrate ws_expirer, ws_validate_config
- [ ] add tests for new tools
- [ ] debug what is there
- [x] get CMake setup in better shape
- [x] remove tbb dependency
- [ ] migrate and check/correct/add documentation, guides and man pages
- [ ] test with more compilers and distributions
- [ ] do real live tests
- [ ] define and implement new DB format

## Input and ideas and contributions needed

- how to automate testing? VMs? container?
- missing features in current version?

## Howto Build

for developers

```
cmake --preset debug
cmake --build --preset debug  -j 12
```

for mold users:
```
cmake --preset debug -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"
cmake --build --preset debug -j 12
```

for lld users:
```
cmake --preset debug -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"
cmake --build --preset debug -j 12
```

for production

```
cmake --preset release
cmake --build --preset release  -j 12
```

### Source Code Formatting

We use [clang-format](https://clang.llvm.org/docs/ClangFormat.html) to ensure consistent code style across the entire
codebase. This helps us to improve readability, avoid bikeshedding, enable automation and make contributions easier.

#### Style Configuration

Our formatting rules are defined in the `.clang-format` file. This configuration file is automatically picked up when
running `clang-format`. We follow a style close to LLVM with some customizations. The style might be tweaked over time.

### How to Use

We provide a custom CMake target allowing to apply the style configuration to all source files in one sweep:

```
cmake -S . -B build
cmake --build build --target clang-format
```

Using the dry-run option, you can check the compliance of a source code file without applying the style configuration.
This is particularly useful before committing changes.

```
clang-format --dry-run --Werror <file>
```

## Testing

unit tests:

```
ctest --preset debug .
```

higher level tests (user mode only)

```
bats bats/test
```

## testing with docker


```
cd docker
sudo docker build ubuntu-24.04 -t hpcwsv2
sudo docker run hpcwsv2 testall
```

Tests in docker allow coverage analysis

```
cd docker
mkdir coverage
sudo docker run  -v $PWD/coverage:/ws/coverage_report hpcw lcov
sudo chown -R $USERNAME coverage
```

note: setcap tests will fail with ASAN error messages if sysctl `fs.suid_dumpable = 2`
Update: turned out that capability version seems to have restrictions in docker, can only be tested fully in VM
TODO: remove capabuility tests from docker 

### testing with VM

Vagrant files are provided to allow testing with rocky linux 8 and rocky linux 9.
this should allow to test capability version as well as setup with root_squash
filesystems.

```
cd vm/rocky-8
vagrant up
```

turns out that NFS root_squash and Lustre root_sqash do not behave the same way,
what works on Lustre does not on NFS, so NFS is not suitable for testing.

latest lustre versions also drop capabilites, unless 
```
lctl set_param -P mdt.<fsname>-*.enable_cap_mask=+cap_dac_read_search,cap_chown,cap_dac_override,cap_fowner
```
is used.
